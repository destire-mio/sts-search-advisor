#!/usr/bin/env python3
import argparse,copy,json,pathlib,queue,subprocess,sys,threading,time,unittest,hashlib
from fixtures import load,views,selected,SOURCES
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--server',type=pathlib.Path,default=ROOT/'build/engine/sts-search-server');p.add_argument('--reference',type=pathlib.Path,default=ROOT.parent/'sts-rl-agent-principles');p.add_argument('--evidence',type=pathlib.Path,default=ROOT/'build/evidence');args,extra=p.parse_known_args()
EVIDENCE=args.evidence;EVIDENCE.mkdir(parents=True,exist_ok=True)

class Server:
    def __init__(self):
        self.stderr=open(EVIDENCE/'test-server-stderr.log','a')
        self.process=subprocess.Popen([str(args.server)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.stderr,text=True,bufsize=1)
        self.lines=queue.Queue();self.count=0
        def read():
            for line in self.process.stdout:self.lines.put(line)
            self.lines.put(None)
        self.thread=threading.Thread(target=read,daemon=True);self.thread.start()
    def send(self,request): self.process.stdin.write(json.dumps(request,ensure_ascii=False)+'\n');self.process.stdin.flush()
    def receive(self,timeout=12):
        line=self.lines.get(timeout=timeout)
        if line is None: raise AssertionError('engine exited, code '+str(self.process.poll()))
        return json.loads(line)
    def request(self,op='search',snapshot=None,**kwargs):
        self.count+=1;r=dict(version=1,id='request-'+str(self.count),op=op,snapshot_token='token-'+str(self.count),simulations=64,timeout_ms=10000)
        if snapshot is not None:r['snapshot']=snapshot
        r.update(kwargs);self.send(r);return self.receive()
    def close(self):
        self.process.stdin.close()
        try:self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:self.process.kill();self.process.wait()
        self.process.stdout.close();self.thread.join(1);self.stderr.close()

def original_command(a):
    if a['type']=='end_turn':return 'end'
    if a['type']=='play':return 'play '+str(a['hand_index']+1)+((' '+str(a['target_index'])) if a['target_index']>=0 else '')
    if a['type']=='potion':return 'potion '+('discard' if a['discard'] else 'use')+' '+str(a['potion_index'])+((' '+str(a['target_index'])) if a['target_index']>=0 else '')
    raise AssertionError('unexpected first action')

class ProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls): cls.fixtures=list(selected(args.reference));cls.game=cls.fixtures[0][2]['game'];cls.records=[]
    def setUp(self):self.s=Server()
    def tearDown(self):self.s.close()
    def test_ping_and_utf8(self):
        r=self.s.request(op='ping',id='中文请求');self.assertTrue(r['ok']);self.assertEqual(r['id'],'中文请求')
    def test_framing_recovers_after_malformed_json(self):
        self.s.process.stdin.write('{broken\n');self.s.process.stdin.flush();self.assertEqual(self.s.receive()['error']['code'],'INVALID_JSON');self.assertTrue(self.s.request('ping')['ok'])
    def test_version_and_duplicate_ids(self):
        self.assertEqual(self.s.request('ping',version=2)['error']['code'],'VERSION_MISMATCH')
        self.assertTrue(self.s.request('ping',id='same')['ok']);self.assertEqual(self.s.request('ping',id='same')['error']['code'],'DUPLICATE_ID')
    def test_budget_validation(self):
        for budget in (0,-1,100001,1.5):
            with self.subTest(budget=budget):self.assertEqual(self.s.request(snapshot=self.game,simulations=budget)['error']['code'],'BAD_REQUEST')
    def test_missing_rng_fails_without_reseeding(self):
        g=copy.deepcopy(self.game);del g['combat_state']['rngs']['ai']
        self.assertEqual(self.s.request(snapshot=g)['error']['code'],'INVALID_SNAPSHOT')
        self.assertTrue(self.s.request('ping')['ok'])
    def test_old_alignment_capture_rejected(self):
        old=load(args.reference,'sim_patch/alignment/tests/fixtures/original-repair-cards.json.gz')[0]['before']['game']
        self.assertNotIn('end_turn_shuffle',old['combat_state'])
        self.assertEqual(self.s.request(snapshot=old)['error']['code'],'INVALID_SNAPSHOT')
    def test_duplicate_card_identity_and_unknown_content(self):
        g=copy.deepcopy(self.game);g['combat_state']['hand'][1]['uuid']=g['combat_state']['hand'][0]['uuid']
        self.assertEqual(self.s.request(snapshot=g)['error']['code'],'UNSUPPORTED_STATE')
        g=copy.deepcopy(self.game);g['combat_state']['hand'][0]['id']='Unknown:ModCard'
        self.assertEqual(self.s.request(snapshot=g)['error']['code'],'UNSUPPORTED_STATE')
    def test_selection_boundary_rejected(self):
        g=copy.deepcopy(self.game);g['screen_type']='HAND_SELECT'
        self.assertEqual(self.s.request(snapshot=g)['error']['code'],'UNSUPPORTED_STATE')
    def test_cancel_and_busy_preserve_other_request(self):
        self.s.send(dict(version=1,id='long',op='search',snapshot_token='busy',snapshot=self.game,simulations=100000,timeout_ms=30000))
        self.s.send(dict(version=1,id='wrong-cancel',op='cancel',request_id='someone-else'))
        self.assertFalse(self.s.receive()['accepted'])
        self.s.send(dict(version=1,id='busy',op='search',snapshot_token='busy2',snapshot=self.game,simulations=64,timeout_ms=30000))
        self.assertEqual(self.s.receive()['error']['code'],'BUSY')
        start=time.monotonic();self.s.send(dict(version=1,id='stop',op='cancel',request_id='long'))
        replies=[self.s.receive(),self.s.receive()];byid={r['id']:r for r in replies}
        self.assertTrue(byid['stop']['accepted']);self.assertEqual(byid['long']['error']['code'],'CANCELLED');self.assertLess(time.monotonic()-start,2)
        self.assertTrue(self.s.request(snapshot=self.game)['ok'])
    def test_timeout_has_no_partial_plan(self):
        r=self.s.request(snapshot=self.game,simulations=100000,timeout_ms=1)
        self.assertEqual(r['error']['code'],'TIMEOUT');self.assertNotIn('actions',r)
        self.assertTrue(self.s.request('ping')['ok'])
    def test_repeated_state_is_deterministic_and_one_search(self):
        first=self.s.request(snapshot=self.game);second=self.s.request(snapshot=self.game)
        self.assertTrue(first['ok'],first);self.assertTrue(second['ok'],second)
        self.assertEqual(first['actions'],second['actions']);self.assertEqual(first['search_calls'],1);self.assertEqual(first['simulations'],64)
    def test_original_fixtures_import_and_legal_plan(self):
        vectors=[]
        for path,index,view in self.fixtures:
            with self.subTest(source=path,index=index):
                g=view['game'];start=time.monotonic();r=self.s.request(snapshot=g)
                self.assertTrue(r['ok'],r);self.assertTrue(r['actions']);self.assertEqual(r['search_calls'],1);self.assertEqual(r['simulations'],64)
                self.assertTrue(r['complete_turn'],r.get('stop_reason'))
                # Independent original-Java legal-action oracle for the root.
                self.assertTrue(view['parity']['legal_complete']);self.assertIn(original_command(r['actions'][0]),view['parity']['legal_actions'])
                # Fresh import, sequential validators: no state repair during replay.
                replay=self.s.request('validate',snapshot=g,actions=[a['bits'] for a in r['actions']])
                self.assertTrue(replay['ok'],replay);self.assertEqual(replay['actions'],r['actions']);self.assertEqual(replay['search_calls'],0)
                vectors.append(dict(snapshot=g,response=r))
                self.records.append(dict(source=path,index=index,simulations=r['simulations'],actions=len(r['actions']),elapsed_ms=round((time.monotonic()-start)*1000,2)))
        (EVIDENCE/'guard-vectors.json').write_text(json.dumps(vectors,ensure_ascii=False))
    def test_illegal_sequence_rejected(self):
        for actions in ([65535],[7<<29],[(4<<29),0],[-1]):
            with self.subTest(actions=actions):
                r=self.s.request('validate',snapshot=self.game,actions=actions);self.assertFalse(r['ok'])
    @classmethod
    def tearDownClass(cls):
        (EVIDENCE/'fixture-results.json').write_text(json.dumps({'records':cls.records,'source_sha256':{p:hashlib.sha256((args.reference/p).read_bytes()).hexdigest() for p in SOURCES}},indent=2)+'\n')

if __name__=='__main__':unittest.main(argv=[sys.argv[0],*extra],verbosity=2)
