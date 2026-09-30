#!/usr/bin/env python3
"""Create a local sidecar bundle, without writing Steam or any application dir."""
import argparse,pathlib,shutil,zipfile,hashlib,json
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--native',action='append',default=[],metavar='PLATFORM=PATH');a=p.parse_args()
dest=ROOT/'dist/SearchAdvisor';dest.mkdir(parents=True,exist_ok=True)
shutil.copy2(ROOT/'mod/target/SearchAdvisor.jar',dest/'SearchAdvisor.jar')
shutil.copy2(ROOT/'SearchAdvisor.properties.example',dest/'SearchAdvisor.properties.example')
shutil.copy2(ROOT/'LICENSE',dest/'LICENSE')
for name in ['README.md','DESIGN.md','PROTOCOL.md','WORKSHOP.md','THIRD_PARTY.md','NLOHMANN-LICENSE.txt','VALIDATION.md']:
    shutil.copy2(ROOT/name,dest/name)
for spec in a.native:
    platform,path=spec.split('=',1)
    if platform not in ('macos-arm64','macos-x64','windows-x64','linux-x64'):raise SystemExit('unsupported platform: '+platform)
    target=dest/'searchadvisor-engine'/platform/('sts-search-server.exe' if platform.startswith('windows') else 'sts-search-server')
    target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target);target.chmod(0o755)
manifest={str(f.relative_to(dest)):hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(dest.rglob('*')) if f.is_file() and f.name!='SHA256.json'}
(dest/'SHA256.json').write_text(json.dumps(manifest,indent=2)+'\n')
archive=ROOT/'dist/SearchAdvisor-prototype.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
    for f in sorted(dest.rglob('*')):
        if f.is_file():z.write(f,f.relative_to(dest.parent))
print(archive)
