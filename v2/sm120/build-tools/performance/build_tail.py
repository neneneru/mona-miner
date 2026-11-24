"""Compile the experimental tail independently of the host compiler."""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path


# NVRTC supplies the CUDA vector types and intrinsics. The generated device
# headers include some host headers even when compiling just the kernels.
INTEGER_TYPES = """
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef signed char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int64_t;
#define UINT32_MAX 0xffffffffU
"""
HEADERS = {
    'stdint.h': '#pragma once\n' + INTEGER_TYPES,
    'cuda.h': '#pragma once\n#define CUDA_VERSION 13040\n',
    'cuda_runtime.h': '#pragma once\n',
    'stdbool.h': '#pragma once\n',
    'stdio.h': '#pragma once\n',
    'memory.h': '#pragma once\n',
}


def compile_image(lib, code, flags, label):
    program = C.c_void_p()
    lib.nvrtcCreateProgram.argtypes = [C.POINTER(C.c_void_p), C.c_char_p, C.c_char_p, C.c_int,
                                      C.POINTER(C.c_char_p), C.POINTER(C.c_char_p)]
    lib.nvrtcCompileProgram.argtypes = [C.c_void_p, C.c_int, C.POINTER(C.c_char_p)]
    lib.nvrtcDestroyProgram.argtypes = [C.POINTER(C.c_void_p)]
    for name in ('nvrtcGetProgramLogSize', 'nvrtcGetCUBINSize'):
        getattr(lib, name).argtypes = [C.c_void_p, C.POINTER(C.c_size_t)]
    for name in ('nvrtcGetProgramLog', 'nvrtcGetCUBIN'):
        getattr(lib, name).argtypes = [C.c_void_p, C.c_void_p]

    def check(rc):
        if rc:
            raise RuntimeError(f'NVRTC status {rc}')

    keys = (C.c_char_p * len(HEADERS))(*(key.encode() for key in HEADERS))
    values = (C.c_char_p * len(HEADERS))(*(value.encode() for value in HEADERS.values()))
    check(lib.nvrtcCreateProgram(C.byref(program), code, b'cube2_bmw.cu', len(keys), values, keys))
    try:
        opts = (C.c_char_p * len(flags))(*(option.encode() for option in flags))
        status = lib.nvrtcCompileProgram(program, len(opts), opts)
        size = C.c_size_t()
        check(lib.nvrtcGetProgramLogSize(program, C.byref(size)))
        log = C.create_string_buffer(size.value)
        check(lib.nvrtcGetProgramLog(program, log))
        label.with_suffix('.log').write_text(log.value.decode(errors='replace'), encoding='utf-8')
        if status:
            raise RuntimeError(log.value.decode(errors='replace'))
        check(lib.nvrtcGetCUBINSize(program, C.byref(size)))
        cubin = C.create_string_buffer(size.value)
        check(lib.nvrtcGetCUBIN(program, cubin))
        return cubin.raw
    finally:
        check(lib.nvrtcDestroyProgram(C.byref(program)))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sm120', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    args.out.mkdir()
    code = Path(__file__).with_name('cube2_bmw.cu').read_bytes()
    sources = args.sm120.resolve() / 'device-source' / 'R02_CUBE2_TAIL_FUSION'
    base = ['--gpu-architecture=sm_120', '--std=c++17', '--device-as-default-execution-space',
            '--ptxas-options=-v', f'-I{sources / "src"}', f'-I{sources / "generated"}']
    sdk = Path(os.environ['CUDA_PATH']) / 'bin' / 'x64'
    records = {}
    with os.add_dll_directory(str(sdk)):
        lib = C.CDLL(str(sdk / 'nvrtc64_130_0.dll'))
        for block, mask in ((128, 15), (256, 15), (256, 0)):
            name = f'cube2-bmw-{block}-imad{mask}'
            flags = [*base, f'-DMONA_EXPERIMENT_BLOCK={block}', f'-DMONA_CUBE_IMAD_PHASE_MASK={mask}']
            image = compile_image(lib, code, flags, args.out / name)
            (args.out / (name + '.cubin')).write_bytes(image)
            records[name] = {'block': block, 'imad_mask': mask, 'sha256': hashlib.sha256(image).hexdigest()}
            print(json.dumps({name: records[name]}), flush=True)
    (args.out / 'manifest.json').write_text(json.dumps(records, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
