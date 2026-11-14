#!/usr/bin/env python3
"""B1 bench generator: the B1-THT DUT lines of b1-bench-cv-2.cir, unchanged,
with a chosen CV source, temperature and transient. Usage: b1_gen.py OUT.cir key=value ...
  cv=2 | pwl="0 1 0.3 1 0.3001 2 ..."   temp=25   tstop=1.0 tstart=0.94
  grid=520.8333333n  maxstep=0.5u  vecs="v(TRI) v(TBUF) ..."  binary=0|1
  extra="lines"  (netlist lines added after the DUT, '|' separated; the shapers' bench)
  ct=22n  (the timing capacitor C3: a range switch, V2b)"""
import sys, pathlib
from paths import SRC as here
out = pathlib.Path(sys.argv[1])
a = dict(cv='2', temp='25', tstop='1.0', tstart='0.94', grid='520.8333333n', maxstep='0.5u',
         vecs='v(TRI) v(TBUF) v(TRI_OUT) v(PWM_RAW) v(PULSE_OUT) v(SQUARE) v(VCC)', binary='0', pwl='',
         extra='', ct='')
a.update(kv.split('=', 1) for kv in sys.argv[2:])
src = (here / 'b1-bench-cv-2.cir').read_text().splitlines()
dut = []
for line in src:
    if line.startswith('.include'):
        continue
    if line.startswith('.options'):
        break
    dut.append(line)
lines = [dut[0]] + [f'.include "{here}/models/{m}"' for m in ('bc547c.lib', 'bc557c.lib', 'support.lib')]
for line in dut[1:]:
    if line.startswith('.temp'):
        line = f".temp {a['temp']}"
    elif line.startswith('C3 TRI 0') and a['ct']:
        line = f"C3 TRI 0 {a['ct']}"
    elif line.startswith('VCV '):
        line = f"VCV CV 0 PWL({a['pwl']})" if a['pwl'] else f"VCV CV 0 {a['cv']}"
    lines.append(line)
lines += [x for x in a['extra'].split('|') if x]
lines += ['.options reltol=1e-5 vntol=1e-8 abstol=1e-12 method=gear', '.control',
          'set wr_singlescale', 'set wr_vecnames',
          f"save {a['vecs']}",
          f"tran {a['grid']} {a['tstop']} {a['tstart']} {a['maxstep']}",
          f"linearize {a['vecs']}"]
if a['binary'] == '1':
    lines += ['set filetype=binary', f"write trace.raw {a['vecs']}"]
else:
    lines += ['set numdgt=12', f"wrdata trace.tsv {a['vecs']}"]
lines += ['rusage time', 'quit', '.endc', '.end']
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text('\n'.join(lines) + '\n')
