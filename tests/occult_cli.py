#!/usr/bin/env python3
"""Exercise actual CLI dispatch and reject malformed requests politely."""
import subprocess,re
from datetime import datetime,timedelta
from pathlib import Path
root=Path(__file__).resolve().parent.parent
exe=str(root/'navalm')
def run(*args,ok=True):
    p=subprocess.run([exe,*args],cwd=root,text=True,capture_output=True)
    assert (p.returncode==0)==ok,(args,p.returncode,p.stdout,p.stderr)
    return p.stdout+p.stderr
assert 'occultations --date' in run(ok=False)
assert 'recover' in run('occultation','--help')
plan=['occultations','--date','2026-03-27','--lat','39.8','--lon','-105','--days','1','--magnitude','4']
s=run(*plan);assert 'Gem Kappa-77' in s
utc=re.search(r'D (\S+)Z',s).group(1)
watch=(datetime.fromisoformat(utc)-timedelta(seconds=60)+timedelta(seconds=.25)).isoformat(timespec='milliseconds')
r=run('occultation','--star','HD 62345','--near','2026-03-27T04:20:00Z','--contact','D','--watch',watch,'--lat','39.8','--lon','-105','--reaction','.25')
assert 'UTC minus watch = +60.000 seconds' in r,r
for args in [plan+['--days','nan'],plan+['--date','2026-02-30'],plan+['--sort','bogus'],plan+['--filter','bogus'],plan+['--lat','91'],['--engine','classic',*plan],['occultation','--star','fictional']]:run(*args,ok=False)
assert '0 events' in run(*plan,'--altitude','90')
print('Occultation CLI: help, independent event, recovered +60 s, bad dates/numbers/options and no-event handling passed.')
