#!/usr/bin/env python3
"""Exercise the real planetarium HTTP endpoint with an isolated HOME."""
import datetime
import math
from pathlib import Path
import tempfile
import urllib.parse
from web_book import ROOT, api, server

with tempfile.TemporaryDirectory(prefix='navalm-sky-') as temporary:
    work=Path(temporary)
    home=work/'home'; home.mkdir()
    proc,port=server(ROOT/'navalm',home,work)
    def sky(**overrides):
        query=dict(y=2026,mo=6,d=21,h=0,mi=0,s=0,lat=34,lon=-117)
        query.update(overrides)
        return api(port,'/api/sky?'+urllib.parse.urlencode(query))
    try:
        for count in (3,4):
            data=sky(count=count)
            assert data['ok']==1 and data['engine']=='precise',data
            assert len(data['stars'])==data['catalogue_count']==58
            assert len(data['context_stars'])==712
            assert [s['index'] for s in data['context_stars']]==list(range(65,777))
            for star in data['context_stars']:
                assert star['is_context']==1 and star['constellation']
                assert -90<=star['altitude']<=90 and 0<=star['azimuth']<360
                assert 'selected' not in star and 'eligible' not in star
            assert any(f['name']=='Ursa Major' for f in data['constellations'])
            assert len(data['constellation_paths'])>100
            for path in data['constellation_paths']:
                assert len(path)>=4 and len(path)%2==0
                assert all(0<=h<=90 for h in path[::2])
                assert all(0<=z<360 for z in path[1::2])
            assert any(s['name']=='Sirius' and s['constellation']=='Canis Major' for s in data['stars'])
            assert len(data['bodies'])==7
            assert [s['name'] for s in data['bodies']]==['Sun','Moon','Mercury','Venus','Mars','Jupiter','Saturn']
            assert [s['index'] for s in data['bodies']]==list(range(58,65))
            for body in data['bodies']:
                assert body['is_body']==1 and not body['selected'] and not body['eligible']
                assert math.isfinite(body['magnitude']) and -90<=body['altitude']<=90
                assert 0<=body['azimuth']<360
            assert abs(data['bodies'][0]['altitude']-data['sun_altitude'])<1e-6
            assert data['selected']==count
            assert sum(s['selected'] for s in data['stars'])==count
            for star in data['stars']:
                assert math.isfinite(star['altitude']) and -90<=star['altitude']<=90
                assert 0<=star['azimuth']<360
                if star['selected']:
                    assert star['eligible'] and 15<=star['altitude']<=75 and star['magnitude']<=3
        for changes in (dict(y=2026,mo=2,d=29),dict(h=24),dict(s=60),dict(lat='nan'),
                        dict(lon=181),dict(lat='34x'),dict(count=5),dict(min_alt=80),
                        dict(max_mag='inf'),dict(event='noon'),dict(y=1599)):
            assert 'error' in sky(**changes),changes
        assert 'error' in api(port,'/api/sky')
        start=datetime.datetime(2026,6,21)
        for event in ('morning','evening'):
            data=sky(event=event)
            assert data['ok']==1,data
            assert abs(data['sun_altitude']+8)<0.001,data
            instant=datetime.datetime.fromisoformat(data['date']+'T'+data['time'])
            assert start<instant<start+datetime.timedelta(hours=48)
        assert 'error' in sky(lat=90,event='morning')
        data=sky(min_alt=89,max_alt=90,max_mag=-2)
        assert data['eligible']==data['selected']==0 and len(data['bodies'])==7,data
        print('sky-web: 58 stars, Sun/Moon/5 planets, filters, 3/4-star sets, UTC, twilight and invalid requests passed')
    finally:
        proc.terminate();proc.wait(timeout=10)
