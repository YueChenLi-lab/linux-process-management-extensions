#!/usr/bin/env python3
"""Plot actual deterministic simulator CSV output, preserving input/task order."""
import argparse
import csv
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    summaries, timelines = [], []
    for algorithm in ['fcfs', 'sjf', 'rr']:
        with (args.directory / f'{algorithm}.summary.csv').open(newline='') as f:
            summaries.extend(csv.DictReader(f))
        with (args.directory / f'{algorithm}.timeline.csv').open(newline='') as f:
            timelines.append(list(csv.DictReader(f)))
    with (args.directory / 'comparison.csv').open('w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=list(summaries[0]))
        writer.writeheader(); writer.writerows(summaries)
    palette = ['#245B89', '#38A590', '#D79338', '#8861A8']
    names = list(dict.fromkeys(e['task'] for timeline in timelines for e in timeline if e['task'] != 'IDLE'))
    colors = {name: palette[i % len(palette)] for i, name in enumerate(names)}
    colors['IDLE'] = '#D8DEE4'
    plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.size': 13})
    fig, ax = plt.subplots(figsize=(13.3, 5.5), constrained_layout=True)
    for i, timeline in enumerate(timelines):
        for e in timeline:
            start, end = int(e['start']), int(e['end'])
            ax.barh(2-i, end-start, left=start, height=0.55, color=colors[e['task']], edgecolor='white', linewidth=2)
            ax.text((start+end)/2, 2-i, e['task'], ha='center', va='center', color='white' if e['task'] != 'IDLE' else '#172A3A', weight='bold')
    ticks = sorted({int(e[k]) for timeline in timelines for e in timeline for k in ['start', 'end']})
    ax.set_xticks(ticks); ax.set_yticks([2, 1, 0], ['FCFS', 'SJF (non-preemptive)', f"RR (q={summaries[2]['quantum']})"])
    ax.set_xlabel('Simulated time (ticks)'); ax.set_ylim(-0.7, 2.7)
    ax.set_title('Same workload, different execution order', loc='left', fontsize=21, weight='bold', pad=18)
    for spine in ax.spines.values(): spine.set_visible(False)
    ax.grid(axis='x', alpha=0.14); ax.set_axisbelow(True)
    fig.supxlabel('Source: C simulator timeline CSV | This figure is not a real Linux scheduling trace', fontsize=10)
    fig.savefig(args.directory / 'gantt.png', dpi=180); fig.savefig(args.directory / 'gantt.svg'); plt.close(fig)
    fig, ax = plt.subplots(figsize=(12, 5.5), constrained_layout=True)
    cols = ['Algorithm', 'Mean waiting', 'Mean turnaround', 'Mean response', 'Switches']
    values = [[r['algorithm'].upper(), *[f"{float(r[k]):.3f}" for k in ['mean_waiting', 'mean_turnaround', 'mean_response']], r['context_switches']] for r in summaries]
    ax.axis('off')
    table = ax.table(cellText=values, colLabels=cols, cellLoc='center', bbox=[0, .12, 1, .68])
    table.auto_set_font_size(False); table.set_fontsize(15)
    for (row, col), cell in table.get_celld().items():
        cell.set_edgecolor('white'); cell.set_linewidth(3)
        cell.set_facecolor('#EAF0F5' if row % 2 else '#F4F7FA')
        if row == 0: cell.set_facecolor('#203D55'); cell.get_text().set_color('white'); cell.get_text().set_weight('bold')
    ax.set_title('Measured simulator metrics for the identical task input', fontsize=19, weight='bold', loc='left')
    fig.supxlabel('All time metrics: simulated ticks | Switches exclude transitions to/from IDLE', fontsize=11)
    fig.savefig(args.directory / 'comparison.png', dpi=180); plt.close(fig)
    print('Schedule plots and combined comparison.csv generated from C output')


if __name__ == '__main__':
    main()
