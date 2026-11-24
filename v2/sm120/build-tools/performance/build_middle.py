"""Build middle-kernel geometry and scheduling probes from the published bridge."""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path

from build_tail import HEADERS, compile_image


def prefetch_columns(core):
    # A column's stores do not affect another column, even when rowInOut
    # aliases one of the other rows. Keep current inputs while loading next.
    begin = core.index('\tfor (int i = 0; i < RDX_NCOL; i++)', core.index('void reduceDuplexRowt2('))
    end = core.index('\n\n\t\t#pragma unroll\n\t\tfor (int j = 0; j < 3; j++)\n\t\t\tstate[j]', begin)
    front = '''	for (int j = 0; j < 3; ++j) {
        state1[j] = ops.load(ps1 + j);
        state2[j] = ops.load(ps2 + j);
        state3[j] = ops.load(ps3 + j);
    }

    #pragma unroll 4
    for (int i = 0; i < RDX_NCOL; ++i) {
        const uint32_t s2 = ps2 + i * RDX_MEMSHIFT;
        const uint32_t s3 = ps3 + i * RDX_MEMSHIFT;
        typename Ops::Word next1[3], next2[3], next3[3];
        if (i + 1 < RDX_NCOL) {
            #pragma unroll
            for (int j = 0; j < 3; ++j) {
                const uint32_t next = (i + 1) * RDX_MEMSHIFT + j;
                next1[j] = ops.load(ps1 + next);
                next2[j] = ops.load(ps2 + next);
                next3[j] = ops.load(ps3 + next);
            }
        }'''
    finish = core.index('\n\t}\n}', end)
    tail = '''
        if (i + 1 < RDX_NCOL) {
            #pragma unroll
            for (int j = 0; j < 3; ++j) {
                state1[j] = next1[j];
                state2[j] = next2[j];
                state3[j] = next3[j];
            }
        }'''
    return core[:begin] + front + core[end:finish] + tail + core[finish:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sm120', type=Path, required=True)
    parser.add_argument('--row-trials', action='store_true')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir()
    device = args.sm120.resolve() / 'device-source' / 'R02_CUBE2_TAIL_FUSION'
    bridge = (device / 'src' / 'r02_bridge.cuh').read_text()
    entry = bridge.index('extern "C"', bridge.index('// Residual same-owner tail.'))
    primary = bridge[:bridge.index('extern "C"')] + bridge[entry:]
    ops = (device / 'src' / 'cuda_ops.cuh').read_text()
    core = (device / 'src' / 'lyra_core.hpp').read_text()
    preamble = '#include "cube.cuh"\n#include "upstream_skein.cuh"\n'
    HEADERS['cstdint'] = '#pragma once\n#include <stdint.h>\n'
    flags = ['--gpu-architecture=sm_120', '--std=c++17', '--device-as-default-execution-space',
             '--ptxas-options=-v', '-DMONA_CUBE_IMAD_PHASE_MASK=15',
             f'-I{device / "src"}', f'-I{device / "generated"}']
    sdk = Path(os.environ['CUDA_PATH']) / 'bin' / 'x64'
    records = {}
    with os.add_dll_directory(str(sdk)):
        lib = C.CDLL(str(sdk / 'nvrtc64_130_0.dll'))
        variants = ((64, 8, 1, False), (32, 8, 1, False), (64, 5, 1, False), (64, 5, 4, False),
                    (64, 8, 1, True), (64, 0, 1, True), (32, 0, 1, True), (64, 0, 4, True))
        variants = [(*setting, None) for setting in variants]
        if args.row_trials:
            variants = [(64, 8, 1, False, name) for name in ('row-unroll', 'column-prefetch', 'row-unroll-column-prefetch')]
        for block, barriers, unroll, fixed, row_trial in variants:
            label = f'middle-b{block}-sync{barriers}-u{unroll}' + ('-fixed' if fixed else '')
            code = primary.replace('__launch_bounds__(64)', f'__launch_bounds__({block})')
            code = code.replace('size_t(blockIdx.x)*64', f'size_t(blockIdx.x)*{block}')
            if barriers == 5:
                code = code.replace('    #pragma unroll 1\n    for(unsigned pass=0;pass<4;++pass) {\n        __syncwarp();',
                                    f'    __syncwarp();\n    #pragma unroll {unroll}\n    for(unsigned pass=0;pass<4;++pass) {{')
            if barriers == 0:
                code = code.replace('        __syncwarp();\n', '')
                code = code.replace('#pragma unroll 1', f'#pragma unroll {unroll}')
            if fixed:
                code = code.replace('#include "cuda_ops.cuh"', ops.replace('blockDim.x', str(block)))
            if row_trial:
                tuned_core = core
                if 'row-unroll' in row_trial:
                    tuned_core = tuned_core.replace('#pragma unroll 1\n    for(int row=0;', '#pragma unroll 3\n    for(int row=0;')
                if 'column-prefetch' in row_trial:
                    tuned_core = prefetch_columns(tuned_core)
                code = code.replace('#include "lyra_core.hpp"', tuned_core)
                label = row_trial
            image = compile_image(lib, (preamble + code).encode(), flags, args.out / label)
            (args.out / (label + '.cubin')).write_bytes(image)
            records[label] = {'block': block, 'shared': block*48*8, 'sha256': hashlib.sha256(image).hexdigest()}
            print(json.dumps({label: records[label]}), flush=True)
    (args.out / 'middle-manifest.json').write_text(json.dumps(records, indent=2) + '\n', encoding='utf-8')

if __name__ == '__main__':
    main()
