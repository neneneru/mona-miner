"""Move hash stages around the SM120 register-only Lyra kernel for offline A/B."""
import argparse
import ctypes as C
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re

from build_tail import HEADERS, compile_image


@dataclass(frozen=True)
class Variant:
    label: str
    move_head: bool = False
    move_tail: int = 2  # 0 keeps the stages, 1 moves CubeHash, 2 also moves Skein.
    tail_block: int = 256
    middle_block: int = 32
    imad_mask: int = 15
    elide_pass_barrier: bool = False
    flat_cube: bool = False
    cube_grouped: bool = False
    cube_unroll: int = 1
    cube_mix: int = 0
    prefix_block: int = 0
    pair_middle: bool = False
    pair_tail: bool = False
    min_residency: int = 0
    spill_shared: bool = False
    lyra_width: int = 0
    lyra_register_columns: int = 0
    lyra_shuffle_xor: int = 0
    lyra_sender_twist: bool = False
    lyra_predicated_row: bool = False
    lyra_sponge_unroll: int = 1
    lyra_imad64: int = 0
    lyra_peel_final: bool = False
    lyra_shared_rows: int = 0
    lyra_prefetch_row: bool = False
    fused_head: bool = False
    complete_tail: bool = False
    lyra_defer_columns: int = 0
    lyra_defer_shared: bool = False
    keccak_unroll: int = 0
    nonce_group: int = 0
    group_register_columns: int = 4
    group_interleaved: bool = False
    skein_layout: int = 0
    cube_fused_tile: bool = False
    cube_prefix_width: int = 0
    cube_tail_width: int = 0
    cube_tile_shift: int = 1
    cube_tile_imad: bool = True


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f'Expected one source anchor: {old}')
    return text.replace(old, new, 1)


def producer_source(device):
    n02 = device / 'N02_EXP01_PRODUCER'
    upstream = (n02 / 'generated' / 'upstream_blake.cuh').read_text()
    helpers = upstream[upstream.index('#include "cuda_helper.h"'):upstream.index('__host__ __forceinline__')]
    for decl in ('__constant__ static uint32_t c_data[3 + 1];',
                 '__device__ __constant__ static uint32_t cpu_h[8];'):
        helpers = replace_once(helpers, decl, 'extern "C" { ' + decl.replace('static ', '') + ' }')
    code = (n02 / 'src' / 'n02_blake_job_columns.cuh').read_text()
    loop = '#pragma unroll 1\n\tfor (int i = 0; i < 23; i++)'
    code = replace_once(code, loop, '#if KECCAK_UNROLL\n#pragma unroll KECCAK_UNROLL\n'
                        '#else\n#pragma unroll 1\n#endif\n\tfor (int i = 0; i < 23; i++)')
    initial = 'uint64_t *outputHash = (uint64_t *)Hash;'
    last = 'outputHash[i*threads + thread] = devectorize(keccak_gpu_state[i]);'
    start, end = code.index(initial), code.index(last) + len(last)
    stores = """#if MOVE_PRODUCER
        mona_cube32(keccak_gpu_state, mona_one);
#endif
#if GROUP_INTERLEAVED
        #pragma unroll
        for (unsigned i = 0; i < 4; ++i)
            reinterpret_cast<uint2*>(Hash)[size_t(thread)*4 + i] = keccak_gpu_state[i];
#elif VECTOR_IO
        store_vectors(reinterpret_cast<uint2*>(Hash), threads, thread, keccak_gpu_state);
#else
        """ + code[start:end] + """
#endif"""
    entry = code.index('extern "C" __global__ __launch_bounds__(64)')
    digest_start = code.index('const uint32_t T0 = 640;', entry)
    digest = ('__device__ __forceinline__ void n02_digest(uint32_t nonce, uint2* output) {\n' +
              code[digest_start:start] +
              '    #pragma unroll\n'
              '    for (unsigned j = 0; j < 4; ++j) output[j] = keccak_gpu_state[j];\n}\n')
    kernel = code[entry:start] + stores + code[end:]
    return ('#define SPH_ROTR32(x,n) (((x) >> (n)) | ((x) << (32-(n))))\n' +
            helpers + code[:entry] + '\n#if FUSE_HEAD\n' + digest +
            '\n#else\n' + kernel + '\n#endif\n#undef SPH_ROTR32\n')


