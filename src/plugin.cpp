#include "pch.h"
#include <spdlog/sinks/basic_file_sink.h>
#include <numbers>
#include <unordered_map>
#include <unordered_set>
#include "Motion.h"
#include "Config.h"
#include "Seat.h"
#include "RideControl.h"
#include "Extras.h"
#include "MovementCompat.h"
#include "Crawl.h"
#include "Travel.h"
#include "Wardrobe.h"
#include "RideCollision.h"
#include "RideDrive.h"
#include "RideHooks.h"
#include "Diagnostics.h"
#include "PoseOutput.h"
#include "WaterRide.h"

namespace Mounts {
constexpr auto plugin = "NPCMounts.esp";
constexpr auto ownedFlags = static_cast<RE::Actor::BOOL_FLAGS>((1u << 27) | (1u << 20) | (1u << 21) | (1u << 29));
struct State {
    std::uint32_t originalFlags;
    int animation = -1;
    bool aiEnabled = true;
    RE::ACTOR_LIFE_STATE lifeState = RE::ACTOR_LIFE_STATE::kAlive;
    bool restoreRequested = false;
    float originalScale = 0.f;
    float speedDelta = 0.f;
    bool scaleChanged = false;
    float requestedScale = -1.f; // Session-only: reapply once after load, not every frame.
    bool leashMoving = false;
    bool posePending = false; // Session-only: let the scaled model finish one native update.
};
std::unordered_map<RE::FormID, State> targets;
RE::TESFaction* mountFaction{};
RE::TESFaction* riderFaction{};
RE::SpellItem* convertSpell{};
RE::SpellItem* restoreSpell{};
RE::EffectSetting* convertEffect{};
RE::EffectSetting* legacyConvertEffect{};
RE::EffectSetting* restoreEffect{};
RE::ActorHandle ridden;
std::uint32_t mountControllerFlags{}, playerControllerFlags{};
bool firstPersonBefore{};
float jumpTimer{}, previousHeading{}, restoreRetry{};
Motion::RideState rideMotion;
bool wasAir{};
float rideLogTimer{};
float lastNPCDrift{};
unsigned rideSamples{}, rideTicks{}, npcUpdates{};
unsigned jumpAudit=0,jumpAuditBudget=0;
std::unordered_set<std::uint64_t> suppressedButtons;
Settings::Data config;
HoldGate dismountHold;
bool downActive=false; // Kept only to recover transition flags from older RIDE saves.
bool upActive=false;
float transitionTime=0.f;
constexpr float mountTransitionSeconds=.85f,dismountTransitionSeconds=.85f;
constexpr float transitionSideOffset=128.f;
float playerSpeedDelta=0.f, statusTimer=0.f;
RE::NiPoint3 downOffset;
std::uint32_t downOriginalFlags=0;
bool loadedRideState=false;
constexpr auto downFlags=static_cast<RE::Actor::BOOL_FLAGS>((1u<<27)|(1u<<20)|(1u<<21));
void BeginDismount();
void ApplySpeed(RE::Actor* actor,float multiplier,float& applied) {
    auto* values=actor->AsActorValueOwner();
    const float baseline=values->GetActorValue(RE::ActorValue::kSpeedMult)-applied;
    const float desired=baseline*(multiplier-1.f);
    // Only book a delta that was actually applied; always remove even tiny
    // contributions on restore. Otherwise sub-threshold edits drift the ledger.
    if(std::abs(desired-applied)>.001f || (multiplier==1.f && applied!=0.f)) {
        values->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary,RE::ActorValue::kSpeedMult,desired-applied);
        applied=desired;
    }
}
void RestoreScale(RE::Actor* actor,float totalScale) {
    // GetScale includes race height; SetScale takes the reference multiplier.
    const float referenceScale=actor->GetReferenceRuntimeData().refScale/100.f;
    actor->SetScale(std::round(totalScale*referenceScale/actor->GetScale()*100.f)/100.f);
}
void ApplyStats(RE::Actor* actor,State& state) {
    if(config.overrideScale) {
        if(!state.scaleChanged || state.requestedScale!=config.npcScale) {
            if(!state.scaleChanged)state.originalScale=actor->GetScale();
            actor->SetScale(config.npcScale);
            state.requestedScale=config.npcScale;state.scaleChanged=true;
        }
    } else if(state.scaleChanged) {RestoreScale(actor,state.originalScale);state.scaleChanged=false;state.requestedScale=-1.f;}
    ApplySpeed(actor,config.speedMultiplier,state.speedDelta);
}

