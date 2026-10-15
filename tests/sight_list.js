/* Exercise overlapping reduction responses without a live server.
   This small DOM double checks row ownership, not browser layout. */
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');
const page=fs.readFileSync(path.join(__dirname,'../page.html'),'utf8');
const source=page.slice(page.indexOf('/* ---------- stored sights ---------- */'),page.indexOf('function renderMotion('));
const sights=Array.from({length:3},(_,i)=>({body:'sun',label:'sight '+i,sel:true,y:2026,mo:9,d:20,h:3,mi:i,s:0}));
function harness(){
  let saved=structuredClone(sights), rows=[], pending=[];
  const tb={set innerHTML(value){rows=[];},appendChild(row){rows.push(row);}};
  const ctx=vm.createContext({
    loadSights:()=>structuredClone(saved),saveSights:a=>saved=structuredClone(a),
    $:()=>tb,query:s=>s.label,
    fetch:()=>new Promise((resolve,reject)=>pending.push({resolve,reject})),
    document:{createElement:()=>({querySelector:()=>({})})},
    pad:(x,n)=>String(x).padStart(n,'0'),alt:String,num:String
  });
  vm.runInContext(source,ctx);
  return {render:()=>ctx.renderList(),rows:()=>rows,saved:()=>saved,
    set:a=>saved=structuredClone(a),
    take:()=>{assert.ok(pending.length,'expected an outstanding reduction');return pending.shift();},
    drain:async()=>{
      // Each list request makes at most three sequential reductions.
      for(let i=0;i<15;i++){
        pending.splice(0).forEach(p=>p.resolve({json:async()=>({})}));
        await flush();
      }
      assert.equal(pending.length,0);
    }};
}
async function flush(){for(let i=0;i<8;i++) await Promise.resolve();}
(async()=>{
  // The edit-save path used to launch two renders before either answered.
  const h=harness(), old=h.render(), latest=h.render();
  await h.drain(); await Promise.all([old,latest]);
  assert.equal(h.rows().length,3,'overlapping refreshes must not double the rows');
  assert.equal(h.saved().length,3,'rendering must not change stored records');

  // A previously started response arriving last must not reinsert stale data.
  const a=harness(), stale=a.render(), delayed=a.take();
  const changed=structuredClone(sights); changed[1].label='updated sight';
  a.set(changed); const current=a.render(); await a.drain(); await current;
  delayed.resolve({json:async()=>({})}); await a.drain(); await stale;
  assert.equal(a.rows().length,3);
  assert.ok(a.rows()[1].innerHTML.includes('updated sight'));

  // Empty/import/delete while a response is pending must remain empty.
  const b=harness(), beforeClear=b.render(), waiting=b.take();
  b.set([]); await b.render(); waiting.resolve({json:async()=>({})});
  await b.drain(); await beforeClear; assert.equal(b.rows().length,0);

  // Failed reduction requests still leave one editable row per saved sight.
  const c=harness(), failed=c.render(); c.take().reject(new Error('offline'));
  await c.drain(); await failed; assert.equal(c.rows().length,3);
  console.log('Sight-list overlap, late response, empty-list and failed-request checks passed.');
})().catch(e=>{console.error(e);process.exitCode=1;});
