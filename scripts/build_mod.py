#!/usr/bin/env python3
"""Build in this workspace; never install or copy game dependency jars."""
import argparse, os, pathlib, shutil, subprocess, zipfile
ROOT = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--javac', action='store_true', help='offline fallback, no Maven needed')
p.add_argument('--properties', type=pathlib.Path, default=ROOT/'mod/local.properties')
a = p.parse_args()
props = dict(line.strip().split('=', 1) for line in a.properties.read_text().splitlines()
             if line.strip() and not line.lstrip().startswith('#'))
for key in ('sts.jar', 'mts.jar', 'basemod.jar'):
    path = pathlib.Path(props[key])
    if not path.is_absolute() or not path.is_file(): raise SystemExit(f'{key}: missing absolute jar path')
if not a.javac:
    subprocess.run(['mvn', '-f', str(ROOT/'mod/pom.xml'),
                    *[f'-D{k}={v}' for k,v in props.items()], 'package'], check=True)
else:
    home = pathlib.Path(os.environ['JAVA_HOME']) if 'JAVA_HOME' in os.environ else None
    compiler = str(home/'bin/javac') if home else shutil.which('javac')
    dest = ROOT/'mod/target/classes'
    # Limit cleanup to our generated classes.
    if dest.exists(): shutil.rmtree(dest)
    dest.mkdir(parents=True)
    cp = os.pathsep.join(props[k] for k in ('sts.jar', 'mts.jar', 'basemod.jar'))
    sources = sorted((ROOT/'mod/src/main/java').rglob('*.java'))
    version = subprocess.run([compiler, '-version'], capture_output=True, text=True, check=True)
    flags = ['-source','8','-target','8'] if '1.8.' in version.stdout+version.stderr else ['--release','8']
    subprocess.run([compiler, *flags, '-encoding', 'UTF-8', '-cp', cp, '-d', str(dest), *map(str,sources)],check=True)
    for src in (ROOT/'mod/src/main/resources').rglob('*'):
        if src.is_file():
            target=dest/src.relative_to(ROOT/'mod/src/main/resources'); target.parent.mkdir(parents=True,exist_ok=True); shutil.copyfile(src,target)
    with zipfile.ZipFile(ROOT/'mod/target/SearchAdvisor.jar','w',zipfile.ZIP_DEFLATED) as jar:
        for src in sorted(dest.rglob('*')):
            if src.is_file(): jar.write(src,src.relative_to(dest))
    print(ROOT/'mod/target/SearchAdvisor.jar')
