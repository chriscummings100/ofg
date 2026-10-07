# Summarize preserved terrain frame/function CSVs; function totals are cumulative checkpoints.
import csv
import json
import statistics
from pathlib import Path
import sys


def summarize(directory):
    """Return measured steady-frame costs and completed function costs per phase."""
    rows = list(csv.DictReader((directory / 'stationary.csv').open()))
    steady = [r for r in rows if float(r['seconds']) >= 45]
    result = {'frames': len(rows), 'steady_frames': len(steady), 'final': rows[-1]}
    for column in ['update_ms', 'extract_ms', 'render_ms', 'wait_ms', 'frame_ms']:
        values = [sum(float(r[c]) for c in ['update_ms', 'extract_ms', 'render_ms', 'wait_ms'])
                  if column == 'frame_ms' else float(r[column]) for r in steady]
        result[column] = {'mean': statistics.mean(values), 'median': statistics.median(values),
                          'p95': sorted(values)[int((len(values)-1)*.95)]}
    function_path = directory / 'functions.csv'
    if function_path.exists():
        checkpoints = {}
        for row in csv.DictReader(function_path.open()):
            checkpoints.setdefault(row['phase'], {})[row['function']] = row
        for phase, end, start in [('startup_functions','converged',None),('steady_functions','total','warmup')]:
            if end not in checkpoints or (start and start not in checkpoints):
                continue
            functions = []
            for name, row in checkpoints[end].items():
                previous = checkpoints[start][name] if start else {}
                calls = int(row['calls']) - int(previous.get('calls', 0))
                frames = int(row['frames']) - int(previous.get('frames', 0))
                inclusive = float(row['inclusive_ms']) - float(previous.get('inclusive_ms', 0))
                exclusive = float(row['exclusive_ms']) - float(previous.get('exclusive_ms', 0))
                if calls:
                    functions.append({'function': name, 'calls': calls, 'calls_per_frame': calls/frames,
                                      'inclusive_ms_per_frame': inclusive/frames,
                                      'exclusive_ms_per_frame': exclusive/frames,
                                      'total_inclusive_ms': inclusive, 'total_exclusive_ms': exclusive})
            result[phase] = sorted(functions, key=lambda f: -f['inclusive_ms_per_frame'])
            first = next(iter(checkpoints[end].values()))
            result[phase+'_end_seconds'] = float(first['seconds'])
    return result


if __name__ == '__main__':
    root = Path(sys.argv[1])
    result = {p.name: summarize(p) for p in sorted(root.iterdir()) if p.is_dir() and (p/'stationary.csv').exists()}
    output = root / 'summary.json'
    output.write_text(json.dumps(result, indent=2)+'\n')
    for name, run in result.items():
        print(name, 'frame median/mean:', run['frame_ms'], 'final jobs/cut:',run['final']['jobs'],run['final']['cut'])
        for function in run.get('steady_functions', [])[:14]:
            print('  {function}: inclusive {inclusive_ms_per_frame:.4f}, self {exclusive_ms_per_frame:.4f} ms/frame, calls {calls_per_frame:.1f}'.format(**function))
    print(output)
