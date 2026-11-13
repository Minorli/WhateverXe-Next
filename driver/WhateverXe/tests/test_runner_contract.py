#!/usr/bin/env python3
"""Execute the real runner with colliding C++/Python stems and controlled exits.

Only the job commands are replaced. Planning, scheduling, log handling,
aggregation and process exit use the production runner in an isolated tree.
"""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

RUNNER = Path(__file__).resolve().parents[1] / 'tools/test.py'
if len(sys.argv) > 1:
    RUNNER = Path(sys.argv[1]).resolve()

FIXTURE = r'''
import ast
from pathlib import Path
import sys
runner, root, mode = map(str, sys.argv[1:])
root = Path(root)
(root/'tests').mkdir()
(root/'tests/tgl_start_order.cpp').touch()
tree = ast.parse(Path(runner).read_text())
boundary = next(i for i,n in enumerate(tree.body) if isinstance(n,ast.FunctionDef) and n.name=='check')
scope = {'__file__':str(root/'tools/test.py'), '__name__':'__main__'}
sys.argv = [scope['__file__']]
exec(compile(ast.Module(body=tree.body[:boundary],type_ignores=[]),runner,'exec'),scope)
planned = [j for j in scope['jobs'] if j[0].endswith('tgl_start_order')]
assert len(planned)==2, planned
# A failing C++ job precedes the successful Python job in production order.
# A dict keyed by the bare stem used to erase that first failure.
scope['jobs'] = [(name,[sys.executable,'-c',f'print("witness-{i}"); raise SystemExit({7 if i==0 else 0})'],None)
                 for i,(name,_,_) in enumerate(planned)]
if mode=='duplicate': scope['jobs'][1]=(scope['jobs'][0][0],*scope['jobs'][1][1:])
exec(compile(ast.Module(body=tree.body[boundary:],type_ignores=[]),runner,'exec'),scope)
'''

with tempfile.TemporaryDirectory(prefix='wex-runner-contract-') as directory:
    root = Path(directory)
    case = root/'distinct'
    case.mkdir()
    result = subprocess.run([sys.executable,'-c',FIXTURE,str(RUNNER),str(case),'distinct'],
                            text=True,capture_output=True)
    assert result.returncode==1, f'failure lost: rc={result.returncode}, {result.stdout} {result.stderr}'
    results = json.loads((case/'build/tests/results.json').read_text())
    assert len(results)==2 and sorted(results.values())==[0,7], results
    logs = list((case/'build/tests').glob('*.log'))
    assert len(logs)==2 and sorted(p.read_text().strip() for p in logs)==['witness-0','witness-1']
    case = root/'duplicate'
    case.mkdir()
    result = subprocess.run([sys.executable,'-c',FIXTURE,str(RUNNER),str(case),'duplicate'],
                            text=True,capture_output=True)
    assert result.returncode==2 and 'duplicate test job' in result.stderr, result
    assert not list((case/'build/tests').glob('*.log')), 'duplicate job executed'
print('PASS: same-stem failures and logs survive; duplicate job IDs rejected before execution')
