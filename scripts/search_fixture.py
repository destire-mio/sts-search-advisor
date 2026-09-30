#!/usr/bin/env python3
import argparse,json,pathlib,subprocess,sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from fixtures import selected
p=argparse.ArgumentParser();p.add_argument('--reference',type=pathlib.Path,default=ROOT.parent/'sts-rl-agent-principles');p.add_argument('--server',type=pathlib.Path,default=ROOT/'build/engine/sts-search-server');p.add_argument('--index',type=int,default=0);p.add_argument('--budget',type=int,default=2000);a=p.parse_args()
source,index,view=list(selected(a.reference))[a.index]
request=dict(version=1,id='fixture-demo',op='search',snapshot_token='fixture-demo',simulations=a.budget,timeout_ms=5000,snapshot=view['game'])
with subprocess.Popen([str(a.server)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True) as process:
    process.stdin.write(json.dumps(request)+'\n');process.stdin.flush()
    # Keep stdin open until reply; EOF means cancellation in protocol v1.
    response=json.loads(process.stdout.readline());process.stdin.close();process.wait(timeout=3)
print(json.dumps({'fixture':source,'capture_index':index,'response':response},ensure_ascii=False,indent=2))
if not response['ok']:raise SystemExit(1)
