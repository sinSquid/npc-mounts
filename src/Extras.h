#pragma once
namespace Extras {
void Initialize();
bool Handle(RE::FormID effect, RE::Actor* target);
bool Matches(RE::FormID effect);
bool Bound(RE::Actor* actor);
bool BlockMovement(RE::Actor* actor);
bool HoldAnchor(RE::Actor* actor);
bool Leashed(RE::Actor* actor);
bool Busy(RE::Actor* actor);
bool Release(RE::Actor* actor);
void ReleaseAll();
void Tick(float delta);
void AfterActorUpdate(RE::Actor* actor);
void Save(SKSE::SerializationInterface* save);
bool Load(SKSE::SerializationInterface* save, std::uint32_t type, std::uint32_t version, std::uint32_t length);
void Revert();
void PostLoad();
void Loaded(RE::Actor* actor);
std::uint64_t Session();
void Deleted(RE::FormID id);
}
