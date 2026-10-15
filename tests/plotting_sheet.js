/* Geometry regression tests; live browser layout is checked separately. */
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');
const root=path.join(__dirname,'..');
const page=fs.readFileSync(path.join(root,'page.html'),'utf8');
const source=page.slice(page.indexOf('function plotSheetGeometry('),page.indexOf('async function doFix('));
const ctx=vm.createContext({});
vm.runInContext(source,ctx);
const fixtures=JSON.parse(fs.readFileSync(path.join(__dirname,'fixtures/plotting.json'),'utf8'));
const clone=x=>JSON.parse(JSON.stringify(x));
const near=(a,b,tol=1e-9)=>assert.ok(Math.abs(a-b)<tol,`${a} != ${b}`);
const render=r=>{
  ctx.result=r;
  const svg=vm.runInContext('plotSheet(result)',ctx,{timeout:1000});
  assert.ok(!/NaN|Infinity/.test(svg));
  assert.ok(svg.length<60000,'graticule must stay bounded');
  return svg;
};

for(const [name,{result}] of Object.entries(fixtures.cases)){
  const {L,gridLat}=ctx.plotSheetGeometry(result);
  near(gridLat,result.lops.reduce((s,l)=>s+l.ap_lat,0)/L.length);
  L.forEach((line,i)=>{
    // Signed distance to the plotted LOP must match the C solver's residual.
    near(-(line.u.e*line.t.e+line.u.n*line.t.n),result.lops[i].residual_nm,0.0002);
    near(line.p.e*line.u.e+line.p.n*line.u.n,0);
    near((line.t.e-line.ap.e)*line.u.e+(line.t.n-line.ap.n)*line.u.n,line.a);
    near(Math.hypot(line.ap.e-line.ap0.e,line.ap.n-line.ap0.n),line.adv);
  });
  const svg=render(result);
  assert.ok(svg.includes('COCKED HAT ×'));
  assert.ok(svg.includes('aria-label="Celestial fix plotting sheet"'));
  result.lops.forEach(l=>assert.ok(svg.includes(Math.abs(l.intercept_nm).toFixed(1)+"' "+(l.intercept_nm>=0?'T':'A'))));
  if(name==='running') assert.ok(svg.includes('stroke-dasharray="2 3"'));
  console.log(`${name}: plotted residuals agree with precise-engine fix within 0.0002 NM`);
}

const sample=fixtures.cases.stationary.result;
const two=clone(sample); two.lops.pop();
assert.ok(!render(two).includes('COCKED HAT'));
const parallel=clone(sample); parallel.lops.forEach(l=>l.zn=90);
assert.ok(!render(parallel).includes('COCKED HAT'));
const tiny=clone(sample); tiny.lops[0].advanced_nm=0.001; tiny.lops[0].advanced_brg=90;
const line=ctx.plotSheetGeometry(tiny).L[0];
near(line.ap.e-line.ap0.e,0.001);
near(line.ap.n-line.ap0.n,0);
const escaped=clone(sample); escaped.lops[0].name='<test & star>';
assert.ok(render(escaped).includes('&lt;test &amp; star&gt;'));
const polar=clone(sample); polar.lat=89.9999; polar.lops.forEach(l=>l.ap_lat=89.9999);
assert.ok(render(polar).startsWith('<svg'));
polar.lat=90; polar.lops.forEach(l=>l.ap_lat=90);
assert.ok(render(polar).includes('away from the poles'));
const invalid=clone(sample); invalid.lops[0].advanced_nm=NaN;
assert.equal(ctx.plotSheetGeometry(invalid),null);
assert.equal(ctx.plotSheet(null),'');
assert.equal(ctx.plotSheet({lops:[]}),'');
console.log('Two-line, parallel, tiny advance, escaping, invalid and near-pole cases passed.');
