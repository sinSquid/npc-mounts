const fs=require('fs'),path=require('path');
const root=path.resolve(__dirname,'..'),src=fs.readFileSync(path.join(root,'src/plugin.cpp'),'utf8');
function take(sig){const s=src.indexOf(sig);let e=src.indexOf('{',s)+1,n=1;while(n){let c=src[e++];if(c==='{')n++;if(c==='}')n--;}return src.slice(s,e);}
fs.writeFileSync(path.join(root,'build/ray-regression.cpp'),`
#include <cassert>
#include <cstdio>
namespace RE {
struct NiPoint3{float x,y,z;NiPoint3 operator-(NiPoint3 b)const{return{x-b.x,y-b.y,z-b.z};}NiPoint3 operator*(float k)const{return{x*k,y*k,z*k};}};
struct hkVector4{float x{},y{},z{},w{};hkVector4()=default;hkVector4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){}hkVector4(NiPoint3 p):x(p.x),y(p.y),z(p.z){}};
enum class COL_LAYER{kCameraPick};
struct TESObjectREFR{};struct PlayerCharacter:TESObjectREFR{static PlayerCharacter* GetSingleton(){static PlayerCharacter p;return &p;}};
struct hkpCdBody{const hkpCdBody* parent=nullptr;};
struct Filter{unsigned layer=1;unsigned GetCollisionLayer()const{return layer;}void SetCollisionLayer(COL_LAYER){}};
struct hkpCollidable:hkpCdBody{struct{Filter collisionFilterInfo;}broadPhaseHandle;TESObjectREFR* ref=nullptr;};
namespace TESHavokUtilities{TESObjectREFR* FindCollidableRef(const hkpCollidable& c){return c.ref;}}
struct hkpShapeRayCastCollectorOutput{float hitFraction;};
struct hkpRayHitCollector{virtual void AddRayHit(const hkpCdBody&,const hkpShapeRayCastCollectorOutput&)=0;virtual ~hkpRayHitCollector()=default;float earlyOutHitFraction=1.f;};
struct hkpClosestRayHitCollector;
struct bhkPickData{struct{hkVector4 from,to;Filter filterInfo;}rayInput;hkVector4 ray;struct{const hkpCollidable* rootCollidable=nullptr;float hitFraction=1.f;bool HasHit(){return rootCollidable!=nullptr;}}rayOutput;hkpClosestRayHitCollector* closestRayHitCollector=nullptr;};
struct BSReadLockGuard{BSReadLockGuard(int&) {}};
struct bhkWorld{int worldLock=0;static float GetWorldScale(){return .5f;}void PickObject(bhkPickData& p){assert(p.rayInput.from.x==5);assert(p.rayInput.to.x==0&&p.rayInput.to.y==0);assert(p.ray.x==10&&p.ray.y==20);}};
}
${take('bool Ray(RE::bhkWorld*')}
int main(){RE::bhkWorld w;RE::bhkPickData p;assert(!Ray(&w,{10,0,0},{30,40,0},p)); puts("PASS: shared production ray displacement and zero endpoint.");}
`);