bool Converted(RE::Actor* actor) { return actor && targets.contains(actor->GetFormID()); }
bool Restoring(RE::Actor* actor) {return Converted(actor)&&targets.at(actor->GetFormID()).restoreRequested;}
bool Riding(RE::Actor* actor) {auto mount=ridden.get();return mount&&mount.get()==actor;}
RE::Actor* Crosshair()
{
    auto* pick = RE::CrosshairPickData::GetSingleton();
    auto ref = pick ? pick->GetActiveTarget().get() : RE::NiPointer<RE::TESObjectREFR>{};
    return ref ? ref->As<RE::Actor>() : nullptr;
}
void ReleaseAnimationMotion(RE::Actor* actor){
    // Native player controls and LF's direct planner own translation/rotation.
    // A forced idle can leave these graph gates on and stall LF's handoff.
    if(actor->IsAnimationDriven())actor->SetGraphVariableBool("bAnimationDriven",false);
    if(actor->IsAllowRotation())actor->SetGraphVariableBool("bAllowRotation",false);
}
void HoldPose(RE::Actor* actor)
{
    if(Extras::HoldAnchor(actor))return;
    if(Extras::Leashed(actor)){
        if(!actor->IsAIEnabled())actor->EnableAI(true);
        if(actor->AsActorState()->GetLifeState()==RE::ACTOR_LIFE_STATE::kRestrained)actor->SetLifeState(RE::ACTOR_LIFE_STATE::kAlive);
        actor->GetActorRuntimeData().boolFlags.reset(RE::Actor::BOOL_FLAGS::kMovementBlocked);
        ReleaseAnimationMotion(actor);
        return; // LeashFramework owns movement while our OAR faction keeps crawl poses.
    }
    // Restrained != AI disabled. The latter also stopped animation processing.
    if (!actor->IsAIEnabled()) actor->EnableAI(true);
    if (actor->AsActorState()->GetLifeState() == RE::ACTOR_LIFE_STATE::kAlive)
        actor->SetLifeState(RE::ACTOR_LIFE_STATE::kRestrained);
    auto& flags = actor->GetActorRuntimeData().boolFlags;
    constexpr auto forced = RE::Actor::BOOL_FLAGS::kForceAnimGraphUpdate;
    flags.reset(forced);
    flags.set(static_cast<RE::Actor::BOOL_FLAGS>(static_cast<std::uint32_t>(ownedFlags) & ~(1u << 29)));
    flags.set(forced); // Keep generated crawl output live even with restrained locomotion.
    ReleaseAnimationMotion(actor);
}
constexpr auto ridePhysics = static_cast<RE::CHARACTER_FLAGS>((1u << 14) | (1u << 28) | (1u << 27));
constexpr auto proxyPhysics = static_cast<RE::CHARACTER_FLAGS>((1u << 17) | (1u << 27));
bool ActorReady(RE::Actor* actor){
    auto* cell=actor->GetParentCell();
    return cell&&cell->IsAttached()&&actor->Get3D();
}
bool RideReady(RE::Actor* mount,RE::Actor* player){
    if(!ActorReady(player)||!ActorReady(mount))return false;
    auto* cell=player->GetParentCell();
    if(cell==mount->GetParentCell())return true;
    // Adjacent exterior cells share coordinates. Do not treat their border as a teleport.
    auto* world=player->GetWorldspace();
    const auto offset=player->GetPosition()-mount->GetPosition();
    return world&&world==mount->GetWorldspace()&&offset.Length()<=512.f;
}
void SyncMount(RE::Actor* mount, RE::Actor* player, bool force = false)
{
    if(!RideReady(mount,player))return;
    auto* cc = mount->GetCharController();
    if (!cc) return;
    // Keep simulation/animation processing alive. Only actor-to-actor collisions
    // are suppressed; a stale NoSim proxy must not overwrite the moving reference.
    cc->flags.reset(RE::CHARACTER_FLAGS::kNoSim);
    cc->flags.set(RE::CHARACTER_FLAGS::kNoCharacterCollisions);
    cc->SetLinearVelocityImpl(RE::hkVector4());
    cc->fallTime = 0.f;
    auto position = player->GetPosition();position.z-=WaterRide::Offset();
    if (force || mount->GetPosition() != position) mount->SetPosition(position, true);
    // Use the reference rotation API instead of relying only on a heading change
    // while the actor's autonomous movement is blocked.
    const RE::NiPoint3 angle{0.f,0.f,player->GetAngleZ()+config.headingDegrees*std::numbers::pi_v<float>/180.f};
    if(force||mount->GetAngle()!=angle)mount->SetAngle(angle);
}
void RestoreController(RE::Actor* actor, RE::CHARACTER_FLAGS mask, std::uint32_t flags)
{
    if (auto* cc = actor->GetCharController()) {
        cc->flags.reset(mask);
        cc->flags.set(static_cast<RE::CHARACTER_FLAGS>(flags));
        cc->SetLinearVelocityImpl(RE::hkVector4());
        cc->fallTime = 0.f;
    }
}
bool Ray(RE::bhkWorld* world, const RE::NiPoint3& from, const RE::NiPoint3& to, RE::bhkPickData& pick)
{
    pick.rayInput.from = RE::hkVector4(from * RE::bhkWorld::GetWorldScale());
    pick.rayInput.to = RE::hkVector4(0.f, 0.f, 0.f, 0.f);
    pick.ray = RE::hkVector4((to - from) * RE::bhkWorld::GetWorldScale());
    pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kCameraPick);
    RE::BSReadLockGuard lock(world->worldLock);
    world->PickObject(pick);
    return pick.rayOutput.HasHit();
}
void StopMotion(RE::Actor* actor)
{
    actor->NotifyAnimationGraph("SprintStop");
    actor->NotifyAnimationGraph("moveStop");
}
void SetMotion(RE::Actor* actor, State& state, int animation)
{
    if (state.animation == animation) return;
    const int previous = state.animation;
    if(config.logLevel>=4&&Diagnostics::Sample(5,100))Diagnostics::Motion("Gait transition target={:08X} {}->{} (10Hz shared limit)",actor->GetFormID(),previous,animation);
    // OAR must see the new condition when an event activates a clip.
    actor->AddToFaction(mountFaction, static_cast<std::int8_t>(animation));
    state.animation = animation;
    const auto moving = [](int rank) { return rank >= 1 && rank <= 6; };
    if (previous == 3 && animation != 3) actor->NotifyAnimationGraph("SprintStop");
    if (moving(animation) && !moving(previous)) actor->NotifyAnimationGraph("moveStart");
    else if (!moving(animation) && moving(previous)) actor->NotifyAnimationGraph("moveStop");
    if (animation == 3 && previous != 3) actor->NotifyAnimationGraph("SprintStart");
}
void ResetPose(RE::Actor* actor,State& state){
    if(!Riding(actor)){WaterRide::Recover(actor);WaterRide::Remove(actor);}
    Crawl::Bind(actor);
    actor->AddToFaction(mountFaction,0);state.animation=0;
    StopMotion(actor);actor->NotifyAnimationGraph("IdleForceDefaultState");
}
bool Dismount(bool emergency = false, bool = true)
{
    RideCollision::Clear();
    RideDrive::End();
    MovementCompat::End();
    auto mount = ridden.get();
    auto* player = RE::PlayerCharacter::GetSingleton();
    ApplySpeed(player,1.f,playerSpeedDelta);
    WaterRide::End(player,mount.get());
    Seat::Clear();dismountHold.Release();
    if(downActive||upActive){auto& flags=player->GetActorRuntimeData().boolFlags;flags.reset(downFlags);flags.set(static_cast<RE::Actor::BOOL_FLAGS>(downOriginalFlags));}
    upActive=false;
    if (!mount) {
        if (ridden) {
            player->RemoveFromFaction(riderFaction);
            StopMotion(player);
            player->NotifyAnimationGraph("IdleForceDefaultState");
            RestoreController(player, ridePhysics, playerControllerFlags);
        }
        ridden.reset(); downActive=false;transitionTime=0.f; jumpTimer=0.f; rideMotion = {}; wasAir = false;
        if (firstPersonBefore && !emergency) RE::PlayerCamera::GetSingleton()->ForceFirstPerson();
        return true;
    }
    auto position = player->GetPosition();
    if (!emergency && downActive) position=position+downOffset;
    else if (!emergency) {
        auto* cc = player->GetCharController();
        // A clear side position is optional. Failure leaves the player at the
        // current position; it must never block dismount, including in mid-air.
        const float angle = mount->GetAngleZ();
        for (float side : {1.f, -1.f}) {
            auto candidate = position + RE::NiPoint3{std::cos(angle) * 96.f * side, -std::sin(angle) * 96.f * side, 0.f};
            auto* world = cc ? cc->GetHavokWorld() : nullptr;
            if (!world) break;
            RE::bhkPickData floor{};
            if (!Ray(world, candidate + RE::NiPoint3{0.f,0.f,96.f}, candidate - RE::NiPoint3{0.f,0.f,96.f}, floor)) continue;
            if (floor.rayOutput.normal.Dot3(RE::hkVector4(0.f,0.f,1.f,0.f)) < 0.6f) continue;
            candidate.z += 96.f - 192.f * floor.rayOutput.hitFraction;
            if (std::abs(candidate.z - position.z) > 64.f) continue;
            RE::bhkPickData wall{};
            if (Ray(world, position + RE::NiPoint3{0.f,0.f,48.f}, candidate + RE::NiPoint3{0.f,0.f,48.f}, wall)) continue;
            position = candidate + RE::NiPoint3{0.f,0.f,2.f}; break;
        }
    }
    player->RemoveFromFaction(riderFaction);
    StopMotion(player);
    player->NotifyAnimationGraph("IdleForceDefaultState");
    RestoreController(player, ridePhysics, playerControllerFlags);
    RestoreController(mount.get(), proxyPhysics, mountControllerFlags);
    if(!emergency){
        mount->SetPosition(mount->GetPosition(), true); // Resync only a normal, completed dismount.
        player->SetPosition(position, true);
    }
    if (auto state = targets.find(mount->GetFormID()); state != targets.end()) {
        mount->AddToFaction(mountFaction, 0);
        state->second.animation = 0;
        StopMotion(mount.get());
    }
    ridden.reset(); downActive=false;transitionTime=0.f; jumpTimer=0.f; rideMotion = {}; wasAir = false;
    if (firstPersonBefore && !emergency) RE::PlayerCamera::GetSingleton()->ForceFirstPerson();
    spdlog::info("Dismounted");
    return true;
}
void BeginDismount() {
    auto mount=ridden.get();if(!mount||downActive||upActive)return;
    downActive=true;transitionTime=0.f;dismountHold.Release();RideDrive::ResetInput();
    const float angle=mount->GetAngleZ();downOffset={std::cos(angle)*transitionSideOffset,-std::sin(angle)*transitionSideOffset,0.f};
    Seat::Entering(false);Seat::Update(RE::PlayerCharacter::GetSingleton(),mount.get(),config,0.f,downOffset);
}
bool ConversionRejected(RE::Actor* actor,const char* reason){
    spdlog::warn("Conversion rejected ref={:08X}: {}",actor?actor->GetFormID():0,reason);
    RE::SendHUDMessage::ShowHUDMessage(reason);return false;
}
bool Convert(RE::Actor* actor)
{
    if (Converted(actor)) {
        // A reset can replace the actor's graph and faction extras without
        // deleting its reference or our co-save entry. Reassert the conversion.
        if(!ActorReady(actor)||actor->IsDead()||Restoring(actor))return ConversionRejected(actor,"目标未加载、已死亡或仍在还原，请稍后重试");
        if(targets.at(actor->GetFormID()).posePending)return true;
        if(!Riding(actor)&&!Extras::Bound(actor)&&!Extras::Busy(actor)){
            HoldPose(actor);ResetPose(actor,targets.at(actor->GetFormID()));
            targets.at(actor->GetFormID()).requestedScale=-1.f;
            ApplyStats(actor,targets.at(actor->GetFormID()));
        }
        return true;
    }
    if (Extras::Bound(actor) || Extras::Busy(actor)) {
        RE::SendHUDMessage::ShowHUDMessage("请先施放人魅-解除坐骑化与束缚，再施放人魅-人形坐骑化");
        return false;
    }
    if(!actor||actor->IsPlayerRef())return ConversionRejected(actor,"请瞄准非玩家NPC施放");
    if(actor->IsDead()||!ActorReady(actor))return ConversionRejected(actor,"目标已死亡或模型尚未加载");
    RE::NiPointer<RE::Actor> nativeMount;
    if(actor->IsInKillMove()||actor->GetMount(nativeMount))return ConversionRejected(actor,"请等待目标结束击杀或下马后再转换");
    if (!actor->Get3D()->GetObjectByName("NPC Spine2 [Spn2]"))return ConversionRejected(actor,"目标模型缺少人形背骨，不能使用爬行姿态");
    const auto life = actor->AsActorState()->GetLifeState();
    if (life != RE::ACTOR_LIFE_STATE::kAlive && life != RE::ACTOR_LIFE_STATE::kRestrained)return ConversionRejected(actor,"目标正处于倒地等特殊生命状态，请恢复后重试");
    if(!Crawl::Bind(actor,false))return ConversionRejected(actor,Crawl::Available()?"目标动画骨架尚未就绪或不兼容；详情见NPCMounts日志":"爬行动作库加载失败；请检查Crawl.nmc与NPCMounts日志（不是目标骨架错误）");
    if(actor->GetOccupiedFurniture())actor->StopInteractingQuick(false);
    auto& flags = actor->GetActorRuntimeData().boolFlags;
    targets.emplace(actor->GetFormID(), State{flags.underlying() & static_cast<std::uint32_t>(ownedFlags), 0, actor->IsAIEnabled(), life});
    actor->StopCombat();
    Wardrobe::Capture(actor,config.undressOnConvert);
    ApplyStats(actor,targets.at(actor->GetFormID()));
    targets.at(actor->GetFormID()).posePending=true;
    flags.set(RE::Actor::BOOL_FLAGS::kMovementBlocked);
    StopMotion(actor);
    spdlog::info("Converted ref={:08X}; scale applied, crawl pending native model update",actor->GetFormID());
    return true;
}
void Restore(RE::Actor* actor)
{
    if(!actor)return;
    Travel::Forget(actor->GetFormID());
    if (!Converted(actor)) {Extras::Release(actor);return;}
    if(!ActorReady(actor)){targets.at(actor->GetFormID()).restoreRequested=true;return;}
    if(!Extras::Release(actor)){targets.at(actor->GetFormID()).restoreRequested=true;return;}
    const auto& saved = targets.at(actor->GetFormID());
    const auto originalFlags=saved.originalFlags;
    const auto originalLifeState=saved.lifeState;
    const auto originalScale=saved.originalScale;
    const auto aiEnabled=saved.aiEnabled;
    const auto scaleChanged=saved.scaleChanged;
    auto speedDelta=saved.speedDelta;
    const bool emergency = actor->IsDead() || actor->IsInKillMove();
    if (auto mount = ridden.get(); mount && mount.get() == actor) {
        if(!emergency){targets.at(actor->GetFormID()).restoreRequested=true;BeginDismount();return;}
        Dismount(true);
    }
    if(scaleChanged)RestoreScale(actor,originalScale);
    WaterRide::Recover(actor);WaterRide::Remove(actor);
    ApplySpeed(actor,1.f,speedDelta);
    actor->RemoveFromFaction(mountFaction);
    StopMotion(actor);
    auto& flags = actor->GetActorRuntimeData().boolFlags;
    if (!actor->IsDead() && (actor->AsActorState()->GetLifeState() == RE::ACTOR_LIFE_STATE::kRestrained ||
        actor->AsActorState()->GetLifeState() == RE::ACTOR_LIFE_STATE::kAlive))
        actor->SetLifeState(originalLifeState);
    flags.reset(ownedFlags);
    flags.set(static_cast<RE::Actor::BOOL_FLAGS>(originalFlags));
    Crawl::Remove(actor->GetFormID());targets.erase(actor->GetFormID());
    Wardrobe::Restore(actor);
    if (!actor->IsDead()) actor->NotifyAnimationGraph("IdleForceDefaultState");
    actor->EnableAI(aiEnabled);
    if (aiEnabled && !actor->IsDead()) actor->EvaluatePackage(false, false);
    spdlog::info("Restored ref={:08X}", actor->GetFormID());
    Diagnostics::Snapshot(actor,"Restore completed");
}
void RestoreAll()
{
    Extras::ReleaseAll();
    std::vector<RE::FormID> ids;
    for (auto& [id, state] : targets) { state.restoreRequested = true; ids.push_back(id); }
    for (auto id : ids) if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(id)) Restore(actor);
}
void Mount(RE::Actor* actor)
{
    Diagnostics::Scope operation(actor?actor->GetFormID():0);
    Diagnostics::Snapshot(actor,"Mount requested");
    Diagnostics::Trace("Mount gates installed={} converted={} restoring={} bound={} busy={}",RideHooks::installed,Converted(actor),Restoring(actor),Extras::Bound(actor),Extras::Busy(actor));
    Diagnostics::Trace("Ride config logLevel={} overrideScale={} scale={} speed={} jumpHeight={} allowJump={} backward={}",config.logLevel,config.overrideScale,config.npcScale,config.speedMultiplier,config.jumpHeightMultiplier,config.allowJump,config.allowBackward);
    for(const auto& field:Settings::floats)Diagnostics::Trace("Ride setting {}={}",field.key,config.*field.member);
    if(!actor)return;
    if(!RideHooks::installed){RE::SendHUDMessage::ShowHUDMessage("骑乘移动接口校验未通过，请查看 NPCMounts 日志");return;}
    if(Restoring(actor)){RE::SendHUDMessage::ShowHUDMessage("目标仍在还原，等待牵引清理完成后再转换");return;}
    if(Extras::Bound(actor)||Extras::Busy(actor)){RE::SendHUDMessage::ShowHUDMessage("请先施放人魅-解除坐骑化与束缚，或用人魅-牵引坐骑解除绳索");return;}
    if (!Converted(actor) || actor->IsDead() || actor->IsInKillMove() || !actor->Get3D() || !actor->GetCharController()) return;
    if(targets.at(actor->GetFormID()).posePending)return;
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* cc = player->GetCharController();
    if (!cc || player->IsOnMount() || player->IsDead()) return;
    if(!RideReady(actor,player))return;
    if(!Seat::Bind(player)){RE::SendHUDMessage::ShowHUDMessage(Seat::BindError());return;}
    if(!MovementCompat::Begin()){Seat::Clear();RE::SendHUDMessage::ShowHUDMessage("方向移动框架暂未释放控制，请结束当前动作后重试");return;}
    ResetPose(actor,targets.at(actor->GetFormID()));
    if(auto* proxy=actor->GetCharController()){proxy->SetLinearVelocityImpl(RE::hkVector4());proxy->outVelocity=RE::hkVector4();proxy->pushDelta=RE::hkVector4();proxy->fallTime=0.f;}
    ridden = actor->GetHandle();
    WaterRide::Begin(player,actor);
    RideDrive::Begin();
    RideCollision::Update(player,actor);
    Travel::Remember(actor);
    mountControllerFlags = actor->GetCharController()->flags.underlying() & static_cast<std::uint32_t>(proxyPhysics);
    playerControllerFlags = cc->flags.underlying() & static_cast<std::uint32_t>(ridePhysics);
    firstPersonBefore = RE::PlayerCamera::GetSingleton()->IsInFirstPerson();
    RE::PlayerCamera::GetSingleton()->ForceThirdPerson();
    downOriginalFlags=player->GetActorRuntimeData().boolFlags.underlying()&static_cast<std::uint32_t>(downFlags);
    cc->flags.set(ridePhysics);
    cc->SetLinearVelocityImpl(RE::hkVector4());cc->outVelocity=RE::hkVector4();cc->pushDelta=RE::hkVector4();cc->fallTime=0.f;
    player->SetPosition(actor->GetPosition(), true);
    player->SetHeading(actor->GetAngleZ());
    SyncMount(actor, player, true);
    player->AddToFaction(riderFaction, 0);
    player->AsActorState()->actorState1.sneaking = false;
    player->NotifyAnimationGraph("SneakStop");
    player->DrawWeaponMagicHands(false);
    player->NotifyAnimationGraph("IdleForceDefaultState");
    downActive=false;dismountHold.Release();ApplySpeed(player,config.speedMultiplier,playerSpeedDelta);
    upActive=true;transitionTime=0.f;
    const float angle=actor->GetAngleZ();downOffset={std::cos(angle)*transitionSideOffset,-std::sin(angle)*transitionSideOffset,0.f};
    Seat::Entering(true);Seat::Update(player,actor,config,1.f,downOffset);
    rideMotion = {}; previousHeading = player->GetAngleZ();
    rideLogTimer = lastNPCDrift = 0.f; rideSamples = rideTicks = npcUpdates = 0;
    jumpAudit=0;jumpAuditBudget=32;Seat::TakeWrites();
    spdlog::info("Mounted ref={:08X}", actor->GetFormID());
}
void Tick(RE::Actor* player, float delta)
{
    Travel::Tick(player,delta);
    if (RE::UI::GetSingleton()->GameIsPaused()) {dismountHold.Release();return;}
    config=Settings::Get();
    Extras::Tick(delta);
    restoreRetry = std::max(0.f, restoreRetry - delta);
    std::vector<RE::FormID> release;
    for (auto& [id, state] : targets) {
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
        if (actor && (actor->IsDead() || actor->IsInKillMove() || (state.restoreRequested && restoreRetry <= 0.f))) release.push_back(id);
        else if (actor && ActorReady(actor)) {
            if(state.posePending)continue;
            if(state.animation<0)ResetPose(actor,state); // Recover a load task that ran before 3D was ready.
            HoldPose(actor);ApplyStats(actor,state);
            if(Extras::Leashed(actor))if(auto* controller=actor->GetCharController()){
                state.leashMoving=true;
                RE::hkVector4 velocity;controller->GetLinearVelocityImpl(velocity);
                const float speed=std::hypot(velocity.quad.m128_f32[0],velocity.quad.m128_f32[1])*RE::bhkWorld::GetWorldScaleInverse();
                int gait=std::clamp(state.animation,0,3);SetMotion(actor,state,Motion::Choose(speed,1,0,gait,config.runThreshold,config.sprintThreshold));
            }
            if(!Extras::Leashed(actor)&&state.leashMoving){state.leashMoving=false;SetMotion(actor,state,0);StopMotion(actor);}
            if(!Riding(actor))Crawl::Update(actor,state.animation,delta);
        }
    }
    for (auto id : release) Restore(RE::TESForm::LookupByID<RE::Actor>(id));
    if (restoreRetry <= 0.f) restoreRetry = 0.25f;
    auto mount = ridden.get();
    if (!mount) { if (ridden) Dismount(true); return; }
    if (!Converted(mount.get())) { Dismount(true); return; }
    if (player->IsDead() || mount->IsDead() || !mount->Get3D() || mount->IsInKillMove()) { Dismount(true); return; }
    auto* cc=player->GetCharController();
    if (!cc || !mount->GetCharController()) { Dismount(true); return; }
    if (!RideReady(mount.get(),player)) { Dismount(true); return; }
    ApplySpeed(player,config.speedMultiplier,playerSpeedDelta);
    if(dismountHold.Tick(delta,config.holdSeconds)){BeginDismount();return;}
    cc->flags.set(ridePhysics);
    RE::hkVector4 velocity;
    cc->GetLinearVelocityImpl(velocity);
    if(config.logLevel>=4&&jumpAudit){
        --jumpAudit;
        Diagnostics::Trace("Jump observe remaining={} want={} state={} height={} vz={} outZ={} flags={:08X}",jumpAudit,
            static_cast<unsigned>(cc->wantState),static_cast<unsigned>(cc->context.currentState),cc->jumpHeight,
            velocity.quad.m128_f32[2],cc->outVelocity.quad.m128_f32[2],cc->flags.underlying());
    }
    const float speed = std::hypot(velocity.quad.m128_f32[0], velocity.quad.m128_f32[1]) * RE::bhkWorld::GetWorldScaleInverse();
    const bool air = cc->context.currentState == RE::hkpCharacterStateType::kInAir || cc->context.currentState == RE::hkpCharacterStateType::kJumping;
    // Jumping can still carry ground/support velocity before the jump impulse.
    // Acknowledge the pending request only after entering InAir; height was
    // already supplied to the controller, so never multiply sampled velocity.
    const bool takeoff=air&&cc->context.currentState==RE::hkpCharacterStateType::kInAir&&jumpTimer>0.f&&velocity.quad.m128_f32[2]>0.f;
    if(takeoff){
        Diagnostics::Trace("Jump takeoff speedZ={} multiplier={}",velocity.quad.m128_f32[2],config.jumpHeightMultiplier);
        jumpTimer=0.f; // Consume the accepted jump; contact flicker must not boost twice.
    }
    wasAir = air;
    jumpTimer = std::max(0.f, jumpTimer - delta);
    const auto movement = RE::PlayerControls::GetSingleton()->data.moveInputVec;
    if(upActive||downActive){
        auto& playerFlags=player->GetActorRuntimeData().boolFlags;playerFlags.set(downFlags);
        Seat::Refresh(player);
        SetMotion(mount.get(),targets.at(mount->GetFormID()),0);Crawl::Update(mount.get(),0,delta);
        if(upActive&&(!Crawl::Working(mount.get())||!Seat::Working())){
            transitionTime=std::min(transitionTime,0.f)-std::clamp(delta,0.f,.1f);
            if(transitionTime<=-3.f){Dismount(true);RE::SendHUDMessage::ShowHUDMessage("上马姿态未能输出，已解除骑乘；可重新尝试");return;}
            Seat::Motion(delta,false,0);Seat::Update(player,mount.get(),config,1.f,downOffset);return;
        }
        transitionTime=std::max(transitionTime,0.f);
        transitionTime+=delta;
        const float duration=upActive?mountTransitionSeconds:dismountTransitionSeconds;
        const float t=std::clamp(transitionTime/duration,0.f,1.f);
        Seat::Motion(delta,false,0);
        Seat::Update(player,mount.get(),config,upActive?1.f-t:t,downOffset);
        if(t>=1.f){if(upActive){upActive=false;transitionTime=0.f;playerFlags.reset(downFlags);playerFlags.set(static_cast<RE::Actor::BOOL_FLAGS>(downOriginalFlags));Seat::Entering(false);Seat::Update(player,mount.get(),config);}else{Dismount();} }
        return;
    }
    const float turn = std::remainder(player->GetAngleZ() - previousHeading, 2.f * std::numbers::pi_v<float>);
    previousHeading = player->GetAngleZ();
    const int requestedAnimation = rideMotion.Update(speed,movement.x,movement.y,delta>0.f?turn / delta:0.f,
        air,takeoff,delta,config.runThreshold,config.sprintThreshold);
    const int animation=air?2:requestedAnimation;
    auto& state = targets.at(mount->GetFormID());
    SetMotion(mount.get(), state, animation);
    Crawl::Update(mount.get(),animation,delta);
    Seat::Refresh(player);
    Seat::Motion(delta,air,animation);
    Seat::Update(player,mount.get(),config);
    ++rideTicks;
    statusTimer-=delta;
    if(statusTimer<=0){Settings::Status(fmt::format("NPC {:08X} | scale {:.2f} | speed x{:.2f} | 速度 {:.1f} | 动作 {} | 座位输出 {}",mount->GetFormID(),mount->GetScale(),config.speedMultiplier,speed,animation,Seat::Working()?"已执行":"等待"));statusTimer=.5f;}
    if (config.logLevel>=4 && rideSamples < 1200) {
        rideLogTimer -= delta;
        if (rideLogTimer <= 0.f) {
            const auto p = player->GetPosition();
            const auto m = mount->GetPosition();
            const auto r = mount->Get3D()->world.translate;
            auto* back=mount->Get3D()->GetObjectByName("NPC Spine2 [Spn2]");
            const auto b=back?back->world.translate:r;
            const auto pr=player->Get3D()->world.translate;
            Diagnostics::Motion("Ride detail ref={:08X} input=({:.3f},{:.3f}) yaw=({:.3f},{:.3f}) backRelative=({:.3f},{:.3f},{:.3f}) playerRootError=({:.3f},{:.3f},{:.3f}) controllerState={} playerFlags={:08X} seat={} scale={:.3f}",
                mount->GetFormID(),movement.x,movement.y,player->GetAngleZ(),mount->GetAngleZ(),b.x-r.x,b.y-r.y,b.z-r.z,pr.x-p.x,pr.y-p.y,pr.z-p.z,
                static_cast<unsigned>(cc->context.currentState),cc->flags.underlying(),Seat::Working(),mount->GetScale());
            Diagnostics::Motion("Ride sample={} ticks={} npcUpdates={} npcDrift={:.2f} player=({:.2f},{:.2f},{:.2f}) mount=({:.2f},{:.2f},{:.2f}) root=({:.2f},{:.2f},{:.2f}) speed={:.2f} rank={}/{} AI={} life={} proxy={:08X}",
                ++rideSamples, rideTicks, npcUpdates,lastNPCDrift,p.x,p.y,p.z,m.x,m.y,m.z,r.x,r.y,r.z,speed,state.animation,mount->GetFactionRank(mountFaction,false),mount->IsAIEnabled(),
                static_cast<unsigned>(mount->AsActorState()->GetLifeState()), mount->GetCharController()->flags.underlying());
            Diagnostics::Motion("Ride steering left={} right={} stick={:.3f} povSlide={} jumpLatched={} sync=pre-NPC/passive-movement",
                RideDrive::left,RideDrive::right,RideDrive::stick,RE::PlayerControls::GetSingleton()->data.fovSlideMode,rideMotion.jumping);
            auto* camera=RE::PlayerCamera::GetSingleton();
            const auto cameraID=camera->currentState?camera->currentState->id:RE::CameraState::kFirstPerson;
            const auto* third=camera->currentState&&cameraID==RE::CameraState::kThirdPerson?static_cast<RE::ThirdPersonState*>(camera->currentState.get()):nullptr;
            auto* pelvis=player->Get3D()->GetObjectByName("NPC Pelvis [Pelv]");
            const auto rendered=pelvis?pelvis->world.translate:pr;
            Diagnostics::Motion("Ride visual camera={} mouseSteering={} freeYaw={} freeEnabled={} seatWrites={} pelvisBack=({},{},{}) wantState={} waterDepth={}",
                static_cast<unsigned>(cameraID),config.mouseSteering,third?third->freeRotation.x:0.f,third&&third->freeRotationEnabled,
                Seat::TakeWrites(),rendered.x-b.x,rendered.y-b.y,rendered.z-b.z,static_cast<unsigned>(cc->wantState),WaterRide::Offset());
            Seat::LogOutput();Crawl::LogOutput();
            const auto mountWorld=mount->Get3D()->world;
            const auto playerWorld=player->Get3D()->world;
            const auto backLocal=mountWorld.rotate.Transpose()*(b-mountWorld.translate)/mountWorld.scale;
            const auto pelvisLocal=playerWorld.rotate.Transpose()*(rendered-playerWorld.translate)/playerWorld.scale;
            Diagnostics::Motion("Ride model back=({},{},{}) pelvis=({},{},{}) rootScale=({},{}) saddle={}",
                backLocal.x,backLocal.y,backLocal.z,pelvisLocal.x,pelvisLocal.y,pelvisLocal.z,mountWorld.scale,playerWorld.scale,
                mount->Get3D()->GetObjectByName("SaddleBone")!=nullptr);
            rideLogTimer = 0.5f;
            Diagnostics::Flush(); // One batch per sample, not per motion-stage line.
            if(rideSamples==1200)Diagnostics::Trace("Ride sample limit reached ref={:08X}; next mount resets budget",mount->GetFormID());
        }
    }
    // Rider seat translation is authored in the animation. No post-animation
    // scene-node writes or second downward pass (SMP/motion history saw both poses).
}

