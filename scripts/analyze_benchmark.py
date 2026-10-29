#!/usr/bin/env python3
"""Validate C benchmark CSV, summarize four cells, export presentation figures.
Error bars show sample standard deviation (ddof=1), not confidence intervals.
"""
import argparse
import csv
import json
import statistics as st
from collections import defaultdict
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    rows = list(csv.DictReader(args.input.open(newline='')))
    if not rows:
        raise ValueError('empty benchmark: NOT_RUN')
    groups = defaultdict(list)
    seen = set()
    for row in rows:
        if row['status'] != 'PASS':
            raise ValueError(f"failed trial: run {row['run']}")
        ni, competition = int(row['target_nice']), int(row['competition'])
        if (ni, competition) not in {(0, 0), (10, 0), (0, 1), (10, 1)}:
            raise ValueError('invalid condition')
        key = (int(row['round']), ni, competition)
        if key in seen:
            raise ValueError('duplicate condition within round')
        seen.add(key)
        if int(row['actual_nice']) != ni or int(row['actual_cpu']) != int(row['cpu']):
            raise ValueError('nice/affinity mismatch')
        if int(row['target_wait_status']) or int(row['target_error']) or int(row['policy']):
            raise ValueError('target failed or unexpected policy')
        elapsed, cpu = float(row['elapsed_s']), float(row['cpu_s'])
        if elapsed <= 0 or cpu <= 0 or cpu > elapsed + 0.002:
            raise ValueError('invalid clock values')
        if abs(float(row['end_mono']) - float(row['start_mono']) - elapsed) > 2e-7:
            raise ValueError('elapsed/lifecycle mismatch')
        if competition:
            if (int(row['competitor_wait_status']) or int(row['competitor_error'])
                    or int(row['competitor_nice']) != 0
                    or int(row['competitor_cpu']) != int(row['cpu'])):
                raise ValueError('competitor failed/incorrect settings')
            if (float(row['competitor_start_mono']) >= float(row['end_mono'])
                    or float(row['competitor_end_mono']) < float(row['end_mono'])
                    or int(row['competitor_iterations']) <= 0):
                raise ValueError('competitor did not span target completion')
        groups[(ni, competition)].append(row)
    nrounds = len({int(r['round']) for r in rows})
    if len(groups) != 4 or any(len(v) != nrounds for v in groups.values()):
        raise ValueError('incomplete four-condition design')
    for field in ['seed', 'cpu', 'kernel', 'arch', 'iterations', 'checksum']:
        if len({r[field] for r in rows}) != 1:
            raise ValueError(f'uncontrolled variable: {field}')
    args.output.mkdir(parents=True, exist_ok=True)
    summaries = []
    for (ni, competition), data in sorted(groups.items(), key=lambda x: (x[0][1], x[0][0])):
        result = {'target_nice': ni, 'competition': competition, 'n': len(data)}
        for field in ['elapsed_s', 'cpu_s']:
            values = [float(r[field]) for r in data]
            for name, func in [('mean', st.mean), ('median', st.median),
                               ('std', lambda v: st.stdev(v) if len(v) > 1 else 0),
                               ('min', min), ('max', max)]:
                result[f'{field}_{name}'] = func(values)
        result['mean_cpu_fraction'] = st.mean(float(r['cpu_s']) / float(r['elapsed_s']) for r in data)
        summaries.append(result)
    with (args.output / 'summary.csv').open('w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=list(summaries[0]))
        writer.writeheader()
        writer.writerows(summaries)
    payload = {'input': args.input.as_posix(), 'trials': len(rows), 'rounds_per_condition': nrounds,
               'error_bar': 'sample standard deviation, ddof=1', 'hypothesis_test': 'not performed',
               'validation': 'PASS; 4 complete cells, actual affinity/nice, checksums and competitor overlap',
               'conditions': summaries}
    (args.output / 'summary.json').write_text(json.dumps(payload, ensure_ascii=False, indent=2) + '\n')
    plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.size': 12,
                         'axes.spines.top': False, 'axes.spines.right': False})
    colors = {0: '#245B89', 10: '#D76D45'}
    fig, axes = plt.subplots(1, 2, figsize=(13.3, 6.1), constrained_layout=True)
    for ax, field, title in zip(axes, ['elapsed_s', 'cpu_s'], ['Target elapsed time', 'Target CPU time']):
        for ni, offset in [(0, -0.16), (10, 0.16)]:
            cells = [next(s for s in summaries if s['target_nice'] == ni and s['competition'] == c) for c in [0, 1]]
            positions = [c + offset for c in [0, 1]]
            means = [s[f'{field}_mean'] for s in cells]
            ax.bar(positions, means, width=0.29, color=colors[ni], label=f'nice={ni}',
                   yerr=[s[f'{field}_std'] for s in cells], capsize=5, alpha=0.9)
            for x, cell, mean in zip(positions, cells, means):
                values = [float(r[field]) for r in groups[(ni, cell['competition'])]]
                jitter = [(i - (len(values) - 1) / 2) * 0.012 for i in range(len(values))]
                ax.scatter([x+j for j in jitter], values, s=15, color='#152C3A', alpha=0.55, zorder=4)
                ax.annotate(f'{mean:.3f}', (x, mean+cell[f'{field}_std']), xytext=(0, 6),
                            textcoords='offset points', ha='center', fontsize=11)
        ax.set_xticks([0, 1], ['No added competitor', 'Same-CPU competitor'])
        ax.set_title(title, loc='left', weight='bold', pad=18)
        ax.set_ylabel('Seconds'); ax.grid(axis='y', alpha=0.15); ax.set_axisbelow(True)
        ax.set_ylim(0, ax.get_ylim()[1]*1.18)
    axes[0].legend(frameon=False)
    fig.suptitle(f'Real Linux nice benchmark: 2 x 2 conditions, n={nrounds} each', weight='bold', fontsize=18)
    fig.supxlabel(f"CPU {rows[0]['cpu']} | {int(rows[0]['iterations']):,} fixed iterations | dots: trials | bars: mean +/- sample SD", fontsize=11)
    fig.savefig(args.output / 'nice_comparison.png', dpi=180)
    fig.savefig(args.output / 'nice_comparison.svg')
    plt.close(fig)
    fig, ax = plt.subplots(figsize=(11, 5), constrained_layout=True)
    for (ni, c), data in sorted(groups.items()):
        data = sorted(data, key=lambda r: int(r['round']))
        ax.plot([int(r['round']) for r in data], [float(r['elapsed_s']) for r in data],
                marker='o', color=colors[ni], linestyle='-' if c else '--',
                label=f'nice={ni}, competitor={c}')
    ax.set(xlabel='Round (condition order shuffled within every round)', ylabel='Elapsed seconds',
           title='All trials retained: variability and order drift')
    ax.set_xticks(range(1, nrounds+1)); ax.legend(frameon=False, ncols=2); ax.grid(alpha=0.15)
    fig.savefig(args.output / 'nice_rounds.png', dpi=180); plt.close(fig)
    for s in summaries:
        print(f"nice={s['target_nice']:2d} competition={s['competition']} n={s['n']} elapsed mean={s['elapsed_s_mean']:.6f} median={s['elapsed_s_median']:.6f} SD={s['elapsed_s_std']:.6f} CPU mean={s['cpu_s_mean']:.6f}")
    print(f'VALIDATION PASS: {len(rows)} trials, {nrounds} complete rounds per condition')


if __name__ == '__main__':
    main()
