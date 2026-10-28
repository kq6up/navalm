/* A late status reply must not unlock controls or hide a newly started job. */
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');
const page=fs.readFileSync(path.join(__dirname,'../page.html'),'utf8');
const source=page.slice(page.indexOf('/* ---------- annual book ---------- */'),
                        page.indexOf('/* ---------- tabs and wiring ---------- */'));
const elements=new Map(), pending=[];
function $(id){
  if(!elements.has(id)) elements.set(id,{checked:true,value:'',reportValidity:()=>true});
  return elements.get(id);
}
const ctx=vm.createContext({$,URLSearchParams,Date,
  setTimeout:()=>1,clearTimeout:()=>{},
  fetch:(url,options)=>new Promise(resolve=>pending.push({url,options,resolve}))});
vm.runInContext(source,ctx);
function reply(request,data,ok=true){request.resolve({ok,json:async()=>data});}
(async()=>{
  $('book_format').value='latex'; $('book_formulas').checked=false;
  const old=ctx.refreshBook(), stale=pending.shift();
  const start=ctx.buildBook(), post=pending.shift();
  assert.equal(post.options.method,'POST');
  assert.equal(post.options.headers['X-Navalm-Book'],'1');
  const query=new URL('http://localhost'+post.url).searchParams;
  assert.equal(query.get('format'),'latex');
  assert.equal(query.get('reduction'),'ageton');
  await ctx.refreshBook();
  assert.equal(pending.length,0,'Tab refresh must wait for the start response');
  reply(stale,{state:'idle',available:1}); await old;
  assert.equal($('book_build').disabled,true,'Old idle status must not unlock generation');
  reply(post,{state:'running',available:1,percent:10,stage:'January',engine:'precise'});
  await start;
  assert.equal($('book_options').disabled,true);
  assert.equal($('book_status').textContent,'10% · January');
  const status=ctx.refreshBook();
  reply(pending.shift(),{state:'done',available:1,percent:100,id:'abc123',
    path:'/home/user/book.tex',filename:'book.tex',engine:'precise'});
  await status;
  assert.equal($('book_download').hidden,false);
  assert.equal($('book_download').href,'/book/download?id=abc123');
  assert.equal($('book_path').textContent,'/home/user/book.tex');
  assert.equal($('book_options').disabled,false);
  for(const [id,element] of elements) if(id.startsWith('book_')) element.checked=false;
  await ctx.buildBook();
  assert.equal(pending.length,0);
  assert.equal($('book_status').textContent,'Select at least one book section.');
  console.log('Book controls, late status ownership, section choices and completed-link checks passed.');
})().catch(e=>{console.error(e);process.exitCode=1;});
