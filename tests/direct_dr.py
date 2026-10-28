#!/usr/bin/env python3
"""Check real reduction/fix HTTP and CLI paths, isolating user data."""
import math
import json
import os
from pathlib import Path
import subprocess
import tempfile
import urllib.parse
from web_book import ROOT, api, server

with tempfile.TemporaryDirectory(prefix='navalm-direct-dr-') as temporary:
    work=Path(temporary); home=work/'home'; home.mkdir()
    proc,port=server(ROOT/'navalm',home,work)
    query=dict(body='sun',y=2026,mo=9,d=19,h=21,mi=38,s=5,hs=94+37.2/60,
               horizon='ah',limb='ll',temp=30,press=1013,
               drlat=34+2.5/60,drlon=-(117+21.9/60))
    def reduce(**changes):
        args=dict(query,**changes)
        return api(port,'/api/reduce?'+urllib.parse.urlencode(args))
    try:
        legacy=reduce(); direct=reduce(at_dr=1)
        assert legacy['ok']==direct['ok']==1,(legacy,direct)
        assert legacy['at_dr']==0 and direct['at_dr']==1
        assert abs(direct['ap_lat']-query['drlat'])<1e-6
        assert abs(direct['ap_lon']-query['drlon'])<1e-6
        assert abs(direct['lha_deg']-round(direct['lha_deg']))>.01
        assert legacy['ho']==direct['ho']
        phi=math.radians(query['drlat']); dec=math.radians(direct['dec'])
        lha=math.radians(direct['gha']+query['drlon'])
        expected=math.degrees(math.asin(math.sin(phi)*math.sin(dec)+math.cos(phi)*math.cos(dec)*math.cos(lha)))
        assert abs(direct['hc_min']/60-expected)<.051/60
        assert 'below the horizon' in reduce(at_dr=1,drlat=0,drlon=0)['error']
        assert 'error' in reduce(at_dr=2)
        fixture=json.loads((ROOT/'tests/fixtures/plotting.json').read_text())['cases']['stationary']
        args={'n':len(fixture['sights'])}
        for i,sight in enumerate(fixture['sights']):
            args.update({key+str(i):value for key,value in sight.items()})
        args['at_dr0']=1
        mixed=api(port,'/api/fix?'+urllib.parse.urlencode(args))
        assert mixed['ok']==1,mixed
        assert [s['at_dr'] for s in mixed['lops']]==[1,0,0],mixed
        assert abs(mixed['lops'][0]['ap_lat']-fixture['sights'][0]['drlat'])<1e-6
        assert abs(mixed['lat']-fixture['result']['lat'])*60<.15
        env=dict(os.environ,HOME=str(home),NAVALM_SAVE=str(home/'sights.save'),NAVALMRC=str(home/'defaults.rc'))
        base=[str(ROOT/'navalm'),'--ephemeris',str(ROOT/'ephem/de440s.nav')]
        def cli(*args):
            return subprocess.check_output(base+list(args),env=env,cwd=work,text=True)
        report=cli('sight','sun','2026','9','19','21','38','5','--hs','94','37.2','--ah',
                   '--temp','30','--press','1013','--dr','34','2.5','N','117','21.9','W','--at-dr','--save')
        assert 'Direct reduction at DR' in report and 'from DR' in report,report
        assert 'Direct reduction at DR' in cli('lop','show','1')
        cli('lop','edit','1','--ho229')
        assert 'HO 229' in cli('lop','show','1')
        assert '5.3' in cli('-v')
        print('direct-dr: HTTP calculation, errors, CLI save/reopen/method edit and 5.3 version passed')
    finally:
        proc.terminate();proc.wait(timeout=10)