using Update = void (*)(RE::Actor*, float);
Update originalUpdate{};
Update originalNPCUpdate{};
void AuditMotionStage(RE::Actor* actor,const char* stage){
    if(config.logLevel<4||!ridden||rideSamples>=1200||rideLogTimer>0.02f)return;
    const unsigned slot=actor->IsPlayerRef()?(std::strcmp(stage,"player-before")==0?0:1):(std::strcmp(stage,"mount-before")==0?2:3);
    if(!Diagnostics::Sample(slot))return;
    auto* cc=actor->GetCharController();if(!cc)return;
    RE::hkVector4 v;cc->GetLinearVelocityImpl(v);
    auto* process=actor->GetActorRuntimeData().movementController.get();
    if(actor->IsPlayerRef())Diagnostics::Motion("Ride collision groups={:016X} rejected={}",RideCollision::groups.load(std::memory_order_relaxed),RideCollision::rejected.load(std::memory_order_relaxed));
    Diagnostics::Motion("Motion stage={} ref={:08X} velocity=({:.4f},{:.4f},{:.4f}) output=({:.4f},{:.4f},{:.4f}) push=({:.4f},{:.4f},{:.4f}) animationDriven={} allowRotation={} controllerMode={} controls={} ai={} contacts={}",
        stage,actor->GetFormID(),v.quad.m128_f32[0],v.quad.m128_f32[1],v.quad.m128_f32[2],
        cc->outVelocity.quad.m128_f32[0],cc->outVelocity.quad.m128_f32[1],cc->outVelocity.quad.m128_f32[2],
        cc->pushDelta.quad.m128_f32[0],cc->pushDelta.quad.m128_f32[1],cc->pushDelta.quad.m128_f32[2],
        actor->IsAnimationDriven(),actor->IsAllowRotation(),process?process->unk1C0:0,process?process->GetControlsDriven():false,process?process->GetAIDriven():false,cc->contactPointsCount);
}
// Observe only: never cancel or undo another system's teleport.
void AuditLocation(RE::Actor* actor) {
    struct Sample { RE::FormID cell; RE::NiPoint3 position; };
    static std::unordered_map<RE::FormID,Sample> previous;
    static decltype(Extras::Session()) epoch{};
    static unsigned count=0;
    if(epoch!=Extras::Session()){previous.clear();count=0;epoch=Extras::Session();}
    static bool enabled=false;
    if(config.logLevel<4){if(enabled){previous.clear();enabled=false;}return;}
    enabled=true;
    const auto id=actor->GetFormID();
    if(!Converted(actor)){previous.erase(id);return;}
    auto* cell=actor->GetParentCell();if(!cell)return;
    const Sample now{cell->GetFormID(),actor->GetPosition()};
    auto old=previous.find(id);
    if(old!=previous.end()&&count<2000){
        const auto distance=(now.position-old->second.position).Length();
        if(now.cell!=old->second.cell||distance>512.f){
            ++count;
            spdlog::info("Travel observed ref={:08X} cell={:08X}->{:08X} distance={:.2f} fixed={} leashed={} riding={} (observation, caller unknown)",
                id,old->second.cell,now.cell,distance,Extras::Bound(actor),Extras::Leashed(actor),Riding(actor));
        }
    }
    previous.insert_or_assign(id,now);
}
void PlayerUpdate(RE::Actor* player, float delta)
{
    auto currentMount=ridden.get();
    RideCollision::Update(player,currentMount.get());
    AuditMotionStage(player,"player-before");
    if(ridden&&!downActive)ReleaseAnimationMotion(player);
    if(ridden&&!RE::UI::GetSingleton()->GameIsPaused()){
        RideDrive::Before(player,delta,upActive||downActive);
        if(config.mouseSteering&&std::abs(RE::PlayerControls::GetSingleton()->data.moveInputVec.y)>.05f){
            auto* camera=RE::PlayerCamera::GetSingleton();
            if(camera->currentState&&camera->currentState->id==RE::CameraState::kThirdPerson){
                auto* state=static_cast<RE::ThirdPersonState*>(camera->currentState.get());
                RideDrive::Aim(player,state->freeRotation.x,delta);
            }
        }
    }
    originalUpdate(player, delta);
    AuditMotionStage(player,"player-after");
    static bool reached = false;
    if (!reached) { spdlog::info("Player update hook reached"); reached = true; }
    if(auto mount=ridden.get();mount&&!RE::UI::GetSingleton()->GameIsPaused()&&!player->IsDead()&&!mount->IsDead()&&RideReady(mount.get(),player)){
        WaterRide::Update(player,mount.get(),delta);SyncMount(mount.get(),player);
    }
    Tick(player, delta);
}
void NPCUpdate(RE::Actor* actor, float delta)
{
    RE::Actor* placement=Extras::BlockMovement(actor)?actor:nullptr;
    if(Riding(actor))AuditMotionStage(actor,"mount-before");
    AuditLocation(actor);
    if(Extras::Leashed(actor))HoldPose(actor); // Release gates before LF/native movement, not one frame afterwards.
    if(Riding(actor))placement=actor;
    const auto previousPlacement=RideHooks::placedActor;
    RideHooks::placedActor=placement;
    originalNPCUpdate(actor, delta);
    RideHooks::placedActor=previousPlacement;
    if(Converted(actor)&&targets.at(actor->GetFormID()).posePending&&ActorReady(actor)&&!actor->IsDead()&&Crawl::Resume(actor)){
        const auto handle=actor->GetHandle();const auto epoch=Extras::Session();
        SKSE::GetTaskInterface()->AddTask([handle,epoch]{
            if(epoch!=Extras::Session())return;
            auto ref=handle.get();if(!ref||!Converted(ref.get()))return;
            auto& state=targets.at(ref->GetFormID());if(!state.posePending||state.restoreRequested)return;
            state.posePending=false;HoldPose(ref.get());ref->AddToFaction(mountFaction,0);ref->NotifyAnimationGraph("IdleForceDefaultState");
            Diagnostics::Trace("Conversion crawl released after native update ref={:08X}",ref->GetFormID());
        });
    }
    if(Riding(actor))AuditMotionStage(actor,"mount-after");
    Extras::AfterActorUpdate(actor);
    auto mount = ridden.get();
    if (mount.get() != actor || actor->IsDead() || !actor->Get3D() || RE::UI::GetSingleton()->GameIsPaused()) return;
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (player->IsDead() || !RideReady(actor,player)) return;
    ++npcUpdates;
    lastNPCDrift = (actor->GetPosition() - player->GetPosition()).Length();
    // Observe only after publication: a second proxy teleport here makes the
    // next frame compensate for a pose/SMP result already generated this frame.
}
using Activate = bool (*)(RE::ActivateHandler*, RE::InputEvent*);
Activate originalActivate{};
bool ActivateInput(RE::ActivateHandler* handler, RE::InputEvent* event)
{
    if(ridden && event && event->QUserEvent()==RE::UserEvents::GetSingleton()->activate)return false;
    if (!event || event->QUserEvent() != RE::UserEvents::GetSingleton()->activate) return originalActivate(handler,event);
    auto* actor = Crosshair();
    if (!ridden && !Converted(actor)) return originalActivate(handler,event);
    if (auto* button = event->AsButtonEvent(); button && button->IsDown()) {
        auto handle = actor ? actor->GetHandle() : RE::ActorHandle{};
        const auto epoch=Extras::Session();
        SKSE::GetTaskInterface()->AddTask([handle,epoch] {
            if(Extras::Session()!=epoch)return;
            if (!ridden) if (auto target = handle.get()) Mount(target.get());
        });
    }
    return false;
}
using Jump = bool (*)(RE::JumpHandler*, RE::InputEvent*);
Jump originalJump{};
bool JumpInput(RE::JumpHandler* handler, RE::InputEvent* event)
{
    if(ridden&&event&&event->QUserEvent()==RE::UserEvents::GetSingleton()->jump)
        if(auto* button=event->AsButtonEvent();button&&button->IsDown())Diagnostics::Trace("Jump key allowed={} up={} down={} jumping={} air={} timer={}",config.allowJump,upActive,downActive,rideMotion.jumping,wasAir,jumpTimer);
    if(ridden && (!config.allowJump||downActive||upActive||rideMotion.jumping||wasAir||jumpTimer>0.f))return false;
    if(!ridden)return originalJump(handler,event);
    if(event&&event->QUserEvent()==RE::UserEvents::GetSingleton()->jump)
        if(auto* button=event->AsButtonEvent();button&&button->IsDown()){
            auto* player=RE::PlayerCharacter::GetSingleton();
            if(auto* cc=player->GetCharController();cc&&cc->flags.any(RE::CHARACTER_FLAGS::kCanJump)){
                // 1.5.97 native jump request ends at DBEEE0: wantState=1,
                // jumpHeight=height*HavokScale. Skip its human animation action.
                static REL::Relocation<float*> height{REL::Offset(0x1de2320)};
                cc->jumpHeight=*height*player->GetScale()*config.jumpHeightMultiplier*RE::bhkWorld::GetWorldScale();
                cc->wantState=RE::hkpCharacterStateType::kJumping;
                jumpTimer=.2f;
                if(config.logLevel>=4&&jumpAuditBudget){jumpAudit=8;--jumpAuditBudget;}
                Diagnostics::Trace("Ride jump controller request height={}",cc->jumpHeight);
            }
        }
    return false; // Never dispatch the player's human JumpHandler::ProcessButton.
}
using Talk = bool (*)(RE::Actor*, bool, bool, RE::TESTopicInfo*);
Talk originalTalk{};
bool Dialogue(RE::Actor* actor, bool begin, bool force, RE::TESTopicInfo* topic)
{
    return begin && Converted(actor) ? false : originalTalk(actor, begin, force, topic);
}
using Initiate = void (*)(RE::Actor*, RE::Actor*, RE::PackageLocation*, RE::PackageLocation*);
Initiate npcInitiate{}, playerInitiate{};
void NPCDialogue(RE::Actor* self, RE::Actor* other, RE::PackageLocation* a, RE::PackageLocation* b)
{ if (!Converted(self) && !Converted(other)) npcInitiate(self, other, a, b); }
void PlayerDialogue(RE::Actor* self, RE::Actor* other, RE::PackageLocation* a, RE::PackageLocation* b)
{ if (!Converted(other)) playerInitiate(self, other, a, b); }
using MenuMessage = RE::UI_MESSAGE_RESULTS (*)(RE::DialogueMenu*, RE::UIMessage&);
MenuMessage originalMenu{};
RE::UI_MESSAGE_RESULTS DialogueMenu(RE::DialogueMenu* menu, RE::UIMessage& message)
{
    if (message.type.any(RE::UI_MESSAGE_TYPE::kShow, RE::UI_MESSAGE_TYPE::kReshow)) {
        auto* manager = RE::MenuTopicManager::GetSingleton();
        auto speaker = manager ? manager->speaker.get() : RE::NiPointer<RE::TESObjectREFR>{};
        auto* actor = speaker ? speaker->As<RE::Actor>() : nullptr;
        if (Converted(actor)) {
            RE::UIMessageQueue::GetSingleton()->AddMessage(RE::BSFixedString(RE::DialogueMenu::MENU_NAME), RE::UI_MESSAGE_TYPE::kForceHide, nullptr);
            auto handle = actor->GetHandle();
            const auto epoch=Extras::Session();
            SKSE::GetTaskInterface()->AddTask([handle,epoch] {
                if(Extras::Session()!=epoch)return;
                if (auto target = handle.get(); Converted(target.get())) { target->StopCurrentDialogue(); target->EndDialogue(); }
            });
            return RE::UI_MESSAGE_RESULTS::kHandled;
        }
    }
    return originalMenu(menu, message);
}
using POV = bool (*)(RE::TogglePOVHandler*, RE::InputEvent*);
POV originalPOV{};
bool POVInput(RE::TogglePOVHandler* self, RE::InputEvent* event)
{
    if (ridden && event && event->QUserEvent() == RE::UserEvents::GetSingleton()->togglePOV) return false;
    return originalPOV(self, event);
}

