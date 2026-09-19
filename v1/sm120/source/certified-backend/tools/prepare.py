#!/usr/bin/env python3
"""Extract only Lyra2REv2 kernels from an immutable ccminer checkout.

Never edits upstream. Checks exact Git blob identities and fails closed when a
source anchor changes. Generated files retain their upstream provenance.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import re
from pathlib import Path

BASE = '6ff4e50987e59a70056324a94ed8667cc0bf598d'
BLOBS = {
    'Algo256/cuda_blake256.cu': '418ca07ec53be0bbf56935e8fca53174d3ae7b16',
    'Algo256/cuda_cubehash256.cu': '153e87a10eb85043d7b5674adc1d0e1d701aa9c4',
    'Algo256/cuda_skein256.cu': 'cbeb660e85aaca0fbea2038223e78c5ada0824b0',
    'Algo256/cuda_bmw256.cu': '0fde12ee248d2faf5a54332738bf8f5e54a680af',
    'lyra2/cuda_lyra2v2.cu': 'df3291c1fc33a3b55ecbf4ab58c76d5dfb8acae2',
    'lyra2/cuda_lyra2_vectors.h': '6bb11d3c507fb325731c8324d7cafbec0999aeae',
    'cuda_helper.h': 'c51a3253326557762eefb4c2183f9c37148d4e50',
}

def blob_sha(data: bytes) -> str:
    return hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()

def load_checked(root: Path, name: str) -> str:
    raw = (root / name).read_bytes()
    # Git for Windows may have checked out CRLF. Verify the LF-normalized blob.
    lf = raw.replace(b'\r\n', b'\n')
    if blob_sha(lf) != BLOBS[name]:
        raise ValueError(f'upstream mismatch: {name}; require windows@{BASE}')
    try:
        return lf.decode('utf-8')
    except UnicodeDecodeError:
        return lf.decode('cp1252')

def replace_one(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        raise ValueError(f'expected one source anchor: {old[:100]!r}')
    return text.replace(old, new, 1)

def closing_brace(text: str, opening: int) -> int:
    """Find a C/C++ closing brace, ignoring strings, chars and comments."""
    if text[opening] != '{':
        raise ValueError('not an opening brace')
    depth, i, mode = 0, opening, 'code'
    while i < len(text):
        c, nxt = text[i], text[i:i+2]
        if mode in ('string', 'char'):
            if c == '\\':
                i += 2
                continue
            if c == ('"' if mode == 'string' else "'"):
                mode = 'code'
        elif mode == 'line':
            if c == '\n':
                mode = 'code'
        elif mode == 'block':
            if nxt == '*/':
                mode = 'code'
                i += 2
                continue
        elif nxt == '//':
            mode = 'line'; i += 2; continue
        elif nxt == '/*':
            mode = 'block'; i += 2; continue
        elif c == '"':
            mode = 'string'
        elif c == "'":
            mode = 'char'
        elif c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise ValueError('unbalanced C++ braces')

def through_function(text: str, name: str) -> str:
    m = re.search(r'\b' + re.escape(name) + r'\s*\(', text)
    if not m:
        raise ValueError(f'function not found: {name}')
    op = text.index('{', m.end())
    return text[:closing_brace(text, op)+1] + '\n'


def rewrite_function_segment(text: str, marker: str, edit) -> str:
    start=text.find(marker)
    if start < 0:
        raise ValueError(f'function marker not found: {marker!r}')
    op=text.index('{',start)
    end=closing_brace(text,op)+1
    return text[:start]+edit(text[start:end])+text[end:]

def add_member_macro_to_shuffle2_calls(segment: str) -> str:
    return re.sub(r'shuffle2\(([^,\n]+),\s*([^,\n]+),\s*4\)',
                  r'shuffle2(\1, \2, 4 MONA_LYRA_MEMBER_ARG)',segment)

def add_member_macro_to_mona_shfl4_calls(segment: str) -> str:
    return re.sub(r'mona_shfl4\(([^,\n]+),\s*([^,\n]+),\s*4\)',
                  r'mona_shfl4(\1, \2, 4 MONA_LYRA_MEMBER_ARG)',segment)

def rewrite_lyra_stable_members(text: str) -> str:
    """Thread stable membership only when MONA_LYRA_STABLE_BALLOT is enabled."""
    def edit_shuffle(segment: str) -> str:
        segment=replace_one(segment,'uint2 shuffle2(uint2 a, uint32_t b, uint32_t c)',
                            'uint2 shuffle2(uint2 a, uint32_t b, uint32_t c MONA_LYRA_MEMBER_PARAM)')
        segment=segment.replace('mona_shfl4(a.x, b, c)','mona_shfl4(a.x, b, c MONA_LYRA_MEMBER_ARG)')
        segment=segment.replace('mona_shfl4(a.y, b, c)','mona_shfl4(a.y, b, c MONA_LYRA_MEMBER_ARG)')
        return segment
    text=rewrite_function_segment(text,'uint2 shuffle2(uint2 a, uint32_t b, uint32_t c)',edit_shuffle)

    def edit_round(segment: str) -> str:
        segment=replace_one(segment,'void round_lyra_v5(uint2 s[4])',
                            'void round_lyra_v5(uint2 s[4] MONA_LYRA_MEMBER_PARAM)')
        return add_member_macro_to_shuffle2_calls(segment)
    text=rewrite_function_segment(text,'void round_lyra_v5(uint2 s[4])',edit_round)

    def edit_setup(segment: str) -> str:
        segment=replace_one(segment,'void reduceDuplexRowSetup2(uint2 state[4]',
                            'void reduceDuplexRowSetup2(uint2 state[4] MONA_LYRA_MEMBER_PARAM')
        segment=segment.replace('round_lyra_v5(state);','round_lyra_v5(state MONA_LYRA_MEMBER_ARG);')
        return add_member_macro_to_shuffle2_calls(segment)
    text=rewrite_function_segment(text,'void reduceDuplexRowSetup2(uint2 state[4]',edit_setup)

    def edit_row(segment: str,name: str) -> str:
        marker='uint2 state[4])'
        if marker not in segment:
            raise ValueError(f'{name}: state signature anchor not found')
        segment=segment.replace(marker,'uint2 state[4] MONA_LYRA_MEMBER_PARAM)',1)
        segment=segment.replace('round_lyra_v5(state);','round_lyra_v5(state MONA_LYRA_MEMBER_ARG);')
        return add_member_macro_to_shuffle2_calls(segment)
    text=rewrite_function_segment(text,'void reduceDuplexRowt2(const int rowIn,',lambda s:edit_row(s,'reduceDuplexRowt2'))
    text=rewrite_function_segment(text,'void reduceDuplexRowt2x4(const int rowInOut,',lambda s:edit_row(s,'reduceDuplexRowt2x4'))

    def edit_matrix(segment: str) -> str:
        segment=replace_one(segment,
            'const uint32_t thread = blockDim.y * blockIdx.x + threadIdx.y;\n\n\tif (thread < threads)',
            'const uint32_t thread = blockDim.y * blockIdx.x + threadIdx.y;\n'
            '#if defined(MONA_LYRA_DUMMY_TAIL)\n'
            '\tconst bool live = thread < threads;\n'
            '\tif (true) // all warp lanes remain participants; invalid groups use dummy state\n'
            '#elif defined(MONA_LYRA_STABLE_BALLOT)\n'
            '\tconst bool live = thread < threads;\n'
            '\tconst unsigned mona_members = mona_lyra_members(live);\n\n'
            '\tif (live)\n'
            '#else\n'
            '\tif (thread < threads)\n'
            '#endif')
        segment=segment.replace('reduceDuplexRowSetup2(state, row3_first);',
                                'reduceDuplexRowSetup2(state MONA_LYRA_MEMBER_ARG, row3_first);')
        segment=segment.replace('reduceDuplexRowSetup2(state);',
                                'reduceDuplexRowSetup2(state MONA_LYRA_MEMBER_ARG);')
        segment=segment.replace('mona_reduceDuplexRowt2_firstread(rowa, state, row3_first);',
                                'mona_reduceDuplexRowt2_firstread(rowa, state MONA_LYRA_MEMBER_ARG, row3_first);')
        segment=segment.replace('reduceDuplexRowt2(prev, rowa, i, state);',
                                'reduceDuplexRowt2(prev, rowa, i, state MONA_LYRA_MEMBER_ARG);')
        segment=segment.replace('reduceDuplexRowt2x4(rowa, state);',
                                'reduceDuplexRowt2x4(rowa, state MONA_LYRA_MEMBER_ARG);')

        # FULL_MASK + DUMMY_TAIL: every physical lane executes every shuffle,
        # including invalid hash groups in the final partial warp. Invalid
        # groups must never touch global DMatrix; give them deterministic
        # register state, let them use their own shared-memory slice, and
        # suppress the final global store.
        load_re=re.compile(
            r'(?P<decl>^[ \t]*uint2 state\[4\];\n)'
            r'(?P<loads>(?:^[ \t]*state\[[0-3]\]\s*=\s*\(\(uint2\*\)DMatrix\).*?;\n){4})',
            re.M)
        lm=load_re.search(segment)
        if not lm:
            raise ValueError('Lyra dummy-tail rewrite: matrix DMatrix load block not found')
        loads=lm.group('loads')
        wrapped=(
            lm.group('decl')
            + '#if defined(MONA_LYRA_DUMMY_TAIL)\n'
            + '\t\tif (live) {\n'
            + ''.join('\t'+line if line.strip() else line for line in loads.splitlines(True))
            + '\t\t} else {\n'
            + '\t\t\t#pragma unroll\n'
            + '\t\t\tfor (int k = 0; k < 4; ++k) state[k] = make_uint2(0u, 0u);\n'
            + '\t\t}\n'
            + '#else\n'
            + loads
            + '#endif\n')
        segment=segment[:lm.start()]+wrapped+segment[lm.end():]

        store_re=re.compile(
            r'(?P<stores>(?:^[ \t]*\(\(uint2\*\)DMatrix\).*?=\s*state\[[0-3]\];\n){4})',
            re.M)
        matches=list(store_re.finditer(segment))
        if len(matches) != 1:
            raise ValueError(f'Lyra dummy-tail rewrite: expected one matrix DMatrix store block, found {len(matches)}')
        sm=matches[0]
        stores=sm.group('stores')
        wrapped_store=(
            '#if defined(MONA_LYRA_DUMMY_TAIL)\n'
            + '\t\tif (live) {\n'
            + ''.join('\t'+line if line.strip() else line for line in stores.splitlines(True))
            + '\t\t}\n'
            + '#else\n'
            + stores
            + '#endif\n')
        segment=segment[:sm.start()]+wrapped_store+segment[sm.end():]
        return add_member_macro_to_mona_shfl4_calls(segment)
    text=rewrite_function_segment(text,'void lyra2v2_gpu_hash_32_2(uint32_t threads)',edit_matrix)

    # Fail closed: any old cooperative call/signature would bypass stable membership.
    for line in text.splitlines():
        if 'shuffle2(' in line and 'MONA_LYRA_MEMBER_' not in line:
            raise ValueError(f'Lyra stable-members rewrite left old shuffle2 form: {line.strip()}')
        if 'mona_shfl4(' in line and 'MONA_LYRA_MEMBER_' not in line:
            raise ValueError(f'Lyra stable-members rewrite left old mona_shfl4 form: {line.strip()}')
    required=(
        'round_lyra_v5(uint2 s[4] MONA_LYRA_MEMBER_PARAM)',
        'reduceDuplexRowSetup2(uint2 state[4] MONA_LYRA_MEMBER_PARAM',
        'const unsigned mona_members = mona_lyra_members(live);',
        '#if defined(MONA_LYRA_DUMMY_TAIL)',
        'for (int k = 0; k < 4; ++k) state[k] = make_uint2(0u, 0u);')
    for item in required:
        if item not in text:
            raise ValueError(f'Lyra stable-members rewrite missing anchor: {item}')
    return text

def rewrite_lyra_nextcol_prefetch(text: str) -> str:
    # Software-pipeline only the normal wandering rowIn/rowInOut next column.
    # Current-column writes touch column i while speculative reads touch i+1;
    # those address sets are disjoint for every logical-row alias.
    def edit(segment: str) -> str:
        segment=replace_one(segment,
            'uint2 state1[3], state2[3];',
            '''uint2 state1[3], state2[3];
#if defined(MONA_LYRA_PREFETCH_NEXTCOL)
\tuint2 state2_next[3];
#endif''')

        segment=replace_one(segment,
            '''\tconst uint32_t ps3 = memshift * Ncol * rowOut;

\tfor (int i = 0; i < Ncol; i++)''',
            '''\tconst uint32_t ps3 = memshift * Ncol * rowOut;

#if defined(MONA_LYRA_PREFETCH_NEXTCOL)
\t// Prime column 0. Later iterations consume values loaded during the
\t// previous column's round.
\t#pragma unroll
\tfor (int j = 0; j < 3; ++j)
\t\tstate1[j] = LD4S(ps1 + j);
\t#pragma unroll
\tfor (int j = 0; j < 3; ++j)
\t\tstate2[j] = LD4S(ps2 + j);
#endif

\tfor (int i = 0; i < Ncol; i++)''')

        current_loads='''\t\t#pragma unroll
\t\tfor (int j = 0; j < 3; j++)
\t\t\tstate1[j] = LD4S(s1 + j);

\t\t#pragma unroll
\t\tfor (int j = 0; j < 3; j++)
\t\t\tstate2[j] = LD4S(s2 + j);'''
        segment=replace_one(segment,current_loads,
            '''#if !defined(MONA_LYRA_PREFETCH_NEXTCOL)
'''+current_loads+'''
#endif''')

        xor_block='''\t\t#pragma unroll
\t\tfor (int j = 0; j < 3; j++)
\t\t\tstate[j] ^= state1[j] + state2[j];'''
        segment=replace_one(segment,xor_block,xor_block+'''

#if defined(MONA_LYRA_PREFETCH_NEXTCOL)
\t\t// state1 is dead after the XOR, so reuse it for next-column rowIn.
\t\t// state2 survives until the current-column store, so only next
\t\t// rowInOut needs extra live storage.
\t\tif (i + 1 < Ncol)
\t\t{
\t\t\tconst uint32_t next_s1 = ps1 + uint32_t(i + 1) * memshift;
\t\t\tconst uint32_t next_s2 = ps2 + uint32_t(i + 1) * memshift;
\t\t\t#pragma unroll
\t\t\tfor (int j = 0; j < 3; ++j)
\t\t\t\tstate1[j] = LD4S(next_s1 + j);
\t\t\t#pragma unroll
\t\t\tfor (int j = 0; j < 3; ++j)
\t\t\t\tstate2_next[j] = LD4S(next_s2 + j);
\t\t}
#endif''')

        tail='\n\t}\n}'
        pos=segment.rfind(tail)
        if pos < 0:
            raise ValueError('Lyra next-column prefetch: outer loop tail not found')
        carry='''\n#if defined(MONA_LYRA_PREFETCH_NEXTCOL)
\t\tif (i + 1 < Ncol)
\t\t{
\t\t\t#pragma unroll
\t\t\tfor (int j = 0; j < 3; ++j)
\t\t\t\tstate2[j] = state2_next[j];
\t\t}
#endif
'''
        segment=segment[:pos]+carry+segment[pos:]
        return segment

    return rewrite_function_segment(
        text,'void reduceDuplexRowt2(const int rowIn,',edit)


def rewrite_cube_round_adds_imad_phases(text: str, expected: int = 64, phase_width: int = 16) -> str:
    """Tag the 64 fixed CubeHash adds as four compile-time-selectable 16-add phases."""
    m = re.search(r'\brrounds\s*\(', text)
    if not m:
        raise ValueError('function not found: rrounds')
    op = text.index('{', m.end())
    end = closing_brace(text, op) + 1
    body = text[op:end]
    indexed = r'(?:x|x1)(?:\[[01]\]){4,5}'
    pattern = re.compile(
        rf'^(?P<indent>\s*)(?P<lhs>{indexed})\s*=\s*'
        rf'(?P<a>{indexed})\s*\+\s*(?P<b>{indexed});\s*$', re.M)
    matches = list(pattern.finditer(body))
    if len(matches) != expected:
        raise ValueError(f'expected {expected} CubeHash round adds, found {len(matches)}')
    if expected != 4 * phase_width:
        raise ValueError('CubeHash IMAD phase contract requires exactly four equal add phases')
    pieces = []
    last = 0
    for i, match in enumerate(matches):
        pieces.append(body[last:match.start()])
        phase = i // phase_width
        pieces.append(
            f"{match.group('indent')}{match.group('lhs')} = "
            f"MONA_CUBE_ADD_PHASE{phase}({match.group('a')}, {match.group('b')}, mona_one);")
        last = match.end()
    pieces.append(body[last:])
    rewritten = ''.join(pieces)
    return text[:op] + rewritten + text[end:]

CUBE_IMAD_PHASE_HELPER = r'''\
__device__ __forceinline__ uint32_t mona_cube_force_imad_u32(uint32_t a, uint32_t b, uint32_t one)
{
    uint32_t out;
    asm volatile ("mad.lo.u32 %0, %1, %2, %3;" : "=r"(out) : "r"(a), "r"(one), "r"(b));
    return out;
}

#if (MONA_CUBE_IMAD_PHASE_MASK & 1)
#define MONA_CUBE_ADD_PHASE0(a, b, one) mona_cube_force_imad_u32((a), (b), (one))
#else
#define MONA_CUBE_ADD_PHASE0(a, b, one) ((a) + (b))
#endif
#if (MONA_CUBE_IMAD_PHASE_MASK & 2)
#define MONA_CUBE_ADD_PHASE1(a, b, one) mona_cube_force_imad_u32((a), (b), (one))
#else
#define MONA_CUBE_ADD_PHASE1(a, b, one) ((a) + (b))
#endif
#if (MONA_CUBE_IMAD_PHASE_MASK & 4)
#define MONA_CUBE_ADD_PHASE2(a, b, one) mona_cube_force_imad_u32((a), (b), (one))
#else
#define MONA_CUBE_ADD_PHASE2(a, b, one) ((a) + (b))
#endif
#if (MONA_CUBE_IMAD_PHASE_MASK & 8)
#define MONA_CUBE_ADD_PHASE3(a, b, one) mona_cube_force_imad_u32((a), (b), (one))
#else
#define MONA_CUBE_ADD_PHASE3(a, b, one) ((a) + (b))
#endif
'''

def transform(src: dict[str, str]) -> dict[str, str]:
    out: dict[str, str] = {}
    helper = src['cuda_helper.h']
    # Runtime provides these declarations. The legacy host-pass declarations
    # can conflict with newer CUDA headers and are not needed by these kernels.
    helper = re.sub(r'^extern __device__ __device_builtin__ void __(?:syncthreads|threadfence)\(void\);\n', '', helper, flags=re.M)
    helper = replace_one(helper,
        '#ifndef __CUDA_ARCH__\n// define blockDim and threadIdx for host\nextern const dim3 blockDim;\nextern const uint3 threadIdx;\n#endif', '')
    out['cuda_helper.h'] = helper
    out['cuda_lyra2_vectors.h'] = src['lyra2/cuda_lyra2_vectors.h']

    # Keep the original CubeHash round arithmetic, not its 1024-thread policy.
    cube = through_function(src['Algo256/cuda_cubehash256.cu'], 'Final')
    cube = re.sub(r'^#define TPB(?:35|50)\s+\d+\s*$', '', cube, flags=re.M)
    cube = replace_one(cube,
        '__device__ __forceinline__ void rrounds(uint32_t x[2][2][2][2][2])',
        '__device__ __forceinline__ void rrounds(uint32_t x[2][2][2][2][2], uint32_t mona_one)')
    cube = rewrite_cube_round_adds_imad_phases(cube)
    cube = replace_one(cube,
        'void Final(uint32_t x[2][2][2][2][2], uint32_t *hashval)',
        'void Final(uint32_t x[2][2][2][2][2], uint32_t *hashval, uint32_t mona_one)')
    cube = replace_one(cube, 'for (int i = 0; i < 10; ++i) rrounds(x);',
        'for (int i = 0; i < 10; ++i) rrounds(x, mona_one);')
    cube = replace_one(cube, '#include "cuda_helper.h"\n',
        '#include "cuda_helper.h"\n\n' + CUBE_IMAD_PHASE_HELPER + '\n')
    out['upstream_cube.cuh'] = cube

    # One template per launch bound/fusion choice. Neither timing nor register
    # caps are inferred from old Maxwell/Pascal launch policies.
    blake = through_function(src['Algo256/cuda_blake256.cu'], 'blakeKeccak256_gpu_hash_80')
    blake = replace_one(blake, '__global__\nvoid blakeKeccak256_gpu_hash_80',
        'template<int Block, bool FuseCube>\n__global__ __launch_bounds__(Block)\nvoid blakeKeccak256_gpu_hash_80')
    blake = replace_one(blake,
        'const uint32_t threads, const uint32_t startNonce, uint32_t * Hash)',
        'const uint32_t threads, const uint32_t *noncePtr, uint32_t * Hash, uint32_t mona_one)')
    blake = replace_one(blake, 'const uint32_t nonce = startNonce + thread;',
        'const uint32_t nonce = *noncePtr + thread;')
    blake = replace_one(blake, 'keccak_block(keccak_gpu_state);',
        'keccak_block(keccak_gpu_state);\n\t\tif constexpr (FuseCube) mona_cube32(keccak_gpu_state, mona_one);')
    out['upstream_blake.cuh'] = blake

    skein = through_function(src['Algo256/cuda_skein256.cu'], 'skein256_gpu_hash_32')
    skein = replace_one(skein, '__global__ __launch_bounds__(256,3)',
        'template<int Block, bool FuseCube>\n__global__ __launch_bounds__(Block)')
    skein = replace_one(skein,
        'void skein256_gpu_hash_32(uint32_t threads, uint32_t startNounce, uint64_t *outputHash)',
        'void skein256_gpu_hash_32(uint32_t threads, uint32_t startNounce, uint64_t *outputHash, uint32_t mona_one)')
    skein = replace_one(skein, 'outputHash[thread]           = devectorize(p0);', '''if constexpr (FuseCube) {
            uint2 v[4] = {p0, p1, p2, p3};
            mona_cube32(v, mona_one);
            p0=v[0]; p1=v[1]; p2=v[2]; p3=v[3];
        }
        outputHash[thread]           = devectorize(p0);''')
    out['upstream_skein.cuh'] = skein

    bmw = through_function(src['Algo256/cuda_bmw256.cu'], 'Compression256_2')
    bmw = re.sub(r'^static uint32_t \*d_(?:gnounce|GNonce)\[MAX_GPUS\];\n', '', bmw, flags=re.M)
    bmw = replace_one(bmw, '__constant__ uint64_t pTarget[4];', '')
    out['upstream_bmw.cuh'] = bmw

    # SM>=500 arithmetic only. All old GPU fallbacks and host wrappers disappear.
    lyra = src['lyra2/cuda_lyra2v2.cu']
    lyra = lyra[lyra.index('#define Nrow 4'):]
    lyra = through_function(lyra, 'lyra2v2_gpu_hash_32_3')
    lyra = lyra.replace('__launch_bounds__(TPB, 1)', '__launch_bounds__(256)')
    # Same padded stride in scalar and 4-lane stages, including non-multiple
    # batches. Upstream's grid-derived stride is not identical at all tails.
    lyra = lyra.replace('blockDim.x * gridDim.x', 'mona_lyra_stride(threads)')
    lyra = lyra.replace('gridDim.x * blockDim.y', 'mona_lyra_stride(threads)')
    # Exact four-lane participation. No full-warp mask on a partial hash group.
    lyra = lyra.replace('__shfl(', 'mona_shfl4(')

    # Optional SM120-oriented software prefetch for reduceDuplexRowt2.
    # The hot matrix kernel is shared-memory/scoreboard limited. Pull rowOut
    # into registers before round_lyra_v5 so its LDS latency overlaps with
    # arithmetic. If rowOut aliases rowInOut, the value that the original code
    # would re-read after the rowInOut store is exactly the updated state2, so
    # select that register value to preserve semantics.
    lyra = replace_one(lyra,
        'uint2 state1[3], state2[3];\n\tconst uint32_t ps1 = memshift * Ncol * rowIn;',
        '''uint2 state1[3], state2[3];
#if defined(MONA_LYRA_PREFETCH_ROWOUT)
\tuint2 state3[3];
#endif
\tconst uint32_t ps1 = memshift * Ncol * rowIn;''')
    lyra = replace_one(lyra,
        '''\t\t#pragma unroll
\t\tfor (int j = 0; j < 3; j++)
\t\t\tstate2[j] = LD4S(s2 + j);

\t\t#pragma unroll
\t\tfor (int j = 0; j < 3; j++)
\t\t\tstate[j] ^= state1[j] + state2[j];''',
        '''\t\t#pragma unroll
\t\tfor (int j = 0; j < 3; j++)
\t\t\tstate2[j] = LD4S(s2 + j);

#if defined(MONA_LYRA_PREFETCH_ROWOUT)
\t\t#pragma unroll
\t\tfor (int j = 0; j < 3; j++)
\t\t\tstate3[j] = LD4S(s3 + j);
#endif

\t\t#pragma unroll
\t\tfor (int j = 0; j < 3; j++)
\t\t\tstate[j] ^= state1[j] + state2[j];''')
    lyra = replace_one(lyra,
        '''\t\t#pragma unroll
\t\tfor (int j = 0; j < 3; j++)
\t\t\tST4S(s3 + j, LD4S(s3 + j) ^ state[j]);''',
        '''\t\t#pragma unroll
\t\tfor (int j = 0; j < 3; j++)
#if defined(MONA_LYRA_PREFETCH_ROWOUT)
\t\t{
\t\t\tconst uint2 prior = (rowOut == rowInOut) ? state2[j] : state3[j];
\t\t\tST4S(s3 + j, prior ^ state[j]);
\t\t}
#else
\t\t\tST4S(s3 + j, LD4S(s3 + j) ^ state[j]);
#endif''')

    # Optional SM120 next-column software pipeline for normal wandering.
    # Keep reduceDuplexRowt2x4 and the rejected First-Read path untouched.
    lyra = rewrite_lyra_nextcol_prefetch(lyra)

    # Optional fixed-row first-read cache. The first wandering call always
    # reads rowIn=3. reduceDuplexRowSetup2 already owns the final row3 values
    # in registers before storing them to shared, so retain a register copy
    # only until that first call. Row3 remains in shared for all later dynamic
    # rowInOut accesses; unlike the full row3 regcache experiment this does
    # not change shared footprint or occupancy.
    lyra = replace_one(lyra,
        'void reduceDuplexRowSetup2(uint2 state[4])',
        '''void reduceDuplexRowSetup2(uint2 state[4]
#if defined(MONA_LYRA_ROW3_FIRSTREAD)
\t, mona_lyra_row3_firstread &row3_first
#endif
)''')
    lyra = replace_one(lyra,
        '''\t\t#pragma unroll
\t\tfor (j = 0; j < 3; j++)
\t\t\tST4S(s3 + j, state0[Ncol - i - 1][j]);''',
        '''\t\t#pragma unroll
\t\tfor (j = 0; j < 3; j++) {
#if defined(MONA_LYRA_ROW3_FIRSTREAD)
\t\t\trow3_first.v[Ncol - i - 1][j] = state0[Ncol - i - 1][j];
#endif
\t\t\tST4S(s3 + j, state0[Ncol - i - 1][j]);
\t\t}''')
    lyra = replace_one(lyra,
        '''\t\treduceDuplexRowSetup2(state);

\t\tuint32_t rowa;
\t\tint prev = 3;

\t\tfor (int i = 0; i < 3; i++)
\t\t{
\t\t\trowa = mona_shfl4(state[0].x, 0, 4) & 3;
\t\t\treduceDuplexRowt2(prev, rowa, i, state);
\t\t\tprev = i;
\t\t}

\t\trowa = mona_shfl4(state[0].x, 0, 4) & 3;''',
        '''#if defined(MONA_LYRA_ROW3_FIRSTREAD)
\t\tmona_lyra_row3_firstread row3_first;
\t\treduceDuplexRowSetup2(state, row3_first);

\t\tuint32_t rowa = mona_shfl4(state[0].x, 0, 4) & 3;
\t\tmona_reduceDuplexRowt2_firstread(rowa, state, row3_first);
\t\tint prev = 0;
\t\tfor (int i = 1; i < 3; i++)
\t\t{
\t\t\trowa = mona_shfl4(state[0].x, 0, 4) & 3;
\t\t\treduceDuplexRowt2(prev, rowa, i, state);
\t\t\tprev = i;
\t\t}
#else
\t\treduceDuplexRowSetup2(state);

\t\tuint32_t rowa;
\t\tint prev = 3;

\t\tfor (int i = 0; i < 3; i++)
\t\t{
\t\t\trowa = mona_shfl4(state[0].x, 0, 4) & 3;
\t\t\treduceDuplexRowt2(prev, rowa, i, state);
\t\t\tprev = i;
\t\t}
#endif

\t\trowa = mona_shfl4(state[0].x, 0, 4) & 3;''')
    shared_index = '(index * blockDim.y + threadIdx.y) * blockDim.x + threadIdx.x'
    if lyra.count(shared_index) != 2:
        raise RuntimeError('Expected exactly the LD4S/ST4S shared-index expressions')
    lyra = lyra.replace(shared_index, 'mona_lyra_shared_index(index)')
    lyra = rewrite_lyra_stable_members(lyra)
    out['upstream_lyra.cuh'] = lyra
    return out

def prepare(root: Path, dest: Path) -> None:
    src = {name: load_checked(root, name) for name in BLOBS}
    outputs = transform(src)
    dest.mkdir(parents=True, exist_ok=True)
    provenance = f'// Derived from tpruvot/ccminer windows@{BASE}.\n// See upstream LICENSE.txt and per-file notices. Generated; do not hand edit.\n'
    for name, text in outputs.items():
        (dest / name).write_text(provenance + text, encoding='utf-8', newline='\n')
    (dest / 'UPSTREAM_LICENSE.txt').write_bytes((root / 'LICENSE.txt').read_bytes())
    (dest / 'manifest.json').write_text(json.dumps({'base': BASE, 'blobs': BLOBS}, indent=2)+'\n', encoding='utf-8')
    print(f'Prepared {len(outputs)} files from verified upstream blobs.')

if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--upstream', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    prepare(a.upstream.resolve(), a.out.resolve())
