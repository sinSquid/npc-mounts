#pragma once
namespace Wardrobe {
void Capture(RE::Actor* actor,bool clothing);
void Restore(RE::Actor* actor);
void Forget(RE::FormID id);
void Clear();
void Save(SKSE::SerializationInterface* save);
bool Load(SKSE::SerializationInterface* save,std::uint32_t type,std::uint32_t version,std::uint32_t length);
}
