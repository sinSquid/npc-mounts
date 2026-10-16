#pragma once
namespace Travel {
void Initialize();
void Remember(RE::Actor* actor);
void Forget(RE::FormID id);
void Tick(RE::Actor* player,float delta);
void Save(SKSE::SerializationInterface* save);
bool Load(SKSE::SerializationInterface* save,std::uint32_t type,std::uint32_t version,std::uint32_t length);
void Revert();
}
