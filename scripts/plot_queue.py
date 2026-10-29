#!/usr/bin/env python3
"""Draw a state heatmap from validated teaching-PCB CSV events."""
import csv
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.colors import ListedColormap

p = Path('results/pcb/trace.csv')
rows = list(csv.DictReader(p.open(newline='')))
if not rows or any(r['invariant'] != 'PASS' for r in rows):
    raise ValueError('Missing or failed invariant events')
names = list(dict.fromkeys(r['process'] for r in rows if r['event'] == 'create'))
states = {n: 'N' for n in names}
codes = {'N': 0, 'R': 1, 'w': 2, 'b': 3, 'F': 4}
matrix = []
for r in rows:
    active = set()
    for queue in ['ready', 'blocked']:
        for item in filter(None, r[queue].split(';')):
            name, state, _ = item.split(':')
            states[name] = state; active.add(name)
    for n in names:
        if n not in active and states[n] != 'N': states[n] = 'F'
    matrix.append([codes[states[n]] for n in names])
fig, ax = plt.subplots(figsize=(13.3, 5.5), constrained_layout=True)
ax.imshow(list(map(list, zip(*matrix))), aspect='auto', cmap=ListedColormap(['#EDF0F3', '#245B89', '#38A590', '#D79338', '#7A8290']), vmin=-.5, vmax=4.5)
labels = list(codes)
for x, row in enumerate(matrix):
    for y, val in enumerate(row): ax.text(x, y, labels[val], ha='center', va='center', color='white' if val else '#8A939B', fontsize=15, weight='bold')
ax.set_xticks(range(len(rows)), [f"{r['step']}\n{r['event']}" for r in rows], rotation=45, ha='right', fontsize=10)
ax.set_yticks(range(len(names)), names); ax.set_title(f'PCB double-queue trace: {len(rows)}/{len(rows)} invariant checks passed', loc='left', fontsize=21, weight='bold', pad=20)
fig.supxlabel('N: not created | R: running at ready head | w: ready | b: blocked | F: finished\nTeaching state machine, not Linux kernel queues', fontsize=11)
fig.savefig(p.parent / 'state_trace.png', dpi=180); plt.close(fig)
print(f'PCB plot generated from {len(rows)} recorded events')
