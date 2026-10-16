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
};
std::unordered_map<RE::FormID, State> targets;
RE::TESFaction* mountFaction{};
RE::TESFaction* riderFaction{};
RE::SpellItem* convertSpell{};
RE::SpellItem* restoreSpell{};
RE::EffectSetting* convertEffect{};
RE::EffectSetting* undressEffect{};
RE::EffectSetting* restoreEffect{};
RE::ActorHandle ridden;
std::uint32_t mountControllerFlags{}, playerControllerFlags{};
bool firstPersonBefore{};
float jumpTimer{}, landTimer{}, previousHeading{}, restoreRetry{};
int rideGait{};
Motion::RideState rideMotion;
bool wasAir{};
float rideLogTimer{};
float lastNPCDrift{};
unsigned rideSamples{}, rideTicks{}, npcUpdates{};
std::unordered_set<std::uint64_t> suppressedButtons;
Settings::Data config;
HoldGate dismountHold;
bool downActive=false;
bool upActive=false;
float upElapsed=0.f,upDuration=.85f;
RE::NiPoint3 upOffset;
float downElapsed=0.f, downDuration=.85f, playerSpeedDelta=0.f, statusTimer=0.f;
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
void ApplyStats(RE::Actor* actor,State& state) {
    if(config.overrideScale) {
        if(!state.scaleChanged || state.requestedScale!=config.npcScale) {
            if(!state.scaleChanged)state.originalScale=actor->GetScale();
            actor->SetScale(config.npcScale);
            state.requestedScale=config.npcScale;state.scaleChanged=true;
        }
    } else if(state.scaleChanged) {actor->SetScale(state.originalScale);state.scaleChanged=false;state.requestedScale=-1.f;}
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
    auto* cell=player->GetParentCell();
    return cell==mount->GetParentCell()&&ActorReady(player)&&ActorReady(mount);
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
    const auto position = player->GetPosition();
    if (force || mount->GetPosition() != position) mount->SetPosition(position, true);
    // Use the reference rotation API instead of relying only on a heading change
    // while the actor's autonomous movement is blocked.
    mount->SetAngle(RE::NiPoint3{0.f, 0.f, player->GetAngleZ()+config.headingDegrees*std::numbers::pi_v<float>/180.f});
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
    Crawl::Bind(actor);
    actor->AddToFaction(mountFaction,0);state.animation=0;
    StopMotion(actor);actor->NotifyAnimationGraph("IdleForceDefaultState");
}
bool Dismount(bool emergency = false, bool = true)
{
    RideCollision::Clear();
    MovementCompat::End();
    auto mount = ridden.get();
    auto* player = RE::PlayerCharacter::GetSingleton();
    ApplySpeed(player,1.f,playerSpeedDelta);
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
        ridden.reset(); downActive=false; jumpTimer = landTimer = 0.f; rideGait = 0; rideMotion = {}; wasAir = false;
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
    ridden.reset(); downActive=false; jumpTimer = landTimer = 0.f; rideGait = 0; rideMotion = {}; wasAir = false;
    if (firstPersonBefore && !emergency) RE::PlayerCamera::GetSingleton()->ForceFirstPerson();
    spdlog::info("Dismounted");
    return true;
}
void BeginDismount() {
    auto mount=ridden.get();if(!mount||downActive||upActive)return;
    auto* player=RE::PlayerCharacter::GetSingleton();
    downActive=true;downElapsed=0;downDuration=config.dismountSeconds;dismountHold.Release();
    auto& flags=player->GetActorRuntimeData().boolFlags;downOriginalFlags=flags.underlying()&static_cast<std::uint32_t>(downFlags);flags.set(downFlags);
    // Use a clear side if available; no usable side means an in-place animation.
    downOffset={};auto* cc=player->GetCharController();auto* world=cc?cc->GetHavokWorld():nullptr;
    const auto pos=player->GetPosition();const float yaw=player->GetAngleZ();
    const float distance=72.f*(mount->Get3D()?mount->Get3D()->world.scale:1.f);
    if(world)for(float side:{-1.f,1.f}){
        const RE::NiPoint3 offset{std::cos(yaw)*distance*side,-std::sin(yaw)*distance*side,0};
        RE::bhkPickData wall{};
        if(!Ray(world,pos+RE::NiPoint3{0,0,48},pos+offset+RE::NiPoint3{0,0,48},wall)){downOffset=offset;break;}
    }
    StopMotion(player);StopMotion(mount.get());SetMotion(mount.get(),targets.at(mount->GetFormID()),0);
    player->NotifyAnimationGraph("IdleForceDefaultState");
    Seat::Update(player,mount.get(),config,0.f,downOffset);
    spdlog::info("Dismount animation started duration={:.2f}",downDuration);
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
    if(actor->IsInKillMove()||actor->IsOnMount())return ConversionRejected(actor,"请等待目标结束击杀或下马后再转换");
    if (!actor->Get3D()->GetObjectByName("NPC Spine2 [Spn2]"))return ConversionRejected(actor,"目标模型缺少人形背骨，不能使用爬行姿态");
    const auto life = actor->AsActorState()->GetLifeState();
    if (life != RE::ACTOR_LIFE_STATE::kAlive && life != RE::ACTOR_LIFE_STATE::kRestrained)return ConversionRejected(actor,"目标正处于倒地等特殊生命状态，请恢复后重试");
    if(!Crawl::Bind(actor))return ConversionRejected(actor,"目标动画骨架尚未就绪或不兼容；详情见NPCMounts日志");
    if(actor->GetOccupiedFurniture())actor->StopInteractingQuick(false);
    auto& flags = actor->GetActorRuntimeData().boolFlags;
    targets.emplace(actor->GetFormID(), State{flags.underlying() & static_cast<std::uint32_t>(ownedFlags), 0, actor->IsAIEnabled(), life});
    actor->StopCombat();
    HoldPose(actor);
    ApplyStats(actor,targets.at(actor->GetFormID()));
    actor->AddToFaction(mountFaction, 0);
    StopMotion(actor);
    const bool reset = actor->NotifyAnimationGraph("IdleForceDefaultState");
    spdlog::info("Converted ref={:08X} AI={} life={} idleEvent={}", actor->GetFormID(), actor->IsAIEnabled(),
        static_cast<unsigned>(actor->AsActorState()->GetLifeState()), reset);
    return true;
}
void Restore(RE::Actor* actor)
{
    if(actor)Travel::Forget(actor->GetFormID());
    if (!Converted(actor)) {Extras::Release(actor);return;}
    if(!ActorReady(actor)){targets.at(actor->GetFormID()).restoreRequested=true;return;}
    if(!Extras::Release(actor)){targets.at(actor->GetFormID()).restoreRequested=true;return;}
    const auto saved = targets.at(actor->GetFormID());
    const bool emergency = actor->IsDead() || actor->IsInKillMove();
    if (auto mount = ridden.get(); mount && mount.get() == actor) {
        if(!emergency){targets.at(actor->GetFormID()).restoreRequested=true;BeginDismount();return;}
        Dismount(true);
    }
    if(saved.scaleChanged)actor->SetScale(saved.originalScale);
    auto speedDelta=saved.speedDelta;ApplySpeed(actor,1.f,speedDelta);
    actor->RemoveFromFaction(mountFaction);
    StopMotion(actor);
    auto& flags = actor->GetActorRuntimeData().boolFlags;
    if (!actor->IsDead() && (actor->AsActorState()->GetLifeState() == RE::ACTOR_LIFE_STATE::kRestrained ||
        actor->AsActorState()->GetLifeState() == RE::ACTOR_LIFE_STATE::kAlive))
        actor->SetLifeState(saved.lifeState);
    flags.reset(ownedFlags);
    flags.set(static_cast<RE::Actor::BOOL_FLAGS>(saved.originalFlags));
    Crawl::Remove(actor->GetFormID());targets.erase(actor->GetFormID());
    if (!actor->IsDead()) actor->NotifyAnimationGraph("IdleForceDefaultState");
    actor->EnableAI(saved.aiEnabled);
    if (saved.aiEnabled && !actor->IsDead()) actor->EvaluatePackage(false, false);
    spdlog::info("Restored ref={:08X}", actor->GetFormID());
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
    if(Restoring(actor)){RE::SendHUDMessage::ShowHUDMessage("目标仍在还原，等待牵引清理完成后再转换");return;}
    if(Extras::Bound(actor)||Extras::Busy(actor)){RE::SendHUDMessage::ShowHUDMessage("请先施放人魅-解除坐骑化与束缚，或用人魅-牵引坐骑解除绳索");return;}
    if (!Converted(actor) || actor->IsDead() || actor->IsInKillMove() || !actor->Get3D() || !actor->GetCharController()) return;
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* cc = player->GetCharController();
    if (!cc || player->IsOnMount() || player->IsDead()) return;
    if(!RideReady(actor,player))return;
    if(!Seat::Bind(player)){RE::SendHUDMessage::ShowHUDMessage(Seat::BindError());return;}
    if(!MovementCompat::Begin()){Seat::Clear();RE::SendHUDMessage::ShowHUDMessage("方向移动框架暂未释放控制，请结束当前动作后重试");return;}
    ridden = actor->GetHandle();
    RideCollision::Update(player,actor);
    Travel::Remember(actor);
    mountControllerFlags = actor->GetCharController()->flags.underlying() & static_cast<std::uint32_t>(proxyPhysics);
    playerControllerFlags = cc->flags.underlying() & static_cast<std::uint32_t>(ridePhysics);
    firstPersonBefore = RE::PlayerCamera::GetSingleton()->IsInFirstPerson();
    upOffset=player->GetPosition()-actor->GetPosition();
    if(upOffset.Length()>160.f)upOffset={72.f,0,0};
    RE::PlayerCamera::GetSingleton()->ForceThirdPerson();
    cc->flags.set(ridePhysics);
    player->SetPosition(actor->GetPosition(), true);
    player->SetHeading(actor->GetAngleZ());
    SyncMount(actor, player, true);
    player->AddToFaction(riderFaction, 0);
    player->AsActorState()->actorState1.sneaking = false;
    player->NotifyAnimationGraph("SneakStop");
    player->DrawWeaponMagicHands(false);
    player->NotifyAnimationGraph("IdleForceDefaultState");
    downActive=false;dismountHold.Release();ApplySpeed(player,config.speedMultiplier,playerSpeedDelta);
    upActive=true;upElapsed=0;upDuration=config.mountSeconds;
    auto& flags=player->GetActorRuntimeData().boolFlags;downOriginalFlags=flags.underlying()&static_cast<std::uint32_t>(downFlags);flags.set(downFlags);
    Seat::Entering(true);Seat::Update(player,actor,config,1.f,upOffset);
    rideGait = 0; rideMotion = {}; previousHeading = player->GetAngleZ();
    rideLogTimer = lastNPCDrift = 0.f; rideSamples = rideTicks = npcUpdates = 0;
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
    for (const auto& [id, state] : targets) {
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
        if (actor && (actor->IsDead() || actor->IsInKillMove() || (state.restoreRequested && restoreRetry <= 0.f))) release.push_back(id);
        else if (actor && ActorReady(actor)) {
            if(state.animation<0)ResetPose(actor,targets.at(id)); // Recover a load task that ran before 3D was ready.
            HoldPose(actor);ApplyStats(actor,targets.at(id));
            if(Extras::Leashed(actor))if(auto* controller=actor->GetCharController()){
                targets.at(id).leashMoving=true;
                RE::hkVector4 velocity;controller->GetLinearVelocityImpl(velocity);
                const float speed=std::hypot(velocity.quad.m128_f32[0],velocity.quad.m128_f32[1])*RE::bhkWorld::GetWorldScaleInverse();
                int gait=std::clamp(state.animation,0,3);SetMotion(actor,targets.at(id),Motion::Choose(speed,1,0,gait,config.runThreshold,config.sprintThreshold));
            }
            if(!Extras::Leashed(actor)&&state.leashMoving){targets.at(id).leashMoving=false;SetMotion(actor,targets.at(id),0);StopMotion(actor);}
            if(!Riding(actor))Crawl::Update(actor,state.animation,delta);
        }
    }
    for (auto id : release) Restore(RE::TESForm::LookupByID<RE::Actor>(id));
    if (restoreRetry <= 0.f) restoreRetry = 0.25f;
    auto mount = ridden.get();
    if (!mount) { if (ridden) Dismount(true); return; }
    if (!Converted(mount.get())) { Dismount(true); return; }
    if (player->IsDead() || mount->IsDead() || !mount->Get3D() || mount->IsInKillMove()) { Dismount(true); return; }
    if (!player->GetCharController() || !mount->GetCharController()) { Dismount(true); return; }
    if (!RideReady(mount.get(),player)) { Dismount(true); return; }
    SyncMount(mount.get(), player);
    ApplySpeed(player,config.speedMultiplier,playerSpeedDelta);
    if(upActive){
        if(upElapsed>=upDuration&&(Seat::Finished()||upElapsed>=upDuration+.25f)){
            upActive=false;auto& flags=player->GetActorRuntimeData().boolFlags;flags.reset(downFlags);flags.set(static_cast<RE::Actor::BOOL_FLAGS>(downOriginalFlags));
            Seat::Entering(false);Seat::Update(player,mount.get(),config);
            if(targets.at(mount->GetFormID()).restoreRequested)BeginDismount();
        }else{
            upElapsed+=delta;
            if(auto* controller=player->GetCharController()){RE::hkVector4 v;controller->GetLinearVelocityImpl(v);v.quad.m128_f32[0]=v.quad.m128_f32[1]=0;controller->SetLinearVelocityImpl(v);}
            Crawl::Update(mount.get(),0,delta);
            Seat::Update(player,mount.get(),config,1.f-std::min(upElapsed/upDuration,1.f),upOffset);return;
        }
    }
    if(dismountHold.Tick(delta,config.holdSeconds))BeginDismount();
    if(downActive){
        // Animation generation can be asynchronous or skipped for a frame.
        // Wait for its final output, but never trap the player if the graph stops.
        if(downElapsed>=downDuration && (Seat::Finished() || downElapsed>=downDuration+.25f)){
            if(!Seat::Finished())spdlog::warn("Dismount final pose not observed; releasing after timeout");
            const auto id=mount->GetFormID();Dismount();if(targets.contains(id)&&targets.at(id).restoreRequested)Restore(mount.get());return;
        }
        downElapsed+=delta;
        if(auto* controller=player->GetCharController()) {RE::hkVector4 v;controller->GetLinearVelocityImpl(v);v.quad.m128_f32[0]=v.quad.m128_f32[1]=0;controller->SetLinearVelocityImpl(v);}
        Seat::Update(player,mount.get(),config,std::min(downElapsed/downDuration,1.f),downOffset);
        Crawl::Update(mount.get(),0,delta);
        return;
    }
    Seat::Update(player,mount.get(),config);
    if (auto* cc = player->GetCharController()) cc->flags.set(ridePhysics);
    auto* cc = player->GetCharController();
    RE::hkVector4 velocity;
    cc->GetLinearVelocityImpl(velocity);
    const float speed = std::hypot(velocity.quad.m128_f32[0], velocity.quad.m128_f32[1]) * RE::bhkWorld::GetWorldScaleInverse();
    const bool air = cc && (cc->context.currentState == RE::hkpCharacterStateType::kInAir || cc->context.currentState == RE::hkpCharacterStateType::kJumping);
    const bool takeoff=air&&!wasAir&&jumpTimer>0.f&&velocity.quad.m128_f32[2]>0.f;
    if(air&&!wasAir&&jumpTimer>0.f&&velocity.quad.m128_f32[2]>0.f){
        velocity.quad.m128_f32[2]*=std::sqrt(config.jumpHeightMultiplier);
        cc->SetLinearVelocityImpl(velocity); // One takeoff impulse, not a per-frame multiplier.
        jumpTimer=0.f; // Consume the accepted jump; contact flicker must not boost twice.
    }
    if (wasAir && !air) landTimer = 0.3f;
    wasAir = air;
    jumpTimer = std::max(0.f, jumpTimer - delta);
    landTimer = std::max(0.f, landTimer - delta);
    const auto movement = RE::PlayerControls::GetSingleton()->data.moveInputVec;
    const float turn = std::remainder(player->GetAngleZ() - previousHeading, 2.f * std::numbers::pi_v<float>);
    previousHeading = player->GetAngleZ();
    const int animation = rideMotion.Update(speed,movement.x,movement.y,delta>0.f?turn / delta:0.f,
        air,takeoff,delta,config.runThreshold,config.sprintThreshold);
    auto& state = targets.at(mount->GetFormID());
    SetMotion(mount.get(), state, animation);
    Crawl::Update(mount.get(),animation,delta);
    ++rideTicks;
    statusTimer-=delta;
    if(statusTimer<=0){Settings::Status(fmt::format("NPC {:08X} | scale {:.2f} | speed x{:.2f} | 速度 {:.1f} | 动作 {} | 座位输出 {}",mount->GetFormID(),mount->GetScale(),config.speedMultiplier,speed,animation,Seat::Working()?"已执行":"等待"));statusTimer=.5f;}
    if (config.debugLog && rideSamples < 1200) {
        rideLogTimer -= delta;
        if (rideLogTimer <= 0.f) {
            const auto p = player->GetPosition();
            const auto m = mount->GetPosition();
            const auto r = mount->Get3D()->world.translate;
            auto* back=mount->Get3D()->GetObjectByName("NPC Spine2 [Spn2]");
            const auto b=back?back->world.translate:r;
            const auto pr=player->Get3D()->world.translate;
            spdlog::info("Ride detail ref={:08X} input=({:.3f},{:.3f}) yaw=({:.3f},{:.3f}) backRelative=({:.3f},{:.3f},{:.3f}) playerRootError=({:.3f},{:.3f},{:.3f}) controllerState={} playerFlags={:08X} seat={} scale={:.3f}",
                mount->GetFormID(),movement.x,movement.y,player->GetAngleZ(),mount->GetAngleZ(),b.x-r.x,b.y-r.y,b.z-r.z,pr.x-p.x,pr.y-p.y,pr.z-p.z,
                static_cast<unsigned>(cc->context.currentState),cc->flags.underlying(),Seat::Working(),mount->GetScale());
            spdlog::info("Ride sample={} ticks={} npcUpdates={} npcDrift={:.2f} player=({:.2f},{:.2f},{:.2f}) mount=({:.2f},{:.2f},{:.2f}) root=({:.2f},{:.2f},{:.2f}) speed={:.2f} rank={}/{} AI={} life={} proxy={:08X}",
                ++rideSamples, rideTicks, npcUpdates,lastNPCDrift,p.x,p.y,p.z,m.x,m.y,m.z,r.x,r.y,r.z,speed,state.animation,mount->GetFactionRank(mountFaction,false),mount->IsAIEnabled(),
                static_cast<unsigned>(mount->AsActorState()->GetLifeState()), mount->GetCharController()->flags.underlying());
            rideLogTimer = 0.5f;
        }
    }
    // Rider seat translation is authored in the animation. No post-animation
    // scene-node writes or second downward pass (SMP/motion history saw both poses).
}

