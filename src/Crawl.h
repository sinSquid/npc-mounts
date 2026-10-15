#pragma once
namespace Crawl {
bool Install();
bool Bind(RE::Actor* actor);
void Update(RE::Actor* actor,int gait,float delta);
void Remove(RE::FormID id);
void Clear();
}
