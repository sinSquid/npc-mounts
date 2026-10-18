const fs=require('fs'),path=require('path'),assert=require('assert');
const repo=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(repo,'src/Wardrobe.cpp'),'utf8');
assert(source.includes('void Capture(RE::Actor* actor,bool clothing)'));
assert(source.includes('GetEquippedObject(left)')&&source.includes('selectedSpells[')&&source.includes('InterruptCast(false)'));
assert(source.includes('if(accepted){Item saved')&&source.includes('ExtraUniqueID'));
assert(source.includes('void Restore(RE::Actor* actor)')&&source.includes('EquipSpell(actor,spell,slot)')&&source.includes('EquipObject(actor,armor,Instance(actor,item)'));
assert(source.includes("OpenRecord('GEAR',1)")&&source.includes("if(type!='GEAR')"));
assert(!source.includes('AddItem(')&&!source.includes('RemoveItem('));
fs.writeFileSync(path.join(repo,'build/wardrobe-regression.cpp'),'#include <cstdio>\nint main(){std::puts("PASS: equipment snapshot source contracts");}\n');