using Update = void (*)(RE::Actor*, float);
Update originalUpdate{};
Update originalNPCUpdate{};
void AuditMotionStage(RE::Actor* actor,const char* stage){
    if(!config.debugLog||!ridden||rideSamples>=1200||rideLogTimer>0.02f)return;
    auto* cc=actor->GetCharController();if(!cc)return;
    RE::hkVector4 v;cc->GetLinearVelocityImpl(v);
    auto* process=actor->GetActorRuntimeData().movementController.get();
    if(actor->IsPlayerRef())spdlog::info("Ride collision groups={:016X} rejected={}",RideCollision::groups.load(std::memory_order_relaxed),RideCollision::rejected.load(std::memory_order_relaxed));
    spdlog::info("Motion stage={} ref={:08X} velocity=({:.4f},{:.4f},{:.4f}) output=({:.4f},{:.4f},{:.4f}) push=({:.4f},{:.4f},{:.4f}) animationDriven={} allowRotation={} controllerMode={} controls={} ai={} contacts={}",
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
    if(!config.debugLog){previous.clear();return;}
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
    originalUpdate(player, delta);
    AuditMotionStage(player,"player-after");
    static bool reached = false;
    if (!reached) { spdlog::info("Player update hook reached"); reached = true; }
    Tick(player, delta);
}
void NPCUpdate(RE::Actor* actor, float delta)
{
    if(Riding(actor))AuditMotionStage(actor,"mount-before");
    AuditLocation(actor);
    if(Extras::Leashed(actor))HoldPose(actor); // Release gates before LF/native movement, not one frame afterwards.
    // Supply the current transform before animation/model publication as well.
    if (auto mount = ridden.get(); mount.get() == actor && !RE::UI::GetSingleton()->GameIsPaused()) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player->IsDead() && !actor->IsDead() && RideReady(actor,player)) SyncMount(actor, player);
    }
    originalNPCUpdate(actor, delta);
    if(Riding(actor))AuditMotionStage(actor,"mount-after");
    Extras::AfterActorUpdate(actor);
    auto mount = ridden.get();
    if (mount.get() != actor || actor->IsDead() || !actor->Get3D() || RE::UI::GetSingleton()->GameIsPaused()) return;
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (player->IsDead() || !RideReady(actor,player)) return;
    ++npcUpdates;
    lastNPCDrift = (actor->GetPosition() - player->GetPosition()).Length();
    // Character::Update can run after Player::Update and publish its own proxy
    // position. Synchronize after that update too, without touching bone nodes.
    SyncMount(actor, player);
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
    if(ridden && (!config.allowJump||downActive||upActive))return false;
    const bool accepted = originalJump(handler, event);
    if (accepted && ridden && event && event->QUserEvent() == RE::UserEvents::GetSingleton()->jump)
        if (auto* button = event->AsButtonEvent(); button && button->IsDown()) jumpTimer = 0.2f;
    return accepted; // Player's native controller handles collision, gravity and jumping.
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
        if(!gameplay)dismountHold.Release();
        // The device queue owns mutable events. Remove blocked events from the
        // dispatched chain before native handlers and later mod event sinks.
        auto** link = const_cast<RE::InputEvent**>(events);
        while (auto* event = *link) {
            if (auto* button = event->AsButtonEvent()) {
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
                if(downActive||upActive)stick->xValue=stick->yValue=0;
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

struct Events final : RE::BSTEventSink<RE::TESMagicEffectApplyEvent>, RE::BSTEventSink<RE::TESObjectLoadedEvent>, RE::BSTEventSink<RE::TESFormDeleteEvent> {
    RE::BSEventNotifyControl ProcessEvent(const RE::TESMagicEffectApplyEvent* event, RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*) override
    {
        if (!event || !event->caster || !event->caster->IsPlayerRef()) return RE::BSEventNotifyControl::kContinue;
        const auto epoch=Extras::Session();
        if(event->magicEffect!=convertEffect->GetFormID()&&event->magicEffect!=restoreEffect->GetFormID()&&event->magicEffect!=undressEffect->GetFormID()){
            if(!Extras::Matches(event->magicEffect))return RE::BSEventNotifyControl::kContinue;
            auto* selected=event->target?event->target->As<RE::Actor>():nullptr;
            const auto handle=selected?selected->GetHandle():RE::ActorHandle{};const auto effect=event->magicEffect;
            SKSE::GetTaskInterface()->AddTask([handle,effect,epoch]{if(Extras::Session()!=epoch)return;auto target=handle.get();Extras::Handle(effect,target.get());});
            return RE::BSEventNotifyControl::kContinue;
        }
        if (event->magicEffect == convertEffect->GetFormID() || event->magicEffect==undressEffect->GetFormID()) {
            auto* actor = event->target ? event->target->As<RE::Actor>() : nullptr;
            auto handle = actor ? actor->GetHandle() : RE::ActorHandle{};
            const bool undress=event->magicEffect==undressEffect->GetFormID();
            SKSE::GetTaskInterface()->AddTask([handle,epoch,undress] {
                if(Extras::Session()!=epoch)return;
                auto target = handle.get();
                if(Convert(target.get())&&undress)Wardrobe::Undress(target.get());
            });
        } else if (event->magicEffect == restoreEffect->GetFormID()) {
            auto* selected = Crosshair();
            auto handle = selected ? selected->GetHandle() : RE::ActorHandle{};
            SKSE::GetTaskInterface()->AddTask([handle,epoch, hadTarget = selected != nullptr] {
                if(Extras::Session()!=epoch)return;
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
            HoldPose(actor);
            ResetPose(actor,targets.at(id));
        });
        return RE::BSEventNotifyControl::kContinue;
    }
    RE::BSEventNotifyControl ProcessEvent(const RE::TESFormDeleteEvent* event, RE::BSTEventSource<RE::TESFormDeleteEvent>*) override
    {
        if (event) {Travel::Forget(event->formID);Crawl::Remove(event->formID);targets.erase(event->formID);Extras::Deleted(event->formID);}
        return RE::BSEventNotifyControl::kContinue;
    }
} events;
void Save(SKSE::SerializationInterface* save)
{
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
    Extras::Save(save);
    Travel::Save(save);
}
void Load(SKSE::SerializationInterface* save)
{
    std::uint32_t type, version, length;
    while (save->GetNextRecordInfo(type, version, length)) {
        if(Travel::Load(save,type,version,length))continue;
        if(Extras::Load(save,type,version,length))continue;
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
void Revert(SKSE::SerializationInterface*) { Travel::Revert();Crawl::Clear();MovementCompat::End();Extras::Revert();Seat::Clear();targets.clear();ridden.reset();suppressedButtons.clear();dismountHold.Release();upActive=downActive=loadedRideState=false;playerSpeedDelta=0; jumpTimer = landTimer = restoreRetry = 0.f; rideGait = 0; rideMotion = {}; wasAir = false; }
void Message(SKSE::MessagingInterface::Message* message)
{
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        auto* data = RE::TESDataHandler::GetSingleton();
        Settings::Initialize();config=Settings::Get();Settings::InstallMenu();
        MovementCompat::Initialize();
        if(!Seat::Install())spdlog::error("Seat/dismount animation data missing or invalid; mounting disabled");
        if(!Crawl::Install())spdlog::error("Crawl motion data missing or invalid; conversion disabled");
        mountFaction = data->LookupForm<RE::TESFaction>(0x800, plugin);
        riderFaction = data->LookupForm<RE::TESFaction>(0x801, plugin);
        convertEffect = data->LookupForm<RE::EffectSetting>(0x802, plugin);
        undressEffect = data->LookupForm<RE::EffectSetting>(0x822, plugin);
        restoreEffect = data->LookupForm<RE::EffectSetting>(0x803, plugin);
        convertSpell = data->LookupForm<RE::SpellItem>(0x804, plugin);
        restoreSpell = data->LookupForm<RE::SpellItem>(0x805, plugin);
        if (!mountFaction || !riderFaction || !convertEffect || !undressEffect || !restoreEffect || !convertSpell || !restoreSpell) { spdlog::error("NPCMounts.esp missing/incomplete; no hooks installed"); return; }
        Extras::Initialize();
        Travel::Initialize();
        RideCollision::Install();
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
        RegisterRideInput();
        spdlog::info("Ready 0.4.2: toggle leash and clothing unequip conversion");
        spdlog::info("Diagnostic build 20261016: bounded ride samples + converted actor relocation observations; shared configuration steps");
        spdlog::info("Gait I compiled {} {}: input-gated locomotion, confirmed jump, stable transitions",__DATE__,__TIME__);
    }
    if ((message->type == SKSE::MessagingInterface::kPostLoadGame || message->type == SKSE::MessagingInterface::kNewGame) && convertSpell) {
        if(message->type==SKSE::MessagingInterface::kPostLoadGame&&!message->data)return;
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* data=RE::TESDataHandler::GetSingleton();
        auto* legacy=data->LookupForm<RE::SpellItem>(0x80D,plugin);
        auto* toggle=data->LookupForm<RE::SpellItem>(0x80C,plugin);
        if(legacy&&toggle&&player->HasSpell(legacy)){
            if(player->HasSpell(toggle)||player->AddSpell(toggle))player->RemoveSpell(legacy);
        }
        if(loadedRideState){
            ApplySpeed(player,1.f,playerSpeedDelta);RestoreController(player,ridePhysics,playerControllerFlags&static_cast<std::uint32_t>(ridePhysics));
            if(downActive){auto& flags=player->GetActorRuntimeData().boolFlags;flags.reset(downFlags);flags.set(static_cast<RE::Actor::BOOL_FLAGS>(downOriginalFlags&static_cast<std::uint32_t>(downFlags)));}
        }
        loadedRideState=downActive=false;dismountHold.Release();Seat::Clear();
        RegisterRideInput();
        // A-F are learned from BOOK records. Preserve spells already learned in old saves.
        player->RemoveFromFaction(riderFaction);
        Extras::PostLoad();
        Travel::Initialize();
        std::vector<RE::FormID> ids;
        for (const auto& [id, state] : targets) ids.push_back(id);
        for (auto id : ids) if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(id)) {
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
    if (auto path = SKSE::log::log_directory()) {
        *path /= "NPCMounts.log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        spdlog::set_default_logger(std::make_shared<spdlog::logger>("NPCMounts", sink));
        spdlog::default_logger()->flush_on(spdlog::level::info);
    }
    auto* save = SKSE::GetSerializationInterface();
    save->SetUniqueID('NMNT'); save->SetSaveCallback(Mounts::Save); save->SetLoadCallback(Mounts::Load); save->SetRevertCallback([](SKSE::SerializationInterface* s){RideCollision::Clear();Mounts::Revert(s);});
    return SKSE::GetMessagingInterface()->RegisterListener(Mounts::Message);
}
