const fs=require('fs'),assert=require('assert');
const source=fs.readFileSync('src/RideHooks.h','utf8');
// Keep the production installer and hook bodies. Only redirect game address
// lookup and the global SKSE allocation to isolated fixture memory.
assert(source.includes('REL::Relocation<std::uintptr_t>'));
fs.writeFileSync('build/ride-hooks-fixture.h',source.replace('#include "RideDrive.h"','')
 .replaceAll('REL::Relocation<std::uintptr_t>','Fixture::Relocation')
 .replaceAll('SKSE::AllocTrampoline','Fixture::AllocTrampoline')
 .replaceAll('SKSE::GetTrampoline()','Fixture::trampoline'));
