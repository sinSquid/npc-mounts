const fs=require('fs'),path=require('path'),assert=require('assert');
const root=path.resolve(__dirname,'..');
const plugin=fs.readFileSync(path.join(root,'src/plugin.cpp'),'utf8');
const seat=fs.readFileSync(path.join(root,'src/Seat.cpp'),'utf8');
function take(source,signature){
    const start=source.indexOf(signature);
    assert(start>=0,signature);
    let end=source.indexOf('{',start)+1, depth=1;
    while(depth){const c=source[end++];if(c==='{')++depth;else if(c==='}')--depth;}
    return source.slice(start,end);
}
const tick=take(plugin,'void Tick(');
const reset=tick.match(/lastSeatWrite=Seat::WriteSequence\(\);seatSilentTime=0\.f;/);
assert(reset&&tick.indexOf(reset[0])<tick.indexOf('if(SeatOutputTimedOut(delta))'));
fs.writeFileSync(path.join(root,'build/seat-watchdog-regression.cpp'),`
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <initializer_list>
namespace Seat{
std::atomic<unsigned> writes=0;
${take(seat,'unsigned WriteSequence(')}
${take(seat,'unsigned TakeWrites(')}
}
namespace RE::SendHUDMessage{unsigned notices=0;void ShowHUDMessage(const char*){++notices;}}
unsigned dismounts=0;bool riding=false;
void Dismount(bool emergency){assert(emergency&&riding);++dismounts;riding=false;}
float seatSilentTime=0;unsigned lastSeatWrite=0;
${take(plugin,'bool SeatOutputTimedOut(')}
void EnterSteady(){${reset[0]}riding=true;}
void TickSteady(float delta){if(!riding)return;${take(tick,'if(SeatOutputTimedOut(delta))')}}
int main(){
 for(float fps:{30.f,60.f,144.f}){
  EnterSteady();const auto before=dismounts;
  assert(Seat::TakeWrites()<=Seat::WriteSequence());
  for(int frame=0;frame<int(fps*10);++frame){
   if(frame%int(fps)==0){++Seat::writes;assert(Seat::TakeWrites()==1);}
   TickSteady(1/fps);
  }
  assert(riding&&dismounts==before);
  EnterSteady();
  for(int i=0;i<int(fps*2);++i)TickSteady(1/fps);
  assert(riding);
  ++Seat::writes;Seat::TakeWrites();TickSteady(1/fps);
  for(int i=0;i<int(fps*2);++i)TickSteady(1/fps);
  assert(riding&&dismounts==before);
  for(int i=0;i<int(fps*1.2f);++i)TickSteady(1/fps);
  assert(!riding&&dismounts==before+1&&RE::SendHUDMessage::notices==dismounts);
  EnterSteady();TickSteady(1/fps);assert(riding);
 }
 std::puts("PASS: actual steady-riding timeout branch, short outage recovery, logging independence and next ride reset at 30/60/144 FPS.");
}
`);