def source(sm120):
    device = sm120 / 'device-source'
    fusion = device / 'R02_CUBE2_TAIL_FUSION'
    bridge = (fusion / 'src' / 'r02_bridge.cuh').read_text()
    start = bridge.index('extern "C"', bridge.index('// Residual same-owner tail.'))
    middle = bridge[start:]
    middle = replace_once(middle, 'extern "C" __global__ __launch_bounds__(64)',
                          'extern "C" __global__\n'
                          '#if MIN_RESIDENCY\n__launch_bounds__(MIDDLE_BLOCK, MIN_RESIDENCY)\n'
                          '#else\n__launch_bounds__(MIDDLE_BLOCK)\n#endif')
    middle = replace_once(middle, 'size_t(blockIdx.x)*64', 'size_t(blockIdx.x)*MIDDLE_BLOCK')
    middle = replace_once(middle, 'rdx::lyra_word(x,ops)', 'lyra_register::run<4, MIDDLE_BLOCK>(x, scratch, ops)')
    ops_init = '    rdx::CudaOps ops{scratch,threadIdx.x&31u};'
    middle = replace_once(middle, ops_init, ops_init +
                          '\n#if LYRA_IMAD64\n    ops.one = mona_one;\n#endif')
    warp_sync = '        __syncwarp();'
    if middle.count(warp_sync) != 2:
        raise ValueError('Expected the two pass barriers')
    middle = middle.replace(warp_sync, '#if !ELIDE_PASS_BARRIER\n' + warp_sync + '\n#endif')
    transpose = '    rdx::transpose4(v,ops);'
    lyra_start = middle.index(transpose)
    lyra_end = middle.index(transpose, lyra_start + len(transpose)) + len(transpose)
    middle = (middle[:lyra_start] + '#if LYRA_WIDTH\n'
              '    lyra_hybrid_layout::run<LYRA_WIDTH, LYRA_REGISTER_COLUMNS, MIDDLE_BLOCK>(v, scratch);\n'
              '#else\n' + middle[lyra_start:lyra_end] + '\n#endif' + middle[lyra_end:])
    middle = replace_once(middle, 'uint32_t mona_one) {',
                          'uint32_t mona_one\n'
                          '#if COMPLETE || FUSE_HEAD\n    ,const uint32_t* nonce\n#endif\n'
                          '#if COMPLETE\n'
                          '    ,uint64_t target, DeviceCandidates* hits, uint64_t* upper\n'
                          '#endif\n'
                          ') {\n'
                          '#if SMEM_SPILL\n    asm volatile(".pragma \\"enable_smem_spilling\\";");\n#endif')
    middle = replace_once(
        middle, 'mona_cube32(h,mona_one);\n    uint64_t v[4];',
        '#if PAIR_MIDDLE\n    mona_cube_pair(h,mona_one);\n'
        '    #elif !MOVE_PRODUCER && !CUBE_PREFIX_WIDTH\n'
        '    mona_cube32(h,mona_one);\n    #endif\n    uint64_t v[4];')
    reads = '    #pragma unroll\n    for(int j=0;j<4;++j) h[j]=live?hash[owner+size_t(j)*n]:make_uint2(0,0);'
    middle = replace_once(middle, reads,
                          '#if FUSE_HEAD\n    if (live) n02_digest(*nonce + uint32_t(owner), h);\n'
                          '    else {\n        #pragma unroll\n'
                          '        for (unsigned j = 0; j < 4; ++j) h[j] = make_uint2(0, 0);\n    }\n'
                          '#elif VECTOR_IO\n    load_vectors(hash, n, owner, h, live);\n'
                          '#else\n' + reads + '\n#endif')
    middle = replace_once(middle, 'r02_skein_registers(h);\n    mona_cube32(h,mona_one);',
                          '#if MOVE_TAIL < 2\n    r02_skein_registers(h);\n    #endif\n'
                          '    #if !MOVE_TAIL\n    mona_cube32(h,mona_one);\n    #endif')
    folder = Path(__file__).parent
    tail = (folder / 'cube2_bmw.cu').read_text()
    tail_definitions = tail[:tail.index('#ifndef MONA_EXPERIMENT_BLOCK')]
    filter_code = tail[tail.index('    uint32_t message[16]'):tail.index('\n}\n\nextern "C"')]
    filter_code = filter_code.replace('parts[', 'h[').replace('if constexpr (Debug) upper[index]',
                                                            'if (upper) upper[owner]')
    filter_code = replace_once(filter_code, '*nonce + index', '*nonce + uint32_t(owner)')
    store = '    #pragma unroll\n    for(int j=0;j<4;++j) if(live)hash[owner+size_t(j)*n]=h[j];'
    middle = replace_once(middle, store,
                          '#if COMPLETE\n    if (live) {\n' + filter_code + '\n    }\n'
                          '#elif VECTOR_IO\n    if (live) store_vectors(hash, n, owner, h);\n'
                          '#else\n' + store + '\n#endif')
    tail = replace_once(tail, 'mona_cube32(parts, one);',
                        '#if MOVE_TAIL == 2\n    r02_skein_registers(parts);\n    #endif\n'
                        '    #if PAIR_TAIL\n    mona_cube_pair(parts, one);\n'
                        '    if (!live) return;\n    #elif !CUBE_TAIL_WIDTH\n    mona_cube32(parts, one);\n    #endif')
    tail = replace_once(tail, 'const uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;',
                        'const uint32_t thread = blockIdx.x * blockDim.x + threadIdx.x;\n'
                        '    #if CUBE_TAIL_WIDTH\n'
                        '    constexpr unsigned width = CUBE_TAIL_WIDTH;\n'
                        '    const unsigned lane = threadIdx.x & (width - 1);\n'
                        '    const uint32_t index = thread / width;\n'
                        '    #else\n    const uint32_t index = thread;\n    #endif')
    tail = replace_once(tail, 'if (index >= count) return;',
                        'const bool live = index < count;\n    #if !PAIR_TAIL && !CUBE_TAIL_WIDTH\n'
                        '    if (!live) return;\n    #endif')
    reads = ('    #pragma unroll\n    for (int i = 0; i < 4; ++i) {\n'
             '        parts[i] = hash[index + size_t(i) * count];\n    }')
    tail = replace_once(tail, reads,
                        '#if CUBE_TAIL_WIDTH\n'
                        '    const auto input = live && lane < 4 ? hash[index + size_t(lane) * count] : make_uint2(0, 0);\n'
                        '    const auto tiles = cube_tile::Tile<width, CUBE_TILE_SHIFT>{lane}.run(input, one);\n'
                        '    #pragma unroll\n'
                        '    for (unsigned i = 0; i < 4; ++i) {\n'
                        '        parts[i].x = __shfl_sync(0xffffffffu, tiles.x, i, width);\n'
                        '        parts[i].y = __shfl_sync(0xffffffffu, tiles.y, i, width);\n'
                        '    }\n'
                        '    if (!live || lane) return;\n'
                        '#elif GROUP_INTERLEAVED\n    #pragma unroll\n'
                        '    for (unsigned i = 0; i < 4; ++i) parts[i] = hash[size_t(index)*4 + i];\n'
                        '#elif VECTOR_IO\n    load_vectors(hash, count, index, parts, true);\n'
                        '#elif PAIR_TAIL\n    #pragma unroll\n    for (int i = 0; i < 4; ++i)\n'
                        '        parts[i] = live ? hash[index + size_t(i) * count] : make_uint2(0, 0);\n'
                        '#else\n' + reads + '\n#endif')
    preamble = ('#include "cube.cuh"\n#include "upstream_skein.cuh"\n#include "cuda_ops.cuh"\n'
                '#include "lyra_core.hpp"\n#include "r02_skein_registers.cuh"\n')
    preamble = replace_once(preamble, '#include "r02_skein_registers.cuh"',
                            '#if SKEIN_LAYOUT\n' + (folder / 'skein_native.cuh').read_text() +
                            '\n#else\n#include "r02_skein_registers.cuh"\n#endif')
    original_ops = (fusion / 'src' / 'cuda_ops.cuh').read_text()
    select_lane = ('RDX_INLINE Word shuffle(Word value,int delta){'
                   'return from_lane(value,unsigned(int(lane&3)+delta)&3u,4);}')
    xor_lane = """RDX_INLINE Word shuffle(Word value,int delta) {
        const unsigned distance = unsigned(delta) & 3u;
        if (distance == 0) return value;
        if (LYRA_SHUFFLE_XOR == 1 && distance != 2)
            return from_lane(value, (lane + distance) & 3u, 4);
        const unsigned bits = distance == 2 ? 2u : 1u | (((lane ^ (distance >> 1)) & 1u) << 1);
        const uint32_t lo = __shfl_xor_sync(0xffffffffu, uint32_t(value), bits, 4);
        const uint32_t hi = __shfl_xor_sync(0xffffffffu, uint32_t(value >> 32), bits, 4);
        return uint64_t(lo) | (uint64_t(hi) << 32);
    }"""
    changed_ops = replace_once(original_ops, select_lane,
                               '\n#if LYRA_SHUFFLE_XOR\n' + xor_lane +
                               '\n#else\n    ' + select_lane + '\n#endif\n')
    sum64 = """
#if LYRA_IMAD64
    uint32_t one;
    template<bool Primary>
    RDX_INLINE Word add64(Word a, Word b) {
        if constexpr (!Primary && LYRA_IMAD64 == 3) return a + b;
        const uint32_t b_lo = uint32_t(b);
        const uint32_t b_hi = uint32_t(b >> 32);
        Word result;
        if constexpr (LYRA_IMAD64 == 1) {
            asm("mad.wide.u32 %0, %1, %2, %3;" : "=l"(result) : "r"(b_lo), "r"(one), "l"(a));
            const uint32_t high = uint32_t(result >> 32) + b_hi;
            return (uint64_t(high) << 32) | uint32_t(result);
        } else {
            const uint32_t high = uint32_t(a >> 32) + b_hi;
            const Word addend = (uint64_t(high) << 32) | uint32_t(a);
            asm("mad.wide.u32 %0, %1, %2, %3;" : "=l"(result) : "r"(b_lo), "r"(one), "l"(addend));
            return result;
        }
    }
#endif
"""
    changed_ops = replace_once(changed_ops, '    unsigned lane;\n', '    unsigned lane;\n' + sum64)
    preamble = replace_once(preamble, '#include "cuda_ops.cuh"',
                            '#if LYRA_SHUFFLE_XOR || LYRA_IMAD64\n' + changed_ops +
                            '\n#else\n#include "cuda_ops.cuh"\n#endif')
    core = (fusion / 'src' / 'lyra_core.hpp').read_text()
    if core.count('a += b;') != 2 or core.count('c += d;') != 2:
        raise ValueError('Expected the four additions of the Lyra G function')
    changed_core = core.replace('a += b;', 'a = ops.template add64<true>(a, b);')
    changed_core = changed_core.replace('c += d;', 'c = ops.template add64<false>(c, d);')
    preamble = replace_once(preamble, '#include "lyra_core.hpp"',
                            '#if LYRA_IMAD64\n' + changed_core +
                            '\n#else\n#include "lyra_core.hpp"\n#endif')
    cube_layout = (folder / 'cube_word_layout.cuh').read_text()
    pair_layout = (folder / 'cube_pair_layout.cuh').read_text()
    original_cube = (fusion / 'src' / 'cube.cuh').read_text()
    iv = re.findall(r'0x[0-9A-Fa-f]{8}', original_cube)
    flat_iv = re.findall(r'0x[0-9A-Fa-f]{8}', cube_layout)
    if len(iv) != 32 or flat_iv != iv:
        raise ValueError('The experimental CubeHash IV must match the published 32-word IV')
    pair_layout = replace_once(pair_layout, 'CUBE_INITIAL_VALUES', ', '.join(iv))
    tiles = replace_once((folder / 'cube_tile.cuh').read_text(), 'CUBE_TILE_INITIAL_VALUES', ', '.join(iv))
    preamble = replace_once(preamble, '#include "cube.cuh"',
                            '#if FLAT_CUBE\n#define mona_cube32 mona_cube_reference\n'
                            '#include "cube.cuh"\n#undef mona_cube32\n' + cube_layout +
                            '\n#else\n#include "cube.cuh"\n#endif')
    vector_io = (folder / 'lyra_vector_io.cuh').read_text()
    sponge = (folder / 'lyra_split_sponge.cuh').read_text()
    probe = (folder / 'lyra_phase_probe.cuh').read_text()
    hybrid = '\n'.join((folder / name).read_text()
                       for name in ('lyra_layout.cuh', 'lyra_hybrid_layout.cuh'))
    groups = (folder / 'lyra_nonce_group.cuh').read_text()
    middle = ('\n#if LYRA_NONCE_GROUP\n' + groups +
              '\n#elif PHASE_PROBE\n' + probe + '\n#elif PREFIX_BLOCK && !CUBE_PREFIX_WIDTH\n' + sponge +
              '\n#else\n' + middle + '\n#endif\n')
    return (preamble + '\n#if PAIR_MIDDLE || PAIR_TAIL\n' + pair_layout + '\n#endif\n' +
            '\n#if CUBE_FUSED_TILE || CUBE_PREFIX_WIDTH || CUBE_TAIL_WIDTH\n' + tiles + '\n#endif\n' +
            '\n#if VECTOR_IO\n' + vector_io + '\n#endif\n' +
            (folder / 'lyra_register.cuh').read_text() +
            '\n#if LYRA_WIDTH\n' + hybrid + '\n#endif\n' +
            '\n#if COMPLETE\n' + tail_definitions + '\n#endif\n' +
            '\n#if MOVE_PRODUCER || VECTOR_IO || FUSE_HEAD || KECCAK_UNROLL\n' +
            producer_source(device) + '\n#endif\n' + middle +
            '\n#if MOVE_TAIL\n' + tail + '\n#endif\n').encode(), fusion


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sm120', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    family = parser.add_mutually_exclusive_group()
    family.add_argument('--complete', action='store_true', help='Fuse the BMW prefilter into the middle kernel')
    family.add_argument('--tune-tail', action='store_true', help='Tune the Skein, CubeHash and BMW fusion')
    family.add_argument('--vector-io', action='store_true', help='Use two packed 128-bit planes between kernels')
    family.add_argument('--cube-layout', action='store_true', help='Test logical-permutation CubeHash schedules')
    family.add_argument('--cube-mix', action='store_true', help='Balance IMAD and IADD across CubeHash words')
    family.add_argument('--split-sponge', action='store_true', help='Separate the initial Lyra sponge rounds')
    family.add_argument('--cube-pair', action='store_true', help='Split each CubeHash state across two CUDA lanes')
    family.add_argument('--spill', action='store_true', help='Compare shared and local spills at higher occupancy')
    family.add_argument('--profile-phases', action='store_true', help='Build the sampled Lyra timer')
    family.add_argument('--hybrid-lyra', action='store_true', help='Test narrower Lyra groups with a register matrix')
    family.add_argument('--lyra-instructions', action='store_true', help='Test scalar Lyra data movement and scheduling')
    family.add_argument('--lyra-imad64', action='store_true', help='Move part of the Lyra additions to IMAD.WIDE')
    family.add_argument('--lyra-final', action='store_true', help='Peel the last Lyra round for dead-output removal')
    family.add_argument('--lyra-row-placement', action='store_true', help='Place selected Lyra rows in shared memory')
    family.add_argument('--fuse-head', action='store_true', help='Keep the producer output in the Lyra owner registers')
    family.add_argument('--lyra-defer-row', action='store_true', help='Defer the final dynamically selected row updates')
    family.add_argument('--keccak-unroll', action='store_true', help='Unroll the full Keccak rounds in the producer')
    family.add_argument('--nonce-groups', action='store_true', help='Assign each Lyra nonce to a dedicated tile')
    family.add_argument('--skein-layout', action='store_true', help='Compare native Threefish rotation and unroll')
    family.add_argument('--cube-tiles', action='store_true', help='Distribute CubeHash banks over GPU lanes')
    args = parser.parse_args()
    code, fusion = source(args.sm120.resolve())
    args.out.mkdir()
    HEADERS['cstdint'] = '#pragma once\n#include <stdint.h>\n'
    sdk = Path(os.environ['CUDA_PATH']) / 'bin' / 'x64'
    common = ['--gpu-architecture=sm_120', '--std=c++17', '--device-as-default-execution-space',
              '--ptxas-options=-v',
              f'-I{fusion / "src"}', f'-I{fusion / "generated"}']
    variants = [
        Variant('head-cube1', move_head=True, move_tail=0, tail_block=0),
        Variant('tail-cube2-256', move_tail=1),
        Variant('tail-skein-cube2-256'),
        Variant('tail-skein-cube2-128', tail_block=128),
        Variant('both-cube1-cube2', move_head=True, move_tail=1),
        Variant('both-cube1-skein-cube2', move_head=True),
    ]
    if args.complete:
        variants = [
            Variant('complete-b32', move_tail=0, tail_block=0),
            Variant('complete-head-cube1-b32', move_head=True, move_tail=0, tail_block=0),
            Variant('complete-head-cube1-b64', move_head=True, move_tail=0, tail_block=0, middle_block=64),
        ]
    elif args.tune_tail:
        options = ((64, 15, 0), (512, 15, 0), (256, 15, 1), (512, 15, 1),
                   (256, 0, 0), (256, 7, 0))
        variants = [Variant(f'tail-b{b}-mask{mask}-elide{elide}', tail_block=b, imad_mask=mask,
                            elide_pass_barrier=bool(elide)) for b, mask, elide in options]
    elif args.vector_io:
        variants = [Variant('vector-tail'), Variant('vector-head-tail', move_head=True)]
    elif args.cube_layout:
        variants = [Variant(f'cube-word{grouped}-unroll{unroll}', flat_cube=True,
                            cube_grouped=bool(grouped), cube_unroll=unroll)
                    for grouped in (0, 1) for unroll in (1, 2, 4)]
    elif args.split_sponge:
        shapes = ((32, 32, False), (64, 32, False), (128, 32, False), (64, 64, False), (64, 32, True))
        variants = [Variant(f'split-prefix{prefix}-matrix{middle}' + ('-head' if head else ''),
                            move_head=head, middle_block=middle, prefix_block=prefix)
                    for prefix, middle, head in shapes]
    elif args.cube_pair:
        variants = [Variant('pair-cube1', pair_middle=True),
                    Variant('pair-cube2', pair_tail=True),
                    Variant('pair-both', pair_middle=True, pair_tail=True)]
    elif args.spill:
        shapes = ((32, 16, 0), (32, 16, 1), (32, 20, 1), (32, 24, 1), (64, 8, 1))
        variants = [Variant(f'spill-b{block}-min{resident}-shared{shared}', middle_block=block,
                            min_residency=resident, spill_shared=bool(shared))
                    for block, resident, shared in shapes]
    elif args.cube_mix:
        variants = [Variant(f'cube-mix{mix}', flat_cube=True, cube_grouped=True, cube_mix=mix)
                    for mix in (0, 1, 2, 3)]
    elif args.profile_phases:
        variants = [Variant('phase-probe', flat_cube=True, cube_grouped=True)]
    elif args.hybrid_lyra:
        shapes = [(2, 32, cols) for cols in (1, 2, 3, 4)] + [(2, 64, 3), (1, 32, 1), (1, 32, 2)]
        variants = [Variant(f'lyra-width{width}-b{block}-r{cols}', flat_cube=True, cube_grouped=True,
                            middle_block=block, lyra_width=width, lyra_register_columns=cols)
                    for width, block, cols in shapes]
    elif args.lyra_instructions:
        schedules = [
            ('xor2', dict(lyra_shuffle_xor=1)),
            ('xor-all', dict(lyra_shuffle_xor=2)),
            ('sender-twist', dict(lyra_sender_twist=True)),
            ('predicated-row', dict(lyra_predicated_row=True)),
            ('xor-all-sender', dict(lyra_shuffle_xor=2, lyra_sender_twist=True)),
            ('xor2-sender-row', dict(lyra_shuffle_xor=1, lyra_sender_twist=True, lyra_predicated_row=True)),
            ('sponge-unroll2', dict(lyra_sponge_unroll=2)),
            ('sponge-unroll3', dict(lyra_sponge_unroll=3)),
        ]
        variants = [Variant(f'lyra-{label}', flat_cube=True, cube_grouped=True, **flags)
                    for label, flags in schedules]
    elif args.lyra_imad64:
        variants = [Variant(f'lyra-imad64-mode{mode}' + ('-xor2' if shuffle else ''),
                            flat_cube=True, cube_grouped=True, lyra_imad64=mode, lyra_shuffle_xor=shuffle)
                    for mode, shuffle in ((1, 0), (2, 0), (3, 0), (2, 1), (3, 1))]
    elif args.lyra_final:
        variants = [Variant(f'lyra-final-{name}', flat_cube=True, cube_grouped=True,
                            lyra_peel_final=True, lyra_shuffle_xor=shuffle)
                    for name, shuffle in (('idx', 0), ('xor2', 1), ('xor-all', 2))]
    elif args.lyra_row_placement:
        shapes = [(mask, False, 0) for mask in (1, 4, 8, 9, 10, 12)]
        shapes += [(10, True, 0), (8, False, 16), (10, False, 16)]
        variants = [Variant(f'lyra-row-mask{mask}-prefetch{int(prefetch)}-min{resident}',
                            flat_cube=True, cube_grouped=True, lyra_shuffle_xor=2,
                            lyra_shared_rows=mask, lyra_prefetch_row=prefetch, min_residency=resident)
                    for mask, prefetch, resident in shapes]
    elif args.fuse_head:
        shapes = ((32, False), (64, False), (128, False), (32, True), (64, True))
        variants = [Variant(f'fused-head-b{block}' + ('-complete' if full else ''),
                            middle_block=block, tail_block=0 if full else 256, move_tail=0 if full else 2,
                            flat_cube=True, cube_grouped=True, lyra_shuffle_xor=2,
                            fused_head=True, complete_tail=full)
                    for block, full in shapes]
    elif args.lyra_defer_row:
        variants = [Variant(f'lyra-defer{cols}-shared{int(shared)}', flat_cube=True, cube_grouped=True,
                            lyra_shuffle_xor=2, lyra_defer_columns=cols, lyra_defer_shared=shared)
                    for cols in (2, 4) for shared in (False, True)]
    elif args.keccak_unroll:
        variants = [Variant(f'keccak-u{rounds}', flat_cube=True, cube_grouped=True, lyra_shuffle_xor=2,
                            keccak_unroll=rounds) for rounds in (1, 2, 4, 8, 23)]
        variants += [Variant(f'keccak-u{rounds}-complete', tail_block=0, move_tail=0, middle_block=64,
                             fused_head=True, complete_tail=True, flat_cube=True, cube_grouped=True,
                             lyra_shuffle_xor=2, keccak_unroll=rounds) for rounds in (2, 4, 8)]
    elif args.nonce_groups:
        variants = [Variant('nonce-group-control', move_head=True, flat_cube=True, cube_grouped=True,
                            lyra_shuffle_xor=2, elide_pass_barrier=True)]
        shapes = ((4, 64, 4, False), (4, 128, 4, False), (4, 256, 4, False),
                  (4, 128, 2, False), (4, 128, 3, False),
                  (4, 128, 4, True), (8, 128, 4, False), (8, 256, 4, True))
        variants += [Variant(f'nonce-group{width}-b{block}-r{columns}-aos{int(aos)}',
                             move_head=True, flat_cube=True, cube_grouped=True, lyra_shuffle_xor=2,
                             middle_block=block, nonce_group=width, group_register_columns=columns,
                             group_interleaved=aos)
                     for width, block, columns, aos in shapes]
    elif args.skein_layout:
        variants = [Variant(f'skein-native{layout}-tail{block}', flat_cube=True, cube_grouped=True,
                            lyra_shuffle_xor=2, skein_layout=layout, tail_block=block)
                    for layout in (1, 2) for block in (128, 256, 512)]
        variants += [Variant(f'skein-native{layout}-middle', flat_cube=True, cube_grouped=True,
                             lyra_shuffle_xor=2, skein_layout=layout, move_tail=1)
                     for layout in (1, 2)]
    elif args.cube_tiles:
        shapes = ((32, 1, True), (64, 1, True), (128, 1, True),
                  (64, 0, True), (64, 2, True), (64, 1, False))
        variants = [Variant(f'cube-tile-fused-b{block}-s{shift}-imad{int(imad)}',
                            middle_block=block, flat_cube=True, cube_grouped=True, lyra_shuffle_xor=2,
                            nonce_group=4, cube_fused_tile=True, cube_tile_shift=shift, cube_tile_imad=imad)
                    for block, shift, imad in shapes]
        variants += [Variant(f'cube-tile-prefix{width}-s{shift}', flat_cube=True, cube_grouped=True,
                             lyra_shuffle_xor=2, prefix_block=128, cube_prefix_width=width, cube_tile_shift=shift)
                     for width, shift in ((4, 1), (8, 1), (8, 0))]
        variants += [Variant(f'cube-tile-tail{width}-s{shift}', flat_cube=True, cube_grouped=True,
                             lyra_shuffle_xor=2, move_tail=1, cube_tail_width=width, cube_tile_shift=shift)
                     for width, shift in ((4, 1), (8, 1))]
    records = {}
    with os.add_dll_directory(str(sdk)):
        library = C.CDLL(str(sdk / 'nvrtc64_130_0.dll'))
        for variant in variants:
            label = 'lyra-gpr-' + variant.label
            complete = args.complete or variant.complete_tail
            options = {
                'MOVE_PRODUCER': variant.move_head,
                'MOVE_TAIL': variant.move_tail,
                'MONA_EXPERIMENT_BLOCK': variant.tail_block or 256,
                'MIDDLE_BLOCK': variant.middle_block,
                'COMPLETE': complete,
                'MONA_CUBE_IMAD_PHASE_MASK': variant.imad_mask,
                'ELIDE_PASS_BARRIER': variant.elide_pass_barrier,
                'VECTOR_IO': args.vector_io,
                'FLAT_CUBE': variant.flat_cube,
                'CUBE_GROUPED': variant.cube_grouped,
                'CUBE_UNROLL': variant.cube_unroll,
                'CUBE_MIX': variant.cube_mix,
                'PREFIX_BLOCK': variant.prefix_block,
                'PAIR_MIDDLE': variant.pair_middle,
                'PAIR_TAIL': variant.pair_tail,
                'MIN_RESIDENCY': variant.min_residency,
                'SMEM_SPILL': variant.spill_shared,
                'PHASE_PROBE': args.profile_phases,
                'LYRA_WIDTH': variant.lyra_width,
                'LYRA_REGISTER_COLUMNS': variant.lyra_register_columns,
                'LYRA_SHUFFLE_XOR': variant.lyra_shuffle_xor,
                'LYRA_SENDER_TWIST': variant.lyra_sender_twist,
                'LYRA_PREDICATED_ROW': variant.lyra_predicated_row,
                'LYRA_SPONGE_UNROLL': variant.lyra_sponge_unroll,
                'LYRA_IMAD64': variant.lyra_imad64,
                'LYRA_PEEL_FINAL': variant.lyra_peel_final,
                'LYRA_SHARED_ROWS': variant.lyra_shared_rows,
                'LYRA_PREFETCH_ROW': variant.lyra_prefetch_row,
                'FUSE_HEAD': variant.fused_head,
                'LYRA_DEFER_COLUMNS': variant.lyra_defer_columns,
                'LYRA_DEFER_SHARED': variant.lyra_defer_shared,
                'KECCAK_UNROLL': variant.keccak_unroll,
                'LYRA_NONCE_GROUP': variant.nonce_group,
                'GROUP_REGISTER_COLUMNS': variant.group_register_columns,
                'GROUP_INTERLEAVED': variant.group_interleaved,
                'SKEIN_LAYOUT': variant.skein_layout,
                'CUBE_FUSED_TILE': variant.cube_fused_tile,
                'CUBE_PREFIX_WIDTH': variant.cube_prefix_width,
                'CUBE_TAIL_WIDTH': variant.cube_tail_width,
                'CUBE_TILE_SHIFT': variant.cube_tile_shift,
                'CUBE_TILE_IMAD': variant.cube_tile_imad,
            }
            flags = [*common, *(f'-D{name}={int(value)}' for name, value in options.items())]
            image = compile_image(library, code, flags, args.out / label)
            (args.out / f'{label}.cubin').write_bytes(image)
            shared = (variant.middle_block * (4 - variant.lyra_register_columns) * 12 * 8
                      * (4 // variant.lyra_width)) if variant.lyra_width else 0
            if variant.lyra_shared_rows:
                shared = variant.middle_block * variant.lyra_shared_rows.bit_count() * 12 * 8
            elif variant.lyra_defer_shared:
                shared = variant.middle_block * variant.lyra_defer_columns * 3 * 8
            elif variant.nonce_group == 4:
                shared = variant.middle_block * (4 - variant.group_register_columns) * 12 * 8
            records[label] = {'block': variant.middle_block, 'shared': shared,
                              'producer': bool(variant.move_head or args.vector_io or variant.keccak_unroll),
                              'tail_block': variant.tail_block, 'complete': complete,
                              'fused_head': variant.fused_head,
                              'prefix_block': variant.prefix_block,
                              'prefix_width': variant.cube_prefix_width or 1,
                              'prefix_sponge': not variant.cube_prefix_width,
                              'middle_width': variant.nonce_group or 1,
                              'tail_width': variant.cube_tail_width or 1,
                              'sha256': hashlib.sha256(image).hexdigest()}
            print(json.dumps({label: records[label]}), flush=True)
    (args.out / 'middle-manifest.json').write_text(json.dumps(records, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
