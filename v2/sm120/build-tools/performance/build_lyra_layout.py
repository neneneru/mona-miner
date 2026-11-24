"""Build SM120 Lyra lane and storage probes from the published middle kernel."""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path

from build_tail import HEADERS, compile_image

ROTATE_BYTES = """RDX_INLINE Word rdx_rot64(Word value,unsigned k){
        const uint32_t lo = uint32_t(value);
        const uint32_t hi = uint32_t(value >> 32);
        if (k == 16)
            return uint64_t(__byte_perm(lo, hi, 0x5432)) |
                   (uint64_t(__byte_perm(lo, hi, 0x1076)) << 32);
        if (k == 24)
            return uint64_t(__byte_perm(lo, hi, 0x6543)) |
                   (uint64_t(__byte_perm(lo, hi, 0x2107)) << 32);
        return (value >> k) | (value << (64 - k));
    }"""


def lane_probes(body):
    start = body.index('    rdx::transpose4(v,ops);')
    finish = body.index('    #pragma unroll\n    for(int j=0;j<4;++j) h[j]', start)
    body = body[:start] + '    lyra_layout::run<WIDTH, BLOCK>(v, scratch);\n' + body[finish:]
    body = body.replace('    rdx::CudaOps ops{scratch,threadIdx.x&31u};\n', '')
    widths = ((4, 64), (2, 64), (2, 32), (1, 32))
    probes = [(f'lyra-width{width}-block{block}', block, block * 48 * 8 * (4 // width),
               [f'-DWIDTH={width}']) for width, block in widths]
    return body, 'lyra_layout.cuh', probes


def register_probes(body):
    call = 'rdx::lyra_word(x,ops)'
    if body.count(call) != 1:
        raise ValueError('Expected one Lyra invocation in the published bridge')
    body = body.replace(call, 'lyra_register::run<REGISTER_COLUMNS, BLOCK>(x, scratch, ops)')
    storage = ((0, 64), (1, 64), (2, 64), (3, 64), (4, 64), (4, 32))
    probes = [(f'lyra-register{columns}-block{block}', block, block * (4 - columns) * 12 * 8,
               [f'-DREGISTER_COLUMNS={columns}']) for columns, block in storage]
    return body, 'lyra_register.cuh', probes


def tune_register(body):
    body, header, _ = register_probes(body)
    body = body.replace('#pragma unroll 1\n    for(unsigned pass=0;', '#pragma unroll PASSES\n    for(unsigned pass=0;')
    shapes = [(96, 1, 1, False), (128, 1, 1, False), (192, 1, 1, False),
              (32, 1, 16, False), (32, 1, 1, True), (64, 1, 1, True),
              (32, 2, 1, False), (32, 4, 1, False)]
    variants = []
    for block, passes, minimum, byte_rotate in shapes:
        label = f'lyra-gpr-b{block}-u{passes}'
        if minimum > 1:
            label += f'-min{minimum}'
        if byte_rotate:
            label += '-prmt'
        variants.append((label, block, 0, ['-DREGISTER_COLUMNS=4', f'-DPASSES={passes}',
                                         f'-DMINIMUM={minimum}', f'-DBYTE_ROTATE={int(byte_rotate)}']))
    return body, header, variants


def schedule_probes(body):
    reference, _, _ = register_probes(body)
    start = body.index('    uint64_t a=v[0]')
    end = body.index('    rdx::transpose4(v,ops);', start)
    scalar = reference.replace('lyra_register::run<REGISTER_COLUMNS, BLOCK>',
                               'lyra_register::run<4, BLOCK, true>')
    woven = body[:start] + '    lyra_schedule::run<WEAVE, BLOCK, XOR_UPDATE, SUFFIX, PEEL>(v, scratch, ops);\n' + body[end:]
    variants = [('lyra-xor-wander', 32, 0, ['-DWEAVE=0'])]
    for width, xor, suffix, peel in ((2, 0, 0, 0), (2, 0, 1, 0), (2, 1, 1, 0),
                                     (2, 0, 1, 1), (4, 0, 0, 0), (4, 0, 1, 0)):
        label = f'lyra-weave{width}-' + ('both' if suffix else 'prefix')
        label += ('-xor' if xor else '') + ('-peel' if peel else '')
        variants.append((label, 32, 0, [f'-DWEAVE={width}', f'-DXOR_UPDATE={xor}',
                                         f'-DSUFFIX={suffix}', f'-DPEEL={peel}']))
    return '#if WEAVE\n' + woven + '\n#else\n' + scalar + '\n#endif\n', 'lyra_schedule.cuh', variants


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sm120', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    family = parser.add_mutually_exclusive_group()
    family.add_argument('--register', action='store_true', help='Run register/SMEM matrix probes')
    family.add_argument('--tune-register', action='store_true', help='Tune the register-only matrix')
    family.add_argument('--schedule', action='store_true', help='Test predicated row updates and interleaved sponge rounds')
    args = parser.parse_args()
    device = args.sm120.resolve() / 'device-source' / 'R02_CUBE2_TAIL_FUSION'
    src = device / 'src'
    bridge = (src / 'r02_bridge.cuh').read_text()
    entry = bridge.index('extern "C"', bridge.index('// Residual same-owner tail.'))
    includes = bridge[:bridge.index('extern "C"')]
    factory = lane_probes
    if args.register:
        factory = register_probes
    elif args.tune_register:
        factory = tune_register
    elif args.schedule:
        factory = schedule_probes
    template, header, variants = factory(bridge[entry:])
    bounds = '__launch_bounds__(BLOCK, MINIMUM)' if args.tune_register else '__launch_bounds__(BLOCK)'
    template = template.replace('__launch_bounds__(64)', bounds)
    template = template.replace('size_t(blockIdx.x)*64', 'size_t(blockIdx.x)*BLOCK')
    if args.tune_register:
        ops = (src / 'cuda_ops.cuh').read_text()
        original = 'RDX_INLINE Word rdx_rot64(Word value,unsigned k){return (value>>k)|(value<<(64-k));}'
        if ops.count(original) != 1:
            raise ValueError('Expected the qualified rotate helper')
        patched = ops.replace(original, ROTATE_BYTES)
        includes = includes.replace('#include "cuda_ops.cuh"',
                                    '#if BYTE_ROTATE\n' + patched + '\n#else\n#include "cuda_ops.cuh"\n#endif')
    headers = ('lyra_register.cuh', 'lyra_layout.cuh', header) if args.schedule else (header,)
    layout = '\n'.join(Path(__file__).with_name(name).read_text() for name in headers)
    code = ('#include "cube.cuh"\n#include "upstream_skein.cuh"\n' + includes + layout + template).encode()
    flags = ['--gpu-architecture=sm_120', '--std=c++17', '--device-as-default-execution-space',
             '--ptxas-options=-v', '-DMONA_CUBE_IMAD_PHASE_MASK=15',
             f'-I{src}', f'-I{device / "generated"}']
    HEADERS['cstdint'] = '#pragma once\n#include <stdint.h>\n'
    sdk = Path(os.environ['CUDA_PATH']) / 'bin' / 'x64'
    args.out.mkdir()
    records = {}
    with os.add_dll_directory(str(sdk)):
        library = C.CDLL(str(sdk / 'nvrtc64_130_0.dll'))
        for label, block, shared, variant in variants:
            options = [*flags, *variant, f'-DBLOCK={block}']
            image = compile_image(library, code, options, args.out / label)
            (args.out / f'{label}.cubin').write_bytes(image)
            records[label] = {'block': block, 'shared': shared, 'sha256': hashlib.sha256(image).hexdigest()}
            print(json.dumps({label: records[label]}), flush=True)
    (args.out / 'middle-manifest.json').write_text(json.dumps(records, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
