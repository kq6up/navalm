const fs=require('fs'),vm=require('vm'),assert=require('assert');
const page=fs.readFileSync('page.html','utf8'),code=page.split('<script>')[1].split('</script>')[0];
let saved={},writes=0,nodes={},requests=[];
const $=id=>nodes[id]??=({_value:'',get value(){return this._value},set value(v){this._value=String(v)},checked:false,disabled:false,textContent:'',innerHTML:'',style:{},dataset:{},
 classList:{toggle(){},add(){},remove(){},contains(){return false}},addEventListener(){},reportValidity(){return true},focus(){}});
$('sightsettings').open=false;$('body').value=$('a_body').value='sun';$('a_star').value='';
for(const id of ['sky_min','sky_max','sky_mag','sky_count'])$(id).value=({sky_min:15,sky_max:75,sky_mag:3,sky_count:3})[id];
const document={getElementById:$,querySelector:()=>({classList:{toggle(){}}}),querySelectorAll:()=>[],addEventListener(){}};
const ctx=vm.createContext({document,localStorage:{getItem:k=>saved[k]??null,setItem:(k,v)=>{saved[k]=v;writes++;}},
 console,URLSearchParams,Date,Math,Number,String,Error,setTimeout:()=>0,clearTimeout(){},alert(){},confirm:()=>true,
 fetch:async url=>{requests.push(url);if(url==='/api/version')return {json:async()=>({version:'5.5.0'})};
 if(url.startsWith('/api/almanac')){const q=new URLSearchParams(url.split('?')[1]);return{json:async()=>({name:'Sun',body:q.get('body'),y:+q.get('y'),mo:+q.get('mo'),d:+q.get('d'),h:+q.get('h'),mi:+q.get('mi'),s:+q.get('s'),gha:12,dec:1,ra:2})};}
 if(url.startsWith('/api/paper'))return {json:async()=>({drlat:0,drlon:0,rows:[],delta_t:70})};
 return {json:async()=>({})};}});
vm.runInContext(code,ctx);
const run=x=>vm.runInContext(x,ctx);
const flush=async()=>{for(let i=0;i<12;i++)await Promise.resolve();};
(async()=>{
 await flush();
 assert.equal($('s_horizon').value,'sea');assert.equal($('f_horizon').value,'sea');
 // Persist explicit zero latitude/longitude plus non-default settings and motion.
 for(const [id,value] of Object.entries({s_drlat:'0 N',s_drlon:'0 E',s_he:'10',s_heunit:'ft',s_horizon:'short',s_short:'.5',s_limb:'ul',s_ie:'1.2',s_iearc:'on',s_temp:'0',s_press:'1015',s_course:'270',s_speed:'5',s_set:'90',s_drift:'1.5'}))$(id).value=value;
 $('s_at_dr').checked=true;run('saveSetup()');assert.equal(writes,1,$('setupmsg').textContent);
 const st=run('loadSetup()');assert.equal(st.drlat,0);assert.equal(st.drlon,0);assert.equal(st.temp,0);assert.equal(st.short_horizon,.5);assert.equal(st.at_dr,1);assert.equal(st.drift,1.5);
 run('fillSight(null); almanacUseDR(); skyUseDR(); renderActiveDR()');
 assert($('f_drlat').value.includes('00'));assert($('a_lat').value.includes('00'));assert($('sky_lat').value.includes('00'));
 assert.equal($('f_horizon').value,'short');assert.equal($('f_at_dr').checked,true);assert.equal($('f_drift').value,'1.5');assert(!$('activedr').innerHTML.includes('from Setup'));
 // Planning-page edits remain independent when defaults are saved.
 $('a_lat').value='12 30 S';$('a_lon').value='45 15 W';$('sky_lat').value='20 N';run('saveSetup()');assert.equal($('a_lat').value,'12 30 S');assert.equal($('sky_lat').value,'20 N');
 $('a_date').value='2026-09-19';$('a_time').value='21:38:05.125';
 await run('almanacToSight()');await flush();
 assert.equal($('hsd').value,'');assert.equal($('hsm').value,'');assert.equal($('time').value,'21:38:05.125');
 assert.equal(run('formSight().drlat'),-12.5);assert.equal(run('formSight().drlon'),-45.25);
 assert.equal(run('formSight().at_dr'),1);assert.equal(run('formSight().short_horizon'),.5);assert.equal(writes,2);
 const before=$('time').value;$('a_body').value='aries';await run('almanacToSight()');assert.equal($('time').value,before);assert($('almanacout').textContent.includes('reference point'));
 $('s_short').value='5';run('saveSetup()');assert.equal(writes,2);assert($('setupmsg').textContent.includes('inside the visible horizon'));
 $('f_short').value='';assert.equal(run('formSight().short_horizon'),-1);
 console.log('PASS web defaults and almanac handoff: sea initial state, zero DR, settings/motion persistence, page independence, fractional UTC, blank Hs, no autosave, Aries and short-horizon validation');
})().catch(e=>{console.error(e);process.exit(1)});