bool BlockRideButton(RE::ButtonEvent* button)
{
    // Releases are paired by RideInput; pre-mount presses must still be released.
    if (button->IsUp()) return false;
    const auto* actions = RE::UserEvents::GetSingleton();
    const auto& action = button->QUserEvent();
    const auto key = button->GetIDCode();
    if (button->GetDevice() == RE::INPUT_DEVICE::kKeyboard &&
        ((config.blockSneak&&(key==0x1D||key==0x9D)) || (config.blockAlt&&(key==0x38||key==0xB8)&&!(config.allowBackward&&action==actions->back)))) return true;
    if ((config.blockSneak&&action==actions->sneak) || (!config.allowBackward&&action==actions->back) ||
        (!config.allowJump&&action==actions->jump) || action==actions->togglePOV ||
        (config.blockCombat&&(action==actions->shout||action==actions->readyWeapon))) return true;
    if (config.blockCombat&&(action == actions->leftAttack || action == actions->rightAttack || action == actions->dualAttack)) {
        const auto* player = RE::PlayerCharacter::GetSingleton();
        // Keep B castable; ordinary attacks/blocks/spells are not riding actions.
        if (action == actions->leftAttack) return player->GetEquippedObject(true) != restoreSpell;
        if (action == actions->rightAttack) return player->GetEquippedObject(false) != restoreSpell;
        return true;
    }
    return false;
}
struct RideInput final : RE::BSTEventSink<RE::InputEvent*> {
    RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* events, RE::BSTEventSource<RE::InputEvent*>*) override
    {
        if (!events)
            return RE::BSEventNotifyControl::kContinue;
        const auto& contexts = RE::ControlMap::GetSingleton()->GetRuntimeData().contextPriorityStack;
        const bool gameplay = !RE::UI::GetSingleton()->GameIsPaused() &&
            (contexts.empty() || contexts.back() == RE::UserEvents::INPUT_CONTEXT_ID::kGameplay);
        if(!gameplay){dismountHold.Release();RideDrive::ResetInput();}
        // The device queue owns mutable events. Remove blocked events from the
        // dispatched chain before native handlers and later mod event sinks.
        auto** link = const_cast<RE::InputEvent**>(events);
        while (auto* event = *link) {
            if (auto* button = event->AsButtonEvent()) {
                const auto* actions=RE::UserEvents::GetSingleton();
                const bool left=button->QUserEvent()==actions->strafeLeft;
                const bool right=button->QUserEvent()==actions->strafeRight;
                if((left||right)&&ridden&&gameplay){
                    if(left)RideDrive::left=!button->IsUp();
                    if(right)RideDrive::right=!button->IsUp();
                    const auto key=(std::uint64_t(button->GetDevice())<<32)|button->GetIDCode();
                    if(button->IsUp())suppressedButtons.erase(key);else suppressedButtons.insert(key);
                    *link=event->next;continue;
                }
                const bool downKey=config.dismountKey==0 ? button->QUserEvent()==RE::UserEvents::GetSingleton()->activate :
                    button->GetDevice()==RE::INPUT_DEVICE::kKeyboard && button->GetIDCode()==static_cast<unsigned>(config.dismountKey);
                if(ridden&&gameplay&&downKey){
                    if(button->IsDown()&&!downActive&&!upActive)dismountHold.Press();
                    if(button->IsUp()){
                        dismountHold.Release();
                        suppressedButtons.erase((std::uint64_t(button->GetDevice())<<32)|button->GetIDCode());
                    }
                    *link=event->next;continue;
                }
                if(ridden&&gameplay&&button->QUserEvent()==actions->jump){
                    // One owner: issue our physical request, then remove press,
                    // hold and release before native handlers/later input sinks.
                    JumpInput(nullptr,event);
                    *link=event->next;continue;
                }
                const auto key = (std::uint64_t(button->GetDevice()) << 32) | button->GetIDCode();
                bool block = false;
                if (button->IsUp()) block = suppressedButtons.erase(key) != 0;
                else if (ridden && gameplay && (BlockRideButton(button) ||
                    ((downActive||upActive) && (button->QUserEvent()==RE::UserEvents::GetSingleton()->forward || button->QUserEvent()==RE::UserEvents::GetSingleton()->back ||
                    button->QUserEvent()==RE::UserEvents::GetSingleton()->strafeLeft || button->QUserEvent()==RE::UserEvents::GetSingleton()->strafeRight || button->QUserEvent()==RE::UserEvents::GetSingleton()->jump)))) {
                    block = true;
                    if (button->IsDown()) suppressedButtons.insert(key);
                }
                if (block) { *link = event->next; continue; }
            }
            if (auto* stick = event->AsThumbstickEvent(); ridden && gameplay && stick && stick->IsLeft()) {
                RideDrive::stick=stick->xValue;
                stick->xValue=0.f;
                if(downActive||upActive){RideDrive::ResetInput();stick->yValue=0;}
                else if(!config.allowBackward)stick->yValue=std::max(0.f,stick->yValue);
            }
            link = &event->next;
        }
        return RE::BSEventNotifyControl::kContinue;
    }
} rideInput;
void RegisterRideInput()
{
    auto* input = RE::BSInputDeviceManager::GetSingleton();
    input->RemoveEventSink(&rideInput);
    input->PrependEventSink(&rideInput);
}

