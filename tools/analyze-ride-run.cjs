// Summarize a whole archived run; observations, not animation/game verdicts.
const fs=require('fs'),assert=require('assert');
const file=process.argv[2];assert(file);
const lines=fs.readFileSync(file,'utf8').split(/\r?\n/);
const pick=pattern=>lines.filter(line=>pattern.test(line));
const takeoffs=pick(/Jump takeoff speedZ=/).map(line=>Number(line.match(/speedZ=([^ ]+)/)[1]));
const jumps=lines.join('\n').split('Ride jump controller request').slice(1).map(chunk=>{
 const frames=[...chunk.matchAll(/Jump observe remaining=(\d+) want=(\d+) state=(\d+) height=(\S+) vz=(\S+)/g)]
  .map(m=>({state:Number(m[3]),vz:Number(m[5])}));
 return {frames,firstUpward:frames.findIndex(x=>x.vz>1),takeoffLog:chunk.includes('Jump takeoff')};
});
const visuals=pick(/Ride visual camera=/).map(line=>{
 const p=line.match(/pelvisBack=\(([^,]+),([^,]+),([^\)]+)\)/);
 return {writes:Number(line.match(/seatWrites=(\d+)/)[1]),horizontal:p?Math.hypot(Number(p[1]),Number(p[2])):0};
});
console.log(JSON.stringify({file,lines:lines.length,loadedBuild:pick(/Runtime [A-Z]:/),
 mounts:pick(/Mounted ref=/).length,dismounts:pick(/\[info\] Dismounted$/).length,
 transitionStarts:pick(/Dismount animation started/).length,
 saveBegins:pick(/Menu save: begin/).length,saveClosed:pick(/Menu save: file closed success=true/).length,
 saveComplete:pick(/Menu save: render completed/).length,
 acceptedGraphs:pick(/Seat bind accepted/).length,rejectedGraphs:pick(/Seat bind rejected/).length,
 distinctGraphRejections:[...new Set(pick(/Seat bind rejected/).map(line=>line.slice(line.indexOf('Seat bind rejected'))))],
 jumpKeys:pick(/Jump key allowed/).length,controllerRequests:pick(/Ride jump controller request/).length,
 observedTakeoffVelocities:takeoffs,observedTakeoffsAbovePointOne:takeoffs.filter(x=>x>.1).length,
 restored:pick(/Restored ref=/),cameraTelemetryLines:visuals.length,jumpSequences:jumps,
 visualWindowsWithoutWrites:visuals.filter(x=>x.writes===0).length,
 horizontalPelvisBackAbove20AfterFirst:visuals.slice(1).filter(x=>x.horizontal>20).length,
 maxHorizontalPelvisBackAfterFirst:Math.max(0,...visuals.slice(1).map(x=>x.horizontal)),
 notes:'Historical seat=true and per-window output counts do not by themselves prove correct rendered pose or alignment.'},null,2));
