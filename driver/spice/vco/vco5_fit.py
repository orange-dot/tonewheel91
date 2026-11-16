#!/usr/bin/env python3
"""V5: what one of the master's resets does to the slave, from the cases of
vco5.py. For every network, coupling value and master note it gathers the
events of all slave ratios (B's phase just before A's reset, and 12 us after
it with B's own run taken out) and reduces them:
  reset   the phase B loses, as a line in its phase before: lost = d0 + d1 * before,
          never more than it had
  thresh  the window: the latest phase that was left alone and the earliest
          that was reset
  vco5_fit.py            -> <out>/fits5.json and a table on stdout"""
import json, re, sys
import numpy as np
import paths

OUT = paths.out('vco5')
NAME = re.compile(r'(reset|thresh)-a(\d)-([0-9.e+-]+)-x([0-9.]+)\.json')
NOTE = {'2': 45, '4': 69, '6': 93}

def gather():
    groups = {}
    for f in sorted(OUT.glob('*.json')):
        m = NAME.fullmatch(f.name)
        if not m: continue
        r = json.loads(f.read_text())
        key = (m.group(1), float(m.group(3)), NOTE[m.group(2)])
        for e in r['events']:
            groups.setdefault(key, []).append(
                dict(before=e['before'], after=max(e['after'] - r['advance'], 0.0), own=e['own'],
                     t90=e['t90_us'], ratio=r['case']['ratio']))
    return groups

def reduce_reset(ev):
    b = np.array([e['before'] for e in ev]); a = np.array([e['after'] for e in ev])
    lost = b - a
    full = a < .02                                   # reset to the top whatever it had
    part = ~full & (b > .05)
    out = dict(events=len(ev), full=int(full.sum()), untouched=int((np.abs(lost) < .01).sum()))
    if part.sum() >= 3 and np.ptp(b[part]) > .2:
        d1, d0 = np.polyfit(b[part], lost[part], 1)
        out.update(d0=float(d0), d1=float(d1),
                   residual=float(np.abs(lost[part] - (d0 + d1 * b[part])).max()),
                   lost_min=float(lost[part].min()), lost_max=float(lost[part].max()))
    t = [e['t90'] for e in ev if e['t90'] is not None]
    if t: out['t90_us'] = float(np.median(t))
    return out

def reduce_thresh(ev):
    b = np.array([e['before'] for e in ev]); own = np.array([e['own'] for e in ev])
    a = np.array([e['after'] for e in ev])
    out = dict(events=len(ev), fired=int(own.sum()))
    if own.any(): out.update(earliest_fired=float(b[own].min()), left_after=float(np.median(a[own])))
    if (~own).any(): out.update(latest_left=float(b[~own].max()),
                                moved_when_left=float(np.abs(a[~own] - b[~own]).max()))
    t = [e['t90'] for e, o in zip(ev, own) if o and e['t90'] is not None]
    if t: out['t90_us'] = float(np.median(t))
    return out

def main():
    groups = gather()
    fits = []
    for (kind, value, note), ev in sorted(groups.items()):
        r = (reduce_reset if kind == 'reset' else reduce_thresh)(ev)
        fits.append(dict(kind=kind, value=value, note=note, **r))
    (OUT / 'fits5.json').write_text(json.dumps(fits, indent=1))
    print('| network | value | master | events | reading |\n|---|---:|---|---:|---|')
    for f in fits:
        if f['kind'] == 'reset':
            v = f"{f['value'] / 1e3:.0f} k"
            if 'd0' in f:
                text = (f"loses {f['lost_min']:.3f} to {f['lost_max']:.3f} of a turn: {f['d0']:.3f} + "
                        f"{f['d1']:.3f} x phase (within {f['residual']:.3f}); {f['full']} to the top, "
                        f"{f['untouched']} untouched")
            else:
                text = f"{f['full']} to the top, {f['untouched']} untouched"
        else:
            v = f"{f['value'] * 1e12:.1f} pF"
            text = f"fired {f['fired']}"
            if 'earliest_fired' in f: text += f", earliest at {f['earliest_fired']:.3f} (left at {f['left_after']:.3f})"
            if 'latest_left' in f: text += f"; left alone up to {f['latest_left']:.3f} (moved {f['moved_when_left']:.3f})"
        if 't90_us' in f: text += f"; 90 % in {f['t90_us']:.1f} us"
        print(f"| {f['kind']} | {v} | note {f['note']} | {f['events']} | {text} |")

if __name__ == '__main__':
    main()
