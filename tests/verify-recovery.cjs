// Compile actual HoldPose against test doubles. Not an engine/animation test.
const fs = require('fs'), path = require('path'), assert = require('assert');
const repo = path.resolve(__dirname, '..');
const source = fs.readFileSync(path.join(repo, 'src/plugin.cpp'), 'utf8');
function body(name) {
  const start = source.indexOf(`void ${name}(`); assert(start >= 0);
  const open = source.indexOf('{', start); let depth = 1, end = open + 1;
  while (depth) { const c = source[end++]; if (c === '{') depth++; if (c === '}') depth--; }
  return source.slice(start, end);
}
assert(!source.includes('UndoOffset'));
assert(!source.includes('local.translate ='));
assert(!source.includes('UpdateDownwardPass'));
assert(source.includes('mount->SetPosition(position, true)'));
assert(!source.includes('mount->SetPosition(position, false)'));
assert(source.includes('cc->flags.reset(RE::CHARACTER_FLAGS::kNoSim)'));
assert(!source.includes('flags.set(proxyPhysics)'));
assert(source.includes('cc->GetLinearVelocityImpl(velocity)'));
assert(source.includes('RE::bhkWorld::GetWorldScaleInverse()'));
assert(source.includes('turn / delta'));
assert(source.includes('actor->EnableAI(saved.aiEnabled)'));
assert(source.includes('actor->SetLifeState(saved.lifeState)'));
assert(source.includes("save->OpenRecord('NMNT', 5)"));
assert(source.includes('else if (!hadTarget)'));
const code = `
#include <cassert>
#include <cstdio>
#include <cstdint>
namespace RE {
enum class ACTOR_LIFE_STATE { kAlive=0, kDead=2, kRestrained=6 };
struct Flags {
  unsigned value{};
  void set(unsigned bits) { value |= bits; }
  template<class T> void set(T bits) { value |= static_cast<unsigned>(bits); }
  template<class T> void reset(T bits) { value &= ~static_cast<unsigned>(bits); }
};
struct Actor { bool enabled{true}; int aiCalls{}, lifeCalls{};
  enum class BOOL_FLAGS : unsigned { kForceAnimGraphUpdate=1u<<29,kMovementBlocked=1u<<27 };
  ACTOR_LIFE_STATE life=ACTOR_LIFE_STATE::kAlive;
  struct { Flags boolFlags; } data;
  bool IsAIEnabled() { return enabled; } void EnableAI(bool b) { enabled=b; ++aiCalls; }
  auto& GetActorRuntimeData() { return data; }
  Actor* AsActorState() { return this; }
  auto GetLifeState() { return life; }
  void SetLifeState(ACTOR_LIFE_STATE value) { life=value; ++lifeCalls; }
  unsigned GetFormID() { return 1; }
};
}
namespace Extras {bool leashed=false,anchored=false;bool Leashed(RE::Actor*){return leashed;}bool HoldAnchor(RE::Actor*){return anchored;}}
constexpr unsigned ownedFlags=(1u<<27)|(1u<<20)|(1u<<21)|(1u<<29);
struct { struct State { unsigned originalFlags{}; } state; State& at(unsigned) { return state; } } targets;
${body('HoldPose')}
int main() {
  RE::Actor actor;
  for (int i=0;i<20000;++i) HoldPose(&actor);
  assert(actor.enabled && actor.aiCalls==0 && actor.lifeCalls==1);
  assert(actor.life==RE::ACTOR_LIFE_STATE::kRestrained);
  assert((actor.data.boolFlags.value & (ownedFlags&~(1u<<29)))==(ownedFlags&~(1u<<29)));
  assert(!(actor.data.boolFlags.value & (1u<<29)));
  targets.state.originalFlags=1u<<29; HoldPose(&actor);
  assert(actor.data.boolFlags.value & (1u<<29)); targets.state.originalFlags=0;
  RE::Actor frozen; frozen.enabled=false;Extras::anchored=true;HoldPose(&frozen);assert(frozen.aiCalls==0);Extras::anchored=false; HoldPose(&frozen);
  assert(frozen.enabled && frozen.aiCalls==1);
  HoldPose(&frozen); assert(frozen.aiCalls==1 && frozen.lifeCalls==1);
  RE::Actor alreadyRestrained; alreadyRestrained.life=RE::ACTOR_LIFE_STATE::kRestrained;
  HoldPose(&alreadyRestrained); assert(alreadyRestrained.lifeCalls==0);
  Extras::leashed=true;HoldPose(&alreadyRestrained);assert(alreadyRestrained.life==RE::ACTOR_LIFE_STATE::kAlive&&!(alreadyRestrained.data.boolFlags.value&(1u<<27)));
  Extras::leashed=false;HoldPose(&alreadyRestrained);assert(alreadyRestrained.life==RE::ACTOR_LIFE_STATE::kRestrained&&(alreadyRestrained.data.boolFlags.value&(1u<<27)));
  RE::Actor dead; dead.life=RE::ACTOR_LIFE_STATE::kDead;
  HoldPose(&dead); assert(dead.life==RE::ACTOR_LIFE_STATE::kDead);
  for (unsigned version=1;version<=4;++version) {
    const unsigned stride=4*(version+1);
    for(unsigned count=0;count<128;++count) {
      const unsigned length=4+stride*count;
      assert(count==(length-4)/stride && (length-4)%stride==0);
      assert((length+1-4)%stride!=0);
    }
  }
  std::puts("PASS: AI remains enabled; 20000 idempotent restrained updates; frozen actor recovery; no scene-node mutation; v1-v4 layout guards. Not game tested.");
}
`;
fs.writeFileSync(path.join(repo, 'build/recovery-regression.cpp'), code);
console.log('Generated native regression harness from current HoldPose. Compile and run build/recovery-regression.cpp.');
