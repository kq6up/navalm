/* Exercise the shipped browser code: input formats, sky orientation and stale responses. */
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const page=fs.readFileSync('page.html','utf8');
const code=page.split('/* ---------- planetarium ---------- */')[1].split('/* ---------- annual book ---------- */')[0];
const formatting=vm.createContext({});
vm.runInContext(page.slice(page.indexOf('const pad ='),page.indexOf('const alt =')),formatting);
assert.equal(vm.runInContext('dm(5+9.99/60,2)',formatting),'05° 10.0′');
assert.equal(vm.runInContext('dm(5+59.99/60,2)',formatting),'06° 00.0′');
const nodes={};
const $=id=>nodes[id]??=({value:'',checked:true,disabled:false,reportValidity:()=>true});
const pending=[];
const context=vm.createContext({$,console,URLSearchParams,Date,Math,Number,String,Error,
  lat:x=>x+' N',lon:x=>x+' E',alt:x=>String(x),loadSetup:()=>({}),
  endEdit:()=>{},fillSight:s=>context.transferred=s,show:s=>context.shown=s,renderActiveDR:()=>{},refresh:()=>{},
  fetch:()=>new Promise(resolve=>pending.push(resolve))});
vm.runInContext(code,context);
const call=(expression)=>vm.runInContext(expression,context);
for(const [text,isLon,wanted] of [
 ['N 34° 02.5′',false,34+2.5/60],['W 117° 21.9′',true,-117-21.9/60],
 ['34 02.5 N',false,34+2.5/60],['117 21 54 W',true,-117-21.9/60],
 ['-0 30',false,-.5],['0',false,0],['180 E',true,180],['34.5',false,34.5]]){
  assert(Math.abs(call(`skyPosition(${JSON.stringify(text)},${isLon})`)-wanted)<1e-10,text);
}
for(const text of ['34 60 N','34 00 60 N','34.5 2 N','90 1 N','W 34','garbage','N 34 S'])
  assert.throws(()=>call(`skyPosition(${JSON.stringify(text)},false)`),text);
assert(call('skyPoint(0,90).x')<360);
assert(call('skyPoint(0,270).x')>360);
assert.equal(call('skyPoint(0,0).y'),50);
assert.equal(call('skyPoint(90,180).y'),360);
context.fixture={date:'2026-06-21',time:'03:00:00',lat:34,lon:-117,catalogue_count:58,
  eligible:1,selected:1,requested:3,geometry:0,sun_altitude:-8,light:'Nautical twilight',engine:'precise',
  bodies:[{index:58,name:'Sun',body:'Sun',is_body:1,altitude:30,azimuth:180,magnitude:-26.7,selected:0,azimuth_defined:1},
          {index:59,name:'Moon',body:'Moon',is_body:1,altitude:-20,azimuth:280,magnitude:-10,selected:0,azimuth_defined:1}],
  stars:[{index:0,name:'<test&star>',altitude:45,azimuth:90,magnitude:1,eligible:1,selected:1,azimuth_defined:1}]};
const svg=call('skySVG(fixture)');
assert(svg.includes('&lt;test&amp;star&gt;') && !svg.includes('<test&star>'));
assert(svg.includes('Sun') && !svg.includes('data-sky-star="59"'));
assert(call('skySVG(fixture,false)').includes('>Sun</text>'));
assert(!call('skySVG(fixture,false)').includes('&lt;test&amp;star&gt;</text>'));
assert(svg.includes('r="7"') && !svg.includes('r="22.355"'),'bright Sun is drawn at symbolic size');
call('skyResult=fixture; skySelected=58; drawSky()');
assert.equal($('sky_sight').disabled,false);
$('sky_sight').onclick();
assert.equal(context.transferred.body,'sun');assert.equal(context.transferred.star,'');
assert.equal(context.transferred.hs,0);assert.equal(context.transferred.drlon,-117);
assert.equal(context.shown,'sight');
call('skySelected=59; drawSky()');
assert.equal($('sky_sight').disabled,true,'below-horizon Moon cannot start a sight');
$('sky_sight').onclick();assert.equal(context.transferred.body,'sun');
$('sky_date').value='2026-06-21';$('sky_time').value='03:00:00';
$('sky_lat').value='N 34 00';$('sky_lon').value='W 117 00';
(async()=>{
  const first=call('refreshSky()'),second=call('refreshSky()');
  pending[1]({ok:true,json:async()=>context.fixture});await second;
  pending[0]({ok:true,json:async()=>({...context.fixture,date:'2000-01-01'})});await first;
  assert.equal(call('skyResult.date'),'2026-06-21');
  const invalid=call('refreshSky()');pending[2]({ok:true,json:async()=>({error:'Invalid date'})});await invalid;
  assert.equal(call('skyResult'),null);assert.equal($('sky_sight').disabled,true);
  assert.equal($('sky_chart').innerHTML,'');assert.equal($('sky_bodies').innerHTML,'');
  console.log('sky-ui: DR formats, chart orientation, body markers/handoff, escaping, stale responses and error clearing passed');
})().catch(e=>{console.error(e);process.exit(1);});
