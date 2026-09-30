"""Read original fixtures in-place. No reseeding, patching or filling missing fields."""
import gzip, json

def load(root,relative):
    with gzip.open(root/relative,'rt',encoding='utf-8') as stream: return json.load(stream)

def views(value):
    if isinstance(value,dict):
        if isinstance(value.get('game'),dict) and 'combat_state' in value['game']:
            yield value
        else:
            if 'before' in value: yield from views(value['before'])
            for key,child in value.items():
                if key!='before': yield from views(child)
    elif isinstance(value,list):
        for child in value: yield from views(child)

def complete(v):
    g=v['game'];c=g['combat_state']
    return g.get('screen_type')=='NONE' and 'end_turn_shuffle' in c and 'is_bloodied' in c and c['player']['current_hp']>0 and any(m['current_hp']>0 for m in c['monsters'])

SOURCES=[
 'sim_patch/parity/tests/fixtures/exhaust-cost-controls-original.json.gz',
 'sim_patch/parity/tests/fixtures/free-ethereal-original.json.gz',
 'sim_patch/parity/tests/fixtures/relic-counters-original.json.gz',
 'sim_patch/parity/tests/fixtures/save-entry-original.json.gz',
]
def selected(root):
    for path in SOURCES:
        candidates=[v for v in views(load(root,path)) if complete(v)]
        # Fixed first three captures per file, independent of search outcome.
        if len(candidates)<3: raise AssertionError('fixture coverage changed: '+path)
        for index,v in enumerate(candidates[:3]): yield path,index,v
