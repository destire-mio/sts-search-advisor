#!/usr/bin/env python3
import os,pathlib,subprocess,sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
props=dict(l.split('=',1) for l in (ROOT/'mod/local.properties').read_text().splitlines() if l and not l.startswith('#'))
java_home=pathlib.Path(os.environ['JAVA_HOME'])
cp=os.pathsep.join([str(ROOT/'mod/target/classes'),*(props[k] for k in ('sts.jar','mts.jar','basemod.jar'))])
classes=ROOT/'build/java-tests';classes.mkdir(parents=True,exist_ok=True)
version=subprocess.run([str(java_home/'bin/javac'),'-version'],capture_output=True,text=True,check=True)
flags=['-source','8','-target','8'] if '1.8.' in version.stdout+version.stderr else ['--release','8']
subprocess.run([str(java_home/'bin/javac'),*flags,'-encoding','UTF-8','-cp',cp,'-d',str(classes),str(ROOT/'tests/ProtocolTest.java')],check=True)
fake=ROOT/'build/unresponsive-engine'
fake.write_text('#!'+sys.executable+'\nimport sys,time\nsys.stdin.readline()\ntime.sleep(20)\n');fake.chmod(0o755)
subprocess.run([str(java_home/'bin/java'),'-cp',str(classes)+os.pathsep+cp,'ProtocolTest',str(ROOT/'build/evidence/guard-vectors.json'),str(ROOT/'build/engine/sts-search-server'),str(fake)],check=True)
