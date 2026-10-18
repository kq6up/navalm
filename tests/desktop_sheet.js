/* Compare the FLTK/SVG vector port with the unchanged web plotting sheet.
   Run after check-desktop, which produces SVGs from the same synthetic sights. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const root = path.join(__dirname, '..');
const page = fs.readFileSync(path.join(root, 'page.html'), 'utf8');
const ctx = vm.createContext({});
vm.runInContext(page.slice(page.indexOf('function plotSheetGeometry('),
                          page.indexOf('async function doFix(')), ctx);
const cases = JSON.parse(fs.readFileSync(path.join(__dirname, 'fixtures/plotting.json'))).cases;
const attributes = tag => Object.fromEntries([...tag.matchAll(/([\w-]+)="([^"]*)"/g)]
                                             .map(m => [m[1], m[2]]));
const tags = (svg, name) => [...svg.matchAll(new RegExp(`<${name}\\b[^>]*>`, 'g'))]
                           .map(m => attributes(m[0]));
const points = text => text.trim().split(/[ ,]+/).map(Number);
// Web coordinates are rounded to a tenth of a pixel; the recorded HTTP
// fixture also rounds its inputs. The C++ scene retains full precision.
const near = (a, b) => assert.ok(Math.abs(a - b) < .15, `${a} != ${b}`);
const compare = (got, want, role) => {
    assert.equal(got.length, want.length, `${role} count`);
    got.forEach((a, i) => {
        const b = want[i];
        assert.equal(a.stroke, b.stroke, `${role} colour`);
        assert.equal(Number(a['stroke-width']), Number(b['stroke-width']), `${role} weight`);
        assert.equal(a['stroke-dasharray'], b['stroke-dasharray'], `${role} dash`);
        const p = points(a.points);
        const q = ['x1', 'y1', 'x2', 'y2'].map(k => Number(b[k]));
        p.forEach((v, j) => near(v, q[j]));
    });
};
for (const [name, {result}] of Object.entries(cases)) {
    const web = ctx.plotSheet(result);
    const base = path.join(root, '.build/desktop-test', name);
    const native = fs.readFileSync(base + '.svg', 'utf8');
    fs.writeFileSync(base + '-web.svg', web);
    assert.ok(!/NaN|Infinity/.test(native));
    assert.ok(native.includes('viewBox="0 0 760 772"'));
    const webLines = tags(web, 'line');
    const nativeLines = tags(native, 'polyline');
    const role = r => nativeLines.filter(p => p['data-role'] === r);
    for (const [r, width] of [['lop', 2.2], ['azimuth', .9], ['intercept', 3],
                             ['motion', 1.2], ['detail-lop', 1.8]]) {
        compare(role(r), webLines.filter(p => Number(p['stroke-width']) === width), r);
    }
    compare(role('compass-tick'), webLines.filter(p => p.stroke === '#4a5a66'), 'compass');
    compare([...role('parallel'), ...role('meridian')],
            webLines.filter(p => p.stroke === '#a8bcc9'), 'graticule');
    const arrows = tags(native, 'polygon').filter(p => p['data-role'] === 'intercept-arrow');
    const stems = role('intercept');
    const markers = tags(web, 'marker');
    assert.equal(arrows.length, stems.length);
    arrows.forEach((head, i) => {
        const marker = markers[i];
        assert.equal(marker.markerWidth, '5.5');
        assert.equal(marker.refX, '9');
        assert.equal(marker.refY, '5');
        const [ax, ay, tx, ty] = points(stems[i].points);
        const len = Math.hypot(tx - ax, ty - ay), dx = (tx - ax) / len, dy = (ty - ay) / len;
        const unit = Number(marker.markerWidth) / 10 * Number(stems[i]['stroke-width']);
        const expected = [tx - 9 * unit * dx + 5 * unit * dy,
                          ty - 9 * unit * dy - 5 * unit * dx,
                          tx + unit * dx, ty + unit * dy,
                          tx - 9 * unit * dx - 5 * unit * dy,
                          ty - 9 * unit * dy + 5 * unit * dx];
        points(head.points).forEach((v, j) => near(v, expected[j]));
    });
    const nativeAP = tags(native, 'polygon').filter(p => p['data-role'] === 'assumed-position');
    const webAP = tags(web, 'path').filter(p => p['stroke-width'] === '1.4');
    assert.equal(nativeAP.length, webAP.length);
    nativeAP.forEach((p, i) => {
        const q = webAP[i].d.match(/[-\d.]+/g).map(Number);
        points(p.points).forEach((v, j) => near(v, q[j]));
    });
    const nativeHats = tags(native, 'polygon').filter(p => /^(cocked-hat|detail-hat)$/.test(p['data-role']));
    const webHats = tags(web, 'polygon');
    assert.equal(nativeHats.length, webHats.length);
    nativeHats.forEach((p, i) => {
        const q = points(webHats[i].points);
        points(p.points).forEach((v, j) => near(v, q[j]));
    });
    const geo = JSON.parse(fs.readFileSync(base + '.geojson'));
    assert.ok(geo.features.every(f => !/detail|scale|frame/.test(f.properties.role || '')),
              'inset and page furniture must not be exported as chart geography');
    const geoLOPs = geo.features.filter(f => f.properties.role === 'lop');
    assert.equal(geoLOPs.length, result.lops.length);
    const cl = Math.cos(result.lops.reduce((sum, l) => sum + l.ap_lat, 0) / result.lops.length * Math.PI / 180);
    geoLOPs.forEach((f, i) => {
        const l = result.lops[i], z = l.zn * Math.PI / 180;
        for (const [lon, lat] of f.geometry.coordinates) {
            const residual = -((lon - result.lon) * 60 * cl * Math.sin(z) + (lat - result.lat) * 60 * Math.cos(z));
            assert.ok(Math.abs(residual - l.residual_nm) < .0002, 'exported LOP matches solver');
        }
    });
    console.log(`PASS ${name}: web/native LOPs, APs, arrows, grid, compass and cocked hat agree within 0.15 px; geographic residuals agree within 0.0002 NM`);
}
