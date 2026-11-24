"""Read sampled SM clocks for the Lyra2 middle-stage diagnostic."""
import argparse
import ctypes as C
import json
from pathlib import Path
import statistics
import struct
from types import SimpleNamespace

from gpu_trial import D, Driver, NATIVE_BATCH, Slot, Z, install, phase_times, routes

PHASES = ('cube', 'lyra_prefix', 'lyra_matrix', 'lyra_suffix', 'total')
SAMPLE_COUNT = 256
REFERENCE = 'lyra-gpr-cube-mix0'
PROBE = 'lyra-gpr-phase-probe'


def measure(slot, reference, probe, sample_ptr):
    timings = {}
    for label, route in ((REFERENCE, reference), (PROBE, probe)):
        for i in range(12):
            slot.scan(route, i * NATIVE_BATCH, NATIVE_BATCH, 0)
        timings[label] = phase_times(slot, route, NATIVE_BATCH)
    rows = []
    for i in range(4):
        slot.scan(probe, i * NATIVE_BATCH, NATIVE_BATCH, 0)
        rows.extend(struct.iter_unpack('<5Q', slot.get(sample_ptr, SAMPLE_COUNT * 40)))
    if any(row[-1] == 0 or sum(row[:-1]) > row[-1] for row in rows):
        raise ValueError('Incomplete or inconsistent SM-clock sample')
    median = {phase: statistics.median(row[i] for row in rows) for i, phase in enumerate(PHASES)}
    shares = {phase: statistics.median(100 * row[i] / row[-1] for row in rows)
              for i, phase in enumerate(PHASES[:-1])}
    return dict(event_ms=timings, median_cycles=median, median_sample_pct=shares, samples=rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sm120', type=Path, required=True)
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--device', type=int, default=0)
    args = parser.parse_args()
    fixture = json.loads(args.oracle.read_text())
    manifest = json.loads((args.probe / 'middle-manifest.json').read_text())
    if set(manifest) != {PROBE}:
        raise ValueError('Expected the single phase-probe cubin')
    config = SimpleNamespace(sm120=args.sm120, candidates=None,
                             middle_images=[args.reference, args.probe])
    cu = Driver(args.device)
    try:
        producers, choices = routes(cu, config, fixture)
        install(cu, producers, fixture['cases'][3])
        # The diagnostic module is the last, single entry loaded by routes().
        ptr, byte_count = D(), Z()
        cu.run('ModuleGetGlobal_v2', C.byref(ptr), C.byref(byte_count), cu.modules[-1], b'lyra_phase_ticks')
        if byte_count.value != SAMPLE_COUNT * 40:
            raise ValueError('Unexpected phase sample buffer size')
        slot = Slot(cu, NATIVE_BATCH)
        try:
            report = measure(slot, choices[REFERENCE], choices[PROBE], ptr.value)
        finally:
            slot.close()
    finally:
        cu.close()
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({key: value for key, value in report.items() if key != 'samples'}))


if __name__ == '__main__':
    main()
