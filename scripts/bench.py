#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
import argparse, json, math, os, statistics, subprocess, sys, tempfile, time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / 'build'
VM = BUILD / '0xvm'

CASES = [
    {
        'name': 'conformance',
        'argv': [str(VM), '-f', str(BUILD / 'conformance-v2.rom')],
        'stdin': 'B-7\nhey\n',
        'expect_status': 'VM_HALTED',
    },
    {
        'name': 'statistics',
        'argv': [str(VM), '-f', str(ROOT / 'examples/statistics.asm')],
        'stdin': '5\n10\n20\n30\n40\n50\n1\n',
        'expect_status': 'VM_HALTED',
    },
]


def parse_info(text: str):
    # [INFO] Finished with status: VM_HALTED (total ticks=601, 0.000s, 8.31 MIPS)
    for line in text.splitlines():
        if 'Finished with status:' in line and 'total ticks=' in line:
            status = line.split('Finished with status:', 1)[1].split('(')[0].strip()
            ticks = int(line.split('total ticks=', 1)[1].split(',', 1)[0].split(')')[0])
            secs = None
            mips = None
            if ',' in line.split('total ticks=', 1)[1]:
                rest = line.split('total ticks=', 1)[1]
                parts = [p.strip() for p in rest.rstrip(')').split(',')]
                if len(parts) >= 2 and parts[1].endswith('s'):
                    try: secs = float(parts[1][:-1])
                    except ValueError: pass
                if len(parts) >= 3 and parts[2].endswith('MIPS'):
                    try: mips = float(parts[2].split()[0])
                    except ValueError: pass
            return {'status': status, 'ticks': ticks, 'secs': secs, 'mips': mips}
    raise RuntimeError('could not parse VM finish line')


def run_case(case, runs):
    samples = []
    for _ in range(runs):
        t0 = time.perf_counter()
        p = subprocess.run(case['argv'], input=case['stdin'], text=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        t1 = time.perf_counter()
        info_text = p.stderr + '\n' + p.stdout
        info = parse_info(info_text)
        if case.get('expect_status') and info['status'] != case['expect_status']:
            raise RuntimeError(f"{case['name']}: expected {case['expect_status']}, got {info['status']}\n{p.stderr}")
        if p.returncode != 0:
            raise RuntimeError(f"{case['name']}: exit {p.returncode}\n{p.stderr}")
        wall = t1 - t0
        mips = info['ticks'] / wall / 1e6 if wall > 0 else 0.0
        samples.append({'wall_s': wall, 'ticks': info['ticks'], 'mips': mips})
    walls = [s['wall_s'] for s in samples]
    mips = [s['mips'] for s in samples]
    return {
        'name': case['name'],
        'runs': runs,
        'ticks': samples[0]['ticks'],
        'wall_s_median': statistics.median(walls),
        'wall_s_min': min(walls),
        'mips_median': statistics.median(mips),
        'mips_max': max(mips),
        'samples': samples,
    }


def cmd_run(args):
    results = [run_case(c, args.runs) for c in CASES]
    out = {
        'schema': 1,
        'type': os.environ.get('type', ''),
        'runs': args.runs,
        'results': results,
    }
    if args.output:
        Path(args.output).write_text(json.dumps(out, indent=2) + '\n')
    print_report(out)
    return 0


def print_report(out):
    print(f"bench runs={out['runs']} type={out.get('type') or 'default'}")
    for r in out['results']:
        print(f"{r['name']:12s} ticks={r['ticks']:10d}  median={r['mips_median']:8.2f} MIPS  min_wall={r['wall_s_min']*1000:8.2f} ms")


def cmd_compare(args):
    base = json.loads(Path(args.baseline).read_text())
    cur = json.loads(Path(args.current).read_text())
    base_by = {r['name']: r for r in base['results']}
    bad = False
    print(f"compare baseline={args.baseline} current={args.current} threshold={args.threshold:.2%}")
    for r in cur['results']:
        b = base_by.get(r['name'])
        if not b:
            print(f"{r['name']:12s} missing baseline")
            continue
        delta = (r['mips_median'] - b['mips_median']) / b['mips_median'] if b['mips_median'] else 0.0
        flag = ''
        if delta < -args.threshold:
            flag = ' REGRESSION'
            bad = True
        elif delta > args.threshold:
            flag = ' IMPROVED'
        print(f"{r['name']:12s} base={b['mips_median']:8.2f}  cur={r['mips_median']:8.2f}  delta={delta*100:+7.2f}%{flag}")
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)
    run = sub.add_parser('run')
    run.add_argument('--runs', type=int, default=5)
    run.add_argument('--output')
    cmp = sub.add_parser('compare')
    cmp.add_argument('baseline')
    cmp.add_argument('current')
    cmp.add_argument('--threshold', type=float, default=0.10)
    args = ap.parse_args()
    if args.cmd == 'run': return cmd_run(args)
    if args.cmd == 'compare': return cmd_compare(args)

if __name__ == '__main__':
    raise SystemExit(main())