struct Events final : RE::BSTEventSink<RE::TESMagicEffectApplyEvent>, RE::BSTEventSink<RE::TESObjectLoadedEvent>, RE::BSTEventSink<RE::TESFormDeleteEvent>, RE::BSTEventSink<RE::TESSpellCastEvent> {
    RE::BSEventNotifyControl ProcessEvent(const RE::TESSpellCastEvent* event,RE::BSTEventSource<RE::TESSpellCastEvent>*) override{
        if(event&&event->object&&event->object->IsPlayerRef()){
            const auto* spell=RE::TESForm::LookupByID<RE::SpellItem>(event->spell);
            const auto* file=spell?spell->GetFile(0):nullptr;
            if(file&&file->GetFilename()==plugin)Diagnostics::Trace("Player spell cast spell={:08X}; effect receipt logged separately",event->spell);
        }
        return RE::BSEventNotifyControl::kContinue;
    }
    RE::BSEventNotifyControl ProcessEvent(const RE::TESMagicEffectApplyEvent* event, RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*) override
    {
        if (!event || !event->caster || !event->caster->IsPlayerRef()) return RE::BSEventNotifyControl::kContinue;
        const auto epoch=Extras::Session();
        if(event->magicEffect!=convertEffect->GetFormID()&&event->magicEffect!=restoreEffect->GetFormID()&&(!legacyConvertEffect||event->magicEffect!=legacyConvertEffect->GetFormID())){
            if(!Extras::Matches(event->magicEffect))return RE::BSEventNotifyControl::kContinue;
            Diagnostics::Trace("Spell received effect={:08X} target={:08X} epoch={}",event->magicEffect,event->target?event->target->GetFormID():0,epoch);
            auto* selected=event->target?event->target->As<RE::Actor>():nullptr;
            const auto handle=selected?selected->GetHandle():RE::ActorHandle{};const auto effect=event->magicEffect;
            SKSE::GetTaskInterface()->AddTask([handle,effect,epoch]{if(Extras::Session()!=epoch)return;auto target=handle.get();Extras::Handle(effect,target.get());});
            return RE::BSEventNotifyControl::kContinue;
        }
        if (event->magicEffect == convertEffect->GetFormID() || (legacyConvertEffect&&event->magicEffect==legacyConvertEffect->GetFormID())) {
            auto* actor = event->target ? event->target->As<RE::Actor>() : nullptr;
            auto handle = actor ? actor->GetHandle() : RE::ActorHandle{};
            Diagnostics::Trace("Convert spell queued target={:08X} undress={} epoch={}",actor?actor->GetFormID():0,Settings::Get().undressOnConvert,epoch);
            SKSE::GetTaskInterface()->AddTask([handle,epoch] {
                if(Extras::Session()!=epoch)return;
                auto target = handle.get();
                Diagnostics::Scope operation(target?target->GetFormID():0);
                Diagnostics::Snapshot(target.get(),"Convert spell executing");
                Convert(target.get());
            });
        } else if (event->magicEffect == restoreEffect->GetFormID()) {
            auto* selected = Crosshair();
            auto handle = selected ? selected->GetHandle() : RE::ActorHandle{};
            SKSE::GetTaskInterface()->AddTask([handle,epoch, hadTarget = selected != nullptr] {
                if(Extras::Session()!=epoch)return;
                Diagnostics::Scope operation(0);
                Diagnostics::Trace("Restore spell executing hadTarget={} ridden={}",hadTarget,static_cast<bool>(ridden));
                if (auto mount = ridden.get()) Restore(mount.get());
                else if (auto target = handle.get()) Restore(target.get());
                else if (!hadTarget) RestoreAll();
            });
        }
        return RE::BSEventNotifyControl::kContinue;
    }
    RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override
    {
        if (!event || !event->loaded) return RE::BSEventNotifyControl::kContinue;
        if(!RE::TESForm::LookupByID<RE::Actor>(event->formID))return RE::BSEventNotifyControl::kContinue;
        const auto id=event->formID;const auto epoch=Extras::Session();
        SKSE::GetTaskInterface()->AddTask([id,epoch]{
            if(Extras::Session()!=epoch)return;
            auto* actor=RE::TESForm::LookupByID<RE::Actor>(id);
            if(!actor||!actor->Get3D()||!actor->GetParentCell()||!actor->GetParentCell()->IsAttached())return;
            Extras::Loaded(actor);
            if(!targets.contains(id))return;
            if (targets.at(id).restoreRequested) { Restore(actor); return; }
            if(actor->IsDead())return;
            if(targets.at(id).posePending)return;
            HoldPose(actor);
            ResetPose(actor,targets.at(id));
        });
        return RE::BSEventNotifyControl::kContinue;
    }
    RE::BSEventNotifyControl ProcessEvent(const RE::TESFormDeleteEvent* event, RE::BSTEventSource<RE::TESFormDeleteEvent>*) override
    {
        if (event) {WaterRide::Forget(event->formID);Travel::Forget(event->formID);Crawl::Remove(event->formID);Wardrobe::Forget(event->formID);targets.erase(event->formID);Extras::Deleted(event->formID);}
        return RE::BSEventNotifyControl::kContinue;
    }
} events;
void Save(SKSE::SerializationInterface* save)
{
    Diagnostics::Trace("Serialization save targets={} riding={}",targets.size(),static_cast<bool>(ridden));
    save->OpenRecord('NMNT', 5);
    auto count = static_cast<std::uint32_t>(targets.size()); save->WriteRecordData(count);
    for (const auto& [id, state] : targets) {
        save->WriteRecordData(id); save->WriteRecordData(state.originalFlags);
        const std::uint32_t ai = state.aiEnabled; save->WriteRecordData(ai);
        save->WriteRecordData(static_cast<std::uint32_t>(state.lifeState));
        save->WriteRecordData(static_cast<std::uint32_t>(state.restoreRequested));
        save->WriteRecordData(state.originalScale);save->WriteRecordData(state.speedDelta);save->WriteRecordData(static_cast<std::uint32_t>(state.scaleChanged));
    }
    if(ridden){
        save->OpenRecord('RIDE',1);save->WriteRecordData(playerSpeedDelta);save->WriteRecordData(playerControllerFlags);
        save->WriteRecordData(static_cast<std::uint32_t>(downActive||upActive));save->WriteRecordData(downOriginalFlags);
    }
    {auto mount=ridden.get();WaterRide::Save(save,mount.get());}
    Extras::Save(save);
    Wardrobe::Save(save);
    Travel::Save(save);
}
void Load(SKSE::SerializationInterface* save)
{
    Diagnostics::Trace("Serialization load begin");
    std::uint32_t type, version, length;
    while (save->GetNextRecordInfo(type, version, length)) {
        if(WaterRide::Load(save,type,version,length))continue;
        if(Travel::Load(save,type,version,length))continue;
        if(Extras::Load(save,type,version,length))continue;
        if(Wardrobe::Load(save,type,version,length))continue;
        if(type=='RIDE'&&version==1&&length==16){
            std::uint32_t active{};
            loadedRideState=save->ReadRecordData(&playerSpeedDelta,4)==4&&save->ReadRecordData(&playerControllerFlags,4)==4&&
                save->ReadRecordData(&active,4)==4&&save->ReadRecordData(&downOriginalFlags,4)==4;
            downActive=active!=0;
            if(!std::isfinite(playerSpeedDelta)){loadedRideState=false;playerSpeedDelta=0;}
            continue;
        }
        if (type != 'NMNT' || version < 1 || version > 5 || length < 4) continue;
        std::uint32_t count;
        const auto stride = version==5 ? 32u : 4u * (version + 1);
        if (save->ReadRecordData(&count, 4) != 4 || count != (length - 4) / stride || (length - 4) % stride) continue;
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t id, flags, resolved;
            if (save->ReadRecordData(&id, 4) != 4 || save->ReadRecordData(&flags, 4) != 4) break;
            std::uint32_t ai = 1; // v1 never changed AI; ordinary actors were enabled.
            if (version >= 2 && save->ReadRecordData(&ai, 4) != 4) break;
            std::uint32_t life = 0;
            if (version >= 3 && save->ReadRecordData(&life, 4) != 4) break;
            std::uint32_t pending = 0;
            if (version >= 4 && save->ReadRecordData(&pending, 4) != 4) break;
            float scale=0,speed=0;std::uint32_t changed=0;
            if(version==5&&(save->ReadRecordData(&scale,4)!=4||save->ReadRecordData(&speed,4)!=4||save->ReadRecordData(&changed,4)!=4))break;
            if(!std::isfinite(scale)||!std::isfinite(speed)||(changed&&(scale<=0||scale>100)))continue;
            if (life != 0 && life != 6) continue;
            if (save->ResolveFormID(id, resolved)) targets.emplace(resolved, State{flags & static_cast<std::uint32_t>(ownedFlags), -1, ai != 0, static_cast<RE::ACTOR_LIFE_STATE>(life), pending != 0,scale,speed,changed!=0});
        }
    }
}
void Revert(SKSE::SerializationInterface*) { WaterRide::Reset();RideDrive::End();Travel::Revert();Crawl::Clear();Wardrobe::Clear();MovementCompat::End();Extras::Revert();Seat::Clear();targets.clear();ridden.reset();suppressedButtons.clear();dismountHold.Release();upActive=downActive=loadedRideState=false;transitionTime=0;playerSpeedDelta=0;jumpTimer=restoreRetry=0.f;rideMotion={};wasAir=false; }
void MigrateKnownSpells(RE::PlayerCharacter* player){
    WaterRide::Remove(player);
    auto* data=RE::TESDataHandler::GetSingleton();
    const std::array<std::pair<std::uint32_t,RE::SpellItem*>,2> migrations{{
        {0x80D,data->LookupForm<RE::SpellItem>(0x80C,plugin)},
        {0x823,convertSpell}
    }};
    for(const auto& [id,replacement]:migrations){
        auto* legacy=data->LookupForm<RE::SpellItem>(id,plugin);
        if(legacy&&replacement&&player->HasSpell(legacy))
            if(player->HasSpell(replacement)||player->AddSpell(replacement))player->RemoveSpell(legacy);
    }
}
void Message(SKSE::MessagingInterface::Message* message)
{
    Diagnostics::Trace("SKSE message type={}",message->type);
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        auto* data = RE::TESDataHandler::GetSingleton();
        Settings::Initialize();config=Settings::Get();Diagnostics::SetLevel(config.logLevel);Settings::InstallMenu();
        Diagnostics::Trace("Initial setting LogLevel={}",config.logLevel);
        for(const auto& field:Settings::floats)Diagnostics::Trace("Initial setting {}={}",field.key,config.*field.member);
        for(const auto& field:Settings::bools)Diagnostics::Trace("Initial setting {}={}",field.key,config.*field.member);
        MovementCompat::Initialize();
        if(!Seat::Install())spdlog::error("Seat/dismount animation data missing or invalid; mounting disabled");
        if(!Crawl::Install())spdlog::error("Crawl motion data missing or invalid; conversion disabled");
        if(!PoseOutput::Install()){spdlog::error("Pose publication hook unavailable; NPCMounts gameplay hooks not installed");return;}
        mountFaction = data->LookupForm<RE::TESFaction>(0x800, plugin);
        riderFaction = data->LookupForm<RE::TESFaction>(0x801, plugin);
        WaterRide::ability=data->LookupForm<RE::SpellItem>(0x825,plugin);
        convertEffect = data->LookupForm<RE::EffectSetting>(0x802, plugin);
        legacyConvertEffect = data->LookupForm<RE::EffectSetting>(0x822, plugin);
        restoreEffect = data->LookupForm<RE::EffectSetting>(0x803, plugin);
        convertSpell = data->LookupForm<RE::SpellItem>(0x804, plugin);
        restoreSpell = data->LookupForm<RE::SpellItem>(0x805, plugin);
        if (!mountFaction || !riderFaction || !convertEffect || !restoreEffect || !convertSpell || !restoreSpell) { spdlog::error("NPCMounts.esp missing/incomplete; no hooks installed"); return; }
        Extras::Initialize();
        Travel::Initialize();
        RideCollision::Install();
        RideHooks::Install();
        REL::Relocation<std::uintptr_t> player{RE::VTABLE_PlayerCharacter[0]};
        REL::Relocation<std::uintptr_t> npc{RE::VTABLE_Character[0]};
        REL::Relocation<std::uintptr_t> activate{RE::VTABLE_ActivateHandler[0]};
        REL::Relocation<std::uintptr_t> jump{RE::VTABLE_JumpHandler[0]};
        originalUpdate = reinterpret_cast<Update>(player.write_vfunc(0xAD, PlayerUpdate));
        originalNPCUpdate = reinterpret_cast<Update>(npc.write_vfunc(0xAD, NPCUpdate));
        originalActivate = reinterpret_cast<Activate>(activate.write_vfunc(1, ActivateInput));
        originalJump = reinterpret_cast<Jump>(jump.write_vfunc(1, JumpInput));
        originalTalk = reinterpret_cast<Talk>(npc.write_vfunc(0x41, Dialogue));
        npcInitiate = reinterpret_cast<Initiate>(npc.write_vfunc(0xD8, NPCDialogue));
        playerInitiate = reinterpret_cast<Initiate>(player.write_vfunc(0xD8, PlayerDialogue));
        REL::Relocation<std::uintptr_t> menu{RE::VTABLE_DialogueMenu[0]};
        originalMenu = reinterpret_cast<MenuMessage>(menu.write_vfunc(4, DialogueMenu));
        REL::Relocation<std::uintptr_t> pov{RE::VTABLE_TogglePOVHandler[0]};
        originalPOV = reinterpret_cast<POV>(pov.write_vfunc(1, POVInput));
        auto* source = RE::ScriptEventSourceHolder::GetSingleton();
        source->AddEventSink<RE::TESMagicEffectApplyEvent>(&events);
        source->AddEventSink<RE::TESObjectLoadedEvent>(&events);
        source->AddEventSink<RE::TESFormDeleteEvent>(&events);
        source->AddEventSink<RE::TESSpellCastEvent>(&events);
        RegisterRideInput();
        spdlog::info("Ready 0.4.4: equipment restore, capture, human anchor and transition poses");
        spdlog::info("Runtime Y4.16 candidate: water recovery ledger, equipment snapshot lifetime and transactional INI saving; game validation pending");
        spdlog::info("Build compiled {} {}: game validation pending",__DATE__,__TIME__);
    }
    if ((message->type == SKSE::MessagingInterface::kPostLoadGame || message->type == SKSE::MessagingInterface::kNewGame) && convertSpell) {
        if(message->type==SKSE::MessagingInterface::kPostLoadGame&&!message->data)return;
        auto* player = RE::PlayerCharacter::GetSingleton();
        MigrateKnownSpells(player);
        if(loadedRideState){
            ApplySpeed(player,1.f,playerSpeedDelta);RestoreController(player,ridePhysics,playerControllerFlags&static_cast<std::uint32_t>(ridePhysics));
            if(downActive){auto& flags=player->GetActorRuntimeData().boolFlags;flags.reset(downFlags);flags.set(static_cast<RE::Actor::BOOL_FLAGS>(downOriginalFlags&static_cast<std::uint32_t>(downFlags)));}
        }
        loadedRideState=downActive=false;dismountHold.Release();Seat::Clear();
        RegisterRideInput();
        // Spells are learned from BOOK records. Preserve spells already learned in old saves.
        player->RemoveFromFaction(riderFaction);
        Extras::PostLoad();
        Travel::Initialize();
        std::vector<RE::FormID> ids;
        for (const auto& [id, state] : targets) ids.push_back(id);
        for (auto id : ids) if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(id)) {
            WaterRide::Recover(actor);WaterRide::Remove(actor);
            if(!actor->Get3D()||!actor->GetParentCell()||!actor->GetParentCell()->IsAttached())continue;
            if (targets.at(id).restoreRequested) { Restore(actor); continue; }
            if(actor->IsDead())continue;
            HoldPose(actor);
            ApplyStats(actor,targets.at(id));
            ResetPose(actor,targets.at(id));
        }
    }
}
}
SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse);
    const auto startup=Settings::Read(Settings::iniPath);
    if (auto path = SKSE::log::log_directory()) {
        Diagnostics::Initialize(*path,startup.logLevel);
    } else {
        Diagnostics::SetLevel(startup.logLevel);
    }
    auto* save = SKSE::GetSerializationInterface();
    save->SetUniqueID('NMNT'); save->SetSaveCallback(Mounts::Save); save->SetLoadCallback(Mounts::Load); save->SetRevertCallback([](SKSE::SerializationInterface* s){RideCollision::Clear();Mounts::Revert(s);});
    return SKSE::GetMessagingInterface()->RegisterListener(Mounts::Message);
}
