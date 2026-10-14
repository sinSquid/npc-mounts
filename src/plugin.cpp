#include "pch.h"
#include <spdlog/sinks/basic_file_sink.h>
#include <numbers>
#include <unordered_map>
#include "Motion.h"

namespace Mounts {
constexpr auto plugin = "NPCMounts.esp";
constexpr auto ownedFlags = static_cast<RE::Actor::BOOL_FLAGS>((1u << 27) | (1u << 20) | (1u << 21) | (1u << 29));
struct State {
    std::uint32_t originalFlags;
    int animation = -1;
    bool aiEnabled = true;
    RE::ACTOR_LIFE_STATE lifeState = RE::ACTOR_LIFE_STATE::kAlive;
    bool restoreRequested = false;
};
std::unordered_map<RE::FormID, State> targets;
RE::TESFaction* mountFaction{};
RE::TESFaction* riderFaction{};
RE::SpellItem* convertSpell{};
RE::SpellItem* restoreSpell{};
RE::EffectSetting* convertEffect{};
RE::EffectSetting* restoreEffect{};
RE::ActorHandle ridden;
std::uint32_t mountControllerFlags{}, playerControllerFlags{};
bool firstPersonBefore{};
float jumpTimer{}, landTimer{}, previousHeading{}, restoreRetry{};
int rideGait{};
bool wasAir{};
float rideLogTimer{};
float lastNPCDrift{};
unsigned rideSamples{}, rideTicks{}, npcUpdates{};

bool Converted(RE::Actor* actor) { return actor && targets.contains(actor->GetFormID()); }
RE::Actor* Crosshair()
{
    auto* pick = RE::CrosshairPickData::GetSingleton();
    auto ref = pick ? pick->GetActiveTarget().get() : RE::NiPointer<RE::TESObjectREFR>{};
    return ref ? ref->As<RE::Actor>() : nullptr;
}
void HoldPose(RE::Actor* actor)
{
    // Restrained != AI disabled. The latter also stopped animation processing.
    if (!actor->IsAIEnabled()) actor->EnableAI(true);
    if (actor->AsActorState()->GetLifeState() == RE::ACTOR_LIFE_STATE::kAlive)
        actor->SetLifeState(RE::ACTOR_LIFE_STATE::kRestrained);
    auto& flags = actor->GetActorRuntimeData().boolFlags;
    constexpr auto forced = RE::Actor::BOOL_FLAGS::kForceAnimGraphUpdate;
    flags.reset(forced);
    flags.set(static_cast<RE::Actor::BOOL_FLAGS>(static_cast<std::uint32_t>(ownedFlags) & ~(1u << 29)));
    if (targets.at(actor->GetFormID()).originalFlags & (1u << 29)) flags.set(forced);
}
constexpr auto ridePhysics = static_cast<RE::CHARACTER_FLAGS>((1u << 14) | (1u << 28));
constexpr auto proxyPhysics = static_cast<RE::CHARACTER_FLAGS>((1u << 17) | (1u << 27));
void SyncMount(RE::Actor* mount, RE::Actor* player, bool force = false)
{
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
    if (mount->GetAngleZ() != player->GetAngleZ()) mount->SetHeading(player->GetAngleZ());
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
    pick.rayInput.to = RE::hkVector4(to * RE::bhkWorld::GetWorldScale());
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
bool Dismount(bool emergency = false, bool notify = true)
{
    auto mount = ridden.get();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!mount) {
        if (ridden) {
            player->RemoveFromFaction(riderFaction);
            StopMotion(player);
            player->NotifyAnimationGraph("IdleForceDefaultState");
            RestoreController(player, ridePhysics, playerControllerFlags);
        }
        ridden.reset(); jumpTimer = landTimer = 0.f; rideGait = 0; wasAir = false;
        if (firstPersonBefore && !emergency) RE::PlayerCamera::GetSingleton()->ForceFirstPerson();
        return true;
    }
    auto position = player->GetPosition();
    if (!emergency) {
        auto* cc = player->GetCharController();
        if (cc && (cc->context.currentState == RE::hkpCharacterStateType::kInAir || cc->context.currentState == RE::hkpCharacterStateType::kJumping)) {
            if (notify) RE::SendHUDMessage::ShowHUDMessage("落地后下骑；B还原请求会保留");
            return false;
        }
        bool found = false;
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
            position = candidate + RE::NiPoint3{0.f,0.f,2.f}; found = true; break;
        }
        if (!found) { if (notify) RE::SendHUDMessage::ShowHUDMessage("请移到开阔位置后下骑"); return false; }
    }
    player->RemoveFromFaction(riderFaction);
    StopMotion(player);
    player->NotifyAnimationGraph("IdleForceDefaultState");
    RestoreController(player, ridePhysics, playerControllerFlags);
    RestoreController(mount.get(), proxyPhysics, mountControllerFlags);
    mount->SetPosition(mount->GetPosition(), true); // Resync the proxy once, when returning physics.
    player->SetPosition(position, true);
    if (auto state = targets.find(mount->GetFormID()); state != targets.end()) {
        mount->AddToFaction(mountFaction, 0);
        state->second.animation = 0;
        StopMotion(mount.get());
    }
    ridden.reset(); jumpTimer = landTimer = 0.f; rideGait = 0; wasAir = false;
    if (firstPersonBefore && !emergency) RE::PlayerCamera::GetSingleton()->ForceFirstPerson();
    spdlog::info("Dismounted");
    return true;
}
bool Convert(RE::Actor* actor)
{
    if (Converted(actor)) return true;
    if (!actor || actor->IsPlayerRef() || actor->IsDead() || !actor->Get3D() || actor->GetCurrentScene() || actor->IsInKillMove() || actor->GetOccupiedFurniture() || actor->IsOnMount()) return false;
    if (!actor->Get3D()->GetObjectByName("NPC Spine2 [Spn2]")) return false;
    const auto life = actor->AsActorState()->GetLifeState();
    if (life != RE::ACTOR_LIFE_STATE::kAlive && life != RE::ACTOR_LIFE_STATE::kRestrained) return false;
    auto& flags = actor->GetActorRuntimeData().boolFlags;
    targets.emplace(actor->GetFormID(), State{flags.underlying() & static_cast<std::uint32_t>(ownedFlags), 0, actor->IsAIEnabled(), life});
    actor->StopCombat();
    HoldPose(actor);
    actor->AddToFaction(mountFaction, 0);
    StopMotion(actor);
    const bool reset = actor->NotifyAnimationGraph("IdleForceDefaultState");
    spdlog::info("Converted ref={:08X} AI={} life={} idleEvent={}", actor->GetFormID(), actor->IsAIEnabled(),
        static_cast<unsigned>(actor->AsActorState()->GetLifeState()), reset);
    return true;
}
void Restore(RE::Actor* actor)
{
    if (!Converted(actor)) return;
    const auto saved = targets.at(actor->GetFormID());
    const bool emergency = actor->IsDead() || actor->GetCurrentScene();
    if (auto mount = ridden.get(); mount && mount.get() == actor && !Dismount(emergency, !saved.restoreRequested)) {
        targets.at(actor->GetFormID()).restoreRequested = true;
        return;
    }
    actor->RemoveFromFaction(mountFaction);
    StopMotion(actor);
    auto& flags = actor->GetActorRuntimeData().boolFlags;
    if (!actor->IsDead() && actor->AsActorState()->GetLifeState() == RE::ACTOR_LIFE_STATE::kRestrained)
        actor->SetLifeState(saved.lifeState);
    flags.reset(ownedFlags);
    flags.set(static_cast<RE::Actor::BOOL_FLAGS>(saved.originalFlags));
    targets.erase(actor->GetFormID());
    if (!actor->IsDead()) actor->NotifyAnimationGraph("IdleForceDefaultState");
    actor->EnableAI(saved.aiEnabled);
    if (saved.aiEnabled && !actor->IsDead()) actor->EvaluatePackage(false, false);
    spdlog::info("Restored ref={:08X}", actor->GetFormID());
}
void RestoreAll()
{
    std::vector<RE::FormID> ids;
    for (auto& [id, state] : targets) { state.restoreRequested = true; ids.push_back(id); }
    for (auto id : ids) if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(id)) Restore(actor);
}
void Mount(RE::Actor* actor)
{
    if (!Converted(actor) || actor->IsDead() || actor->GetCurrentScene() || !actor->Get3D() || !actor->GetCharController()) return;
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* cc = player->GetCharController();
    if (!cc || player->IsOnMount() || player->IsDead()) return;
    ridden = actor->GetHandle();
    mountControllerFlags = actor->GetCharController()->flags.underlying() & static_cast<std::uint32_t>(proxyPhysics);
    playerControllerFlags = cc->flags.underlying() & static_cast<std::uint32_t>(ridePhysics);
    firstPersonBefore = RE::PlayerCamera::GetSingleton()->IsInFirstPerson();
    RE::PlayerCamera::GetSingleton()->ForceThirdPerson();
    cc->flags.set(ridePhysics);
    player->SetPosition(actor->GetPosition(), true);
    player->SetHeading(actor->GetAngleZ());
    SyncMount(actor, player, true);
    player->AddToFaction(riderFaction, 0);
    player->NotifyAnimationGraph("IdleForceDefaultState");
    rideGait = 0; previousHeading = player->GetAngleZ();
    rideLogTimer = lastNPCDrift = 0.f; rideSamples = rideTicks = npcUpdates = 0;
    spdlog::info("Mounted ref={:08X}", actor->GetFormID());
}
void Tick(RE::Actor* player, float delta)
{
    if (RE::UI::GetSingleton()->GameIsPaused()) return;
    restoreRetry = std::max(0.f, restoreRetry - delta);
    std::vector<RE::FormID> release;
    for (const auto& [id, state] : targets) {
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
        if (actor && (actor->IsDead() || actor->GetCurrentScene() || (state.restoreRequested && restoreRetry <= 0.f))) release.push_back(id);
        else if (actor && actor->Get3D()) HoldPose(actor);
    }
    for (auto id : release) Restore(RE::TESForm::LookupByID<RE::Actor>(id));
    if (restoreRetry <= 0.f) restoreRetry = 0.25f;
    auto mount = ridden.get();
    if (!mount) { if (ridden) Dismount(true); return; }
    if (!Converted(mount.get())) { Dismount(true); return; }
    if (player->IsDead() || mount->IsDead() || !mount->Get3D() || mount->GetCurrentScene()) { Dismount(true); return; }
    if (!player->GetCharController() || !mount->GetCharController()) { Dismount(true); return; }
    if (player->GetParentCell() != mount->GetParentCell()) { Dismount(true); return; }
    SyncMount(mount.get(), player);
    if (auto* cc = player->GetCharController()) cc->flags.set(ridePhysics);
    auto* cc = player->GetCharController();
    RE::hkVector4 velocity;
    cc->GetLinearVelocityImpl(velocity);
    const float speed = std::hypot(velocity.quad.m128_f32[0], velocity.quad.m128_f32[1]) * RE::bhkWorld::GetWorldScaleInverse();
    const bool air = cc && (cc->context.currentState == RE::hkpCharacterStateType::kInAir || cc->context.currentState == RE::hkpCharacterStateType::kJumping);
    if (wasAir && !air) landTimer = 0.3f;
    wasAir = air;
    jumpTimer = std::max(0.f, jumpTimer - delta);
    landTimer = std::max(0.f, landTimer - delta);
    const auto movement = RE::PlayerControls::GetSingleton()->data.moveInputVec;
    const float turn = std::remainder(player->GetAngleZ() - previousHeading, 2.f * std::numbers::pi_v<float>);
    previousHeading = player->GetAngleZ();
    int animation = Motion::Choose(speed, movement.y, delta > 0.f ? turn / delta : 0.f, rideGait);
    if (jumpTimer > 0.f) animation = 7;
    else if (air) animation = 8;
    else if (landTimer > 0.f) animation = 9;
    auto& state = targets.at(mount->GetFormID());
    SetMotion(mount.get(), state, animation);
    ++rideTicks;
    if (rideSamples < 8) {
        rideLogTimer -= delta;
        if (rideLogTimer <= 0.f) {
            const auto p = player->GetPosition();
            const auto m = mount->GetPosition();
            const auto r = mount->Get3D()->world.translate;
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
void PlayerUpdate(RE::Actor* player, float delta)
{
    originalUpdate(player, delta);
    static bool reached = false;
    if (!reached) { spdlog::info("Player update hook reached"); reached = true; }
    Tick(player, delta);
}
void NPCUpdate(RE::Actor* actor, float delta)
{
    originalNPCUpdate(actor, delta);
    auto mount = ridden.get();
    if (mount.get() != actor || actor->IsDead() || !actor->Get3D() || RE::UI::GetSingleton()->GameIsPaused()) return;
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (player->IsDead() || player->GetParentCell() != actor->GetParentCell()) return;
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
    if (!originalActivate(handler, event)) return false;
    if (!event || event->QUserEvent() != RE::UserEvents::GetSingleton()->activate) return true;
    auto* actor = Crosshair();
    if (!ridden && !Converted(actor)) return true;
    if (auto* button = event->AsButtonEvent(); button && button->IsDown()) {
        auto handle = actor ? actor->GetHandle() : RE::ActorHandle{};
        SKSE::GetTaskInterface()->AddTask([handle] {
            if (ridden) Dismount();
            else if (auto target = handle.get()) Mount(target.get());
        });
    }
    return false;
}
using Jump = bool (*)(RE::JumpHandler*, RE::InputEvent*);
Jump originalJump{};
bool JumpInput(RE::JumpHandler* handler, RE::InputEvent* event)
{
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
            SKSE::GetTaskInterface()->AddTask([handle] {
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

struct Events final : RE::BSTEventSink<RE::TESMagicEffectApplyEvent>, RE::BSTEventSink<RE::TESObjectLoadedEvent>, RE::BSTEventSink<RE::TESFormDeleteEvent> {
    RE::BSEventNotifyControl ProcessEvent(const RE::TESMagicEffectApplyEvent* event, RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*) override
    {
        if (!event || !event->caster || !event->caster->IsPlayerRef()) return RE::BSEventNotifyControl::kContinue;
        if (event->magicEffect == convertEffect->GetFormID()) {
            auto* actor = event->target ? event->target->As<RE::Actor>() : nullptr;
            auto handle = actor ? actor->GetHandle() : RE::ActorHandle{};
            SKSE::GetTaskInterface()->AddTask([handle] {
                auto target = handle.get();
                if (!Convert(target.get())) RE::SendHUDMessage::ShowHUDMessage("目标暂不支持转换（未加载、特殊骨架或场景中）");
            });
        } else if (event->magicEffect == restoreEffect->GetFormID()) {
            auto* selected = Crosshair();
            auto handle = selected ? selected->GetHandle() : RE::ActorHandle{};
            SKSE::GetTaskInterface()->AddTask([handle, hadTarget = selected != nullptr] {
                if (auto mount = ridden.get()) Restore(mount.get());
                else if (auto target = handle.get()) Restore(target.get());
                else if (!hadTarget) RestoreAll();
            });
        }
        return RE::BSEventNotifyControl::kContinue;
    }
    RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override
    {
        if (!event || !event->loaded || !targets.contains(event->formID)) return RE::BSEventNotifyControl::kContinue;
        if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(event->formID)) {
            if (targets.at(event->formID).restoreRequested) { Restore(actor); return RE::BSEventNotifyControl::kContinue; }
            HoldPose(actor);
            actor->AddToFaction(mountFaction, 0);
            targets.at(event->formID).animation = 0;
            StopMotion(actor);
            actor->NotifyAnimationGraph("IdleForceDefaultState");
        }
        return RE::BSEventNotifyControl::kContinue;
    }
    RE::BSEventNotifyControl ProcessEvent(const RE::TESFormDeleteEvent* event, RE::BSTEventSource<RE::TESFormDeleteEvent>*) override
    {
        if (event) targets.erase(event->formID);
        return RE::BSEventNotifyControl::kContinue;
    }
} events;
void Save(SKSE::SerializationInterface* save)
{
    save->OpenRecord('NMNT', 4);
    auto count = static_cast<std::uint32_t>(targets.size()); save->WriteRecordData(count);
    for (const auto& [id, state] : targets) {
        save->WriteRecordData(id); save->WriteRecordData(state.originalFlags);
        const std::uint32_t ai = state.aiEnabled; save->WriteRecordData(ai);
        save->WriteRecordData(static_cast<std::uint32_t>(state.lifeState));
        save->WriteRecordData(static_cast<std::uint32_t>(state.restoreRequested));
    }
}
void Load(SKSE::SerializationInterface* save)
{
    std::uint32_t type, version, length;
    while (save->GetNextRecordInfo(type, version, length)) {
        if (type != 'NMNT' || version < 1 || version > 4 || length < 4) continue;
        std::uint32_t count;
        const auto stride = 4u * (version + 1);
        if (save->ReadRecordData(&count, 4) != 4 || count != (length - 4) / stride || (length - 4) % stride) continue;
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t id, flags, resolved;
            if (save->ReadRecordData(&id, 4) != 4 || save->ReadRecordData(&flags, 4) != 4) break;
            std::uint32_t ai = 1; // v1 never changed AI; ordinary actors were enabled.
            if (version >= 2 && save->ReadRecordData(&ai, 4) != 4) break;
            std::uint32_t life = 0;
            if (version >= 3 && save->ReadRecordData(&life, 4) != 4) break;
            std::uint32_t pending = 0;
            if (version == 4 && save->ReadRecordData(&pending, 4) != 4) break;
            if (life != 0 && life != 6) continue;
            if (save->ResolveFormID(id, resolved)) targets.emplace(resolved, State{flags & static_cast<std::uint32_t>(ownedFlags), -1, ai != 0, static_cast<RE::ACTOR_LIFE_STATE>(life), pending != 0});
        }
    }
}
void Revert(SKSE::SerializationInterface*) { targets.clear(); ridden.reset(); jumpTimer = landTimer = restoreRetry = 0.f; rideGait = 0; wasAir = false; }
void Message(SKSE::MessagingInterface::Message* message)
{
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        auto* data = RE::TESDataHandler::GetSingleton();
        mountFaction = data->LookupForm<RE::TESFaction>(0x800, plugin);
        riderFaction = data->LookupForm<RE::TESFaction>(0x801, plugin);
        convertEffect = data->LookupForm<RE::EffectSetting>(0x802, plugin);
        restoreEffect = data->LookupForm<RE::EffectSetting>(0x803, plugin);
        convertSpell = data->LookupForm<RE::SpellItem>(0x804, plugin);
        restoreSpell = data->LookupForm<RE::SpellItem>(0x805, plugin);
        if (!mountFaction || !riderFaction || !convertEffect || !restoreEffect || !convertSpell || !restoreSpell) { spdlog::error("NPCMounts.esp missing/incomplete; no hooks installed"); return; }
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
        spdlog::info("Ready 0.1.5: publish animation rank before events; emergency restore bypasses normal dismount constraints");
    }
    if ((message->type == SKSE::MessagingInterface::kPostLoadGame || message->type == SKSE::MessagingInterface::kNewGame) && convertSpell) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        player->AddSpell(convertSpell); player->AddSpell(restoreSpell);
        player->RemoveFromFaction(riderFaction);
        std::vector<RE::FormID> ids;
        for (const auto& [id, state] : targets) ids.push_back(id);
        for (auto id : ids) if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(id)) {
            if (targets.at(id).restoreRequested) { Restore(actor); continue; }
            HoldPose(actor);
            actor->AddToFaction(mountFaction, 0); targets.at(id).animation = 0;
            StopMotion(actor);
            actor->NotifyAnimationGraph("IdleForceDefaultState");
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
    save->SetUniqueID('NMNT'); save->SetSaveCallback(Mounts::Save); save->SetLoadCallback(Mounts::Load); save->SetRevertCallback(Mounts::Revert);
    return SKSE::GetMessagingInterface()->RegisterListener(Mounts::Message);
}
