"""Offline v2 GPU correctness and paired performance trials. No miner or network."""
import argparse
from contextlib import contextmanager
import ctypes as C
import hashlib
import json
from pathlib import Path
import random
import statistics
import struct
import subprocess
import time
from typing import NamedTuple

P, I, U, Z, D = C.c_void_p, C.c_int, C.c_uint, C.c_size_t, C.c_uint64
PP = C.POINTER(P)
DI = C.POINTER(D)
FIRST = 'n02_blake_job_columns_b64'
MIDDLE = 'r02_cube1_l2_skein_cube2_b64'
MOVED_MIDDLE = 'r02_cube1_l2_skein_b64'
BMW_PREFIX = '_ZN40_GLOBAL__N__0bfffb84_8_entry_cu_a00f0cca15mona_bmw_kernelILi256ELb'
BMW_SUFFIX = 'EEEvjPKjPKyyPNS_16DeviceCandidatesEPy'
IMAGES = {
    'N02_EXP01_PRODUCER': '9463bb3d1242371f7ebaef19aa528b63debeb6fdc2fc900f8f1a0d8e7d85ec19',
    'R02_ACCEPTED': 'c9a9a6872df92f529b88cf6a80eabb925b515c76a1c4a3791788a52afb7e8a84',
    'R02_CUBE2_TAIL_FUSION': '84f3b1b105a7cb4df7dec50968e3ea81e2c4a0c97f83afe4872e413376f5e2bd',
}
GUARD = 256
HIT_BYTES = 264
NATIVE_BATCH = 1_048_576


class Route(NamedTuple):
    first: P | None
    middle: P
    tail: P | None
    tail_debug: P | None
    tail_block: int
    tail_one: bool
    middle_block: int
    shared: int
    prefix: P | None = None
    prefix_block: int = 0
    middle_width: int = 1
    prefix_width: int = 1
    prefix_sponge: bool = True
    tail_width: int = 1

    @property
    def uses_sponge(self):
        return self.prefix is not None and self.prefix_sponge


def sponge_bytes(n):
    return ((n + 31) // 32) * 32 * 16 * 8


def digest(data):
    return hashlib.sha256(data).hexdigest()


class Driver:
    def __init__(self, device):
        lib = C.WinDLL('nvcuda.dll')
        defs = {
            'Init': [U], 'DeviceGet': [C.POINTER(I), I],
            'DeviceGetName': [P, I, I], 'DeviceGetAttribute': [C.POINTER(I), I, I],
            'DevicePrimaryCtxRetain': [PP, I], 'DevicePrimaryCtxRelease_v2': [I],
            'CtxSetCurrent': [P], 'GetErrorName': [I, C.POINTER(C.c_char_p)],
            'ModuleLoadData': [PP, P], 'ModuleUnload': [P],
            'ModuleGetFunction': [PP, P, C.c_char_p], 'ModuleGetGlobal_v2': [DI, C.POINTER(Z), P, C.c_char_p],
            'FuncGetAttribute': [C.POINTER(I), I, P],
            'OccupancyMaxActiveBlocksPerMultiprocessor': [C.POINTER(I), P, I, Z],
            'StreamCreate': [PP, U], 'StreamDestroy_v2': [P], 'StreamSynchronize': [P],
            'MemAlloc_v2': [DI, Z], 'MemFree_v2': [D], 'MemAllocHost_v2': [PP, Z], 'MemFreeHost': [P],
            'MemcpyHtoD_v2': [D, P, Z], 'MemcpyDtoH_v2': [P, D, Z],
            'MemcpyHtoDAsync_v2': [D, P, Z, P], 'MemcpyDtoHAsync_v2': [P, D, Z, P],
            'MemsetD8Async': [D, C.c_ubyte, Z, P],
            'LaunchKernel': [P, U, U, U, U, U, U, U, P, PP, PP],
            'StreamBeginCapture_v2': [P, I], 'StreamEndCapture': [P, PP],
            'GraphInstantiateWithFlags': [PP, P, D], 'GraphLaunch': [P, P],
            'GraphDestroy': [P], 'GraphExecDestroy': [P],
            'EventCreate': [PP, U], 'EventRecord': [P, P], 'EventDestroy_v2': [P],
            'EventElapsedTime': [C.POINTER(C.c_float), P, P],
        }
        self.api = {}
        for name, args in defs.items():
            fn = getattr(lib, 'cu' + name)
            fn.argtypes = args
            fn.restype = I
            self.api[name] = fn
        self.run('Init', 0)
        self.device = I()
        self.run('DeviceGet', C.byref(self.device), device)
        major, minor = I(), I()
        self.run('DeviceGetAttribute', C.byref(major), 75, self.device)
        self.run('DeviceGetAttribute', C.byref(minor), 76, self.device)
        if (major.value, minor.value) != (12, 0):
            raise ValueError('This benchmark uses the repository SM120 images')
        name, self.context = C.create_string_buffer(128), P()
        self.run('DeviceGetName', name, len(name), self.device)
        self.name = name.value.decode()
        self.run('DevicePrimaryCtxRetain', C.byref(self.context), self.device)
        self.run('CtxSetCurrent', self.context)
        self.modules = []

    def run(self, operation, *args):
        status = self.api[operation](*args)
        if status:
            error = C.c_char_p()
            self.api['GetErrorName'](status, C.byref(error))
            raise RuntimeError(f'cu{operation}: {error.value!r} ({status})')

    def module(self, path, expected):
        image = path.read_bytes()
        if digest(image) != expected:
            raise ValueError(f'Unexpected image fingerprint: {path.name}')
        module = P()
        self.run('ModuleLoadData', C.byref(module), C.create_string_buffer(image))
        self.modules.append(module)
        return module

    def function(self, module, name):
        value = P()
        self.run('ModuleGetFunction', C.byref(value), module, name.encode())
        return value

    def resources(self, function, block, shared):
        result = {}
        for name, attr in [('registers', 4), ('local_bytes', 3), ('static_shared', 1), ('binary_version', 6)]:
            value = I()
            self.run('FuncGetAttribute', C.byref(value), attr, function)
            result[name] = value.value
        blocks = I()
        self.run('OccupancyMaxActiveBlocksPerMultiprocessor', C.byref(blocks), function, block, shared)
        return dict(result, blocks_per_sm=blocks.value, block=block, dynamic_shared=shared)

    def to_global(self, module, name, values):
        ptr, size = D(), Z()
        self.run('ModuleGetGlobal_v2', C.byref(ptr), C.byref(size), module, name.encode())
        raw = struct.pack(f'<{len(values)}I', *values)
        if size.value != len(raw):
            raise ValueError(f'Unexpected global size for {name}')
        self.run('MemcpyHtoD_v2', ptr, C.create_string_buffer(raw), len(raw))

    def close(self):
        for m in reversed(self.modules):
            self.run('ModuleUnload', m)
        self.run('CtxSetCurrent', None)
        self.run('DevicePrimaryCtxRelease_v2', self.device)


class Slot:
    def __init__(self, cu, n, split_sponge=False):
        self.cu, self.n = cu, n
        self.stream, self.pinned = P(), P()
        self.buffers = []
        cu.run('StreamCreate', C.byref(self.stream), 1)
        cu.run('MemAllocHost_v2', C.byref(self.pinned), HIT_BYTES + 4)
        self.hash = self.allocate(n * 32)
        self.upper = self.allocate(n * 8)
        self.hits = self.allocate(HIT_BYTES)
        self.nonce = self.allocate(4)
        self.sponge = self.allocate(sponge_bytes(n)) if split_sponge else None
        self.graphs = {}

    def allocate(self, size):
        ptr = D()
        self.cu.run('MemAlloc_v2', C.byref(ptr), size + 2 * GUARD)
        self.buffers.append((ptr.value, size))
        return ptr.value + GUARD

    def set_canaries(self):
        for base, length in self.buffers:
            self.cu.run('MemsetD8Async', base, 0xa5, length + 2 * GUARD, self.stream)

    def get(self, address, size):
        output = C.create_string_buffer(size)
        self.cu.run('MemcpyDtoH_v2', output, address, size)
        return output.raw

    def check_canaries(self, n):
        buffers = [(self.nonce, 4), (self.hash, n*32), (self.upper, n*8), (self.hits, HIT_BYTES)]
        if self.sponge is not None:
            buffers.append((self.sponge, sponge_bytes(n)))
        for ptr, length in buffers:
            expected = b'\xa5' * GUARD
            if self.get(ptr-GUARD, GUARD) != expected or self.get(ptr+length, GUARD) != expected:
                raise ValueError('GPU buffer guard mismatch')

    def kernel(self, function, block, shared, n, *values):
        argv = (P * len(values))(*(C.cast(C.byref(value), P) for value in values))
        self.cu.run('LaunchKernel', function, (n+block-1)//block, 1, 1, block, 1, 1, shared,
                    self.stream, argv, None)

    def enqueue(self, route, n, target, debug=False, events=()):
        cu, stream = self.cu, self.stream
        if events:
            cu.run('EventRecord', events[0], stream)
        cu.run('MemcpyHtoDAsync_v2', self.nonce, self.pinned.value, 4, stream)
        cu.run('MemsetD8Async', self.hits, 0, HIT_BYTES, stream)
        if route.first is not None:
            self.kernel(route.first, 64, 0, n, U(n), D(self.nonce), D(self.hash), U(1))
        if events:
            cu.run('EventRecord', events[1], stream)
        middle_args = [U(n), D(self.hash), U(1)]
        if route.prefix is not None:
            self.kernel(route.prefix, route.prefix_block, 0, n * route.prefix_width,
                        U(n), D(self.hash), D(self.sponge if route.uses_sponge else 0), U(1))
        if route.uses_sponge:
            middle_args.append(D(self.sponge))
        if route.first is None or route.tail is None:
            middle_args.append(D(self.nonce))
        if route.tail is None:
            middle_args.extend((D(target), D(self.hits), D(self.upper if debug else 0)))
        self.kernel(route.middle, route.middle_block, route.shared, n * route.middle_width, *middle_args)
        if events:
            cu.run('EventRecord', events[2], stream)
        if route.tail is not None:
            args = [U(n), D(self.nonce), D(self.hash), D(target), D(self.hits), D(self.upper if debug else 0)]
            if route.tail_one:
                args.append(U(1))
            self.kernel(route.tail_debug if debug else route.tail, route.tail_block, 0, n * route.tail_width, *args)
        cu.run('MemcpyDtoHAsync_v2', self.pinned.value+4, self.hits, HIT_BYTES, stream)
        if events:
            cu.run('EventRecord', events[3], stream)

    def graph(self, key, route, n, target, debug=False):
        graph, executable = P(), P()
        self.cu.run('StreamBeginCapture_v2', self.stream, 0)
        try:
            self.enqueue(route, n, target, debug)
        finally:
            self.cu.run('StreamEndCapture', self.stream, C.byref(graph))
        try:
            self.cu.run('GraphInstantiateWithFlags', C.byref(executable), graph, 0)
        finally:
            self.cu.run('GraphDestroy', graph)
        self.graphs[key] = executable
        return executable

    def launch(self, route, first, n, target, *, debug=False, graph=None, events=()):
        U.from_address(self.pinned.value).value = first
        if graph is None:
            self.enqueue(route, n, target, debug, events)
        else:
            self.cu.run('GraphLaunch', graph, self.stream)

    def collect(self):
        self.cu.run('StreamSynchronize', self.stream)
        return struct.unpack('<66I', C.string_at(self.pinned.value + 4, HIT_BYTES))

    def scan(self, route, first, n, target, **options):
        self.launch(route, first, n, target, **options)
        return self.collect()

    def close(self):
        self.cu.run('StreamSynchronize', self.stream)
        for graph in self.graphs.values():
            self.cu.run('GraphExecDestroy', graph)
        for ptr, _ in reversed(self.buffers):
            self.cu.run('MemFree_v2', ptr)
        self.cu.run('MemFreeHost', self.pinned)
        self.cu.run('StreamDestroy_v2', self.stream)


def install(cu, producer_modules, case):
    for module in producer_modules:
        cu.to_global(module, 'cpu_h', case['h'])
        cu.to_global(module, 'c_data', case['data'])
        cu.to_global(module, 'n02_job_columns', case['record'])


def routes(cu, args, fixture):
    mods = {name: cu.module(args.sm120 / 'images' / (name + '.cubin'), sha) for name, sha in IMAGES.items()}
    for module in mods.values():
        cu.to_global(module, 'u256', fixture['u256'])
        cu.to_global(module, 'sigma', [value for row in fixture['sigma'] for value in row])
    first_model = mods['N02_EXP01_PRODUCER']
    first = cu.function(first_model, FIRST)
    mid_model, bmw_model = mods['R02_CUBE2_TAIL_FUSION'], mods['R02_ACCEPTED']
    mid = cu.function(mid_model, MIDDLE)
    bmw = cu.function(bmw_model, BMW_PREFIX + '0' + BMW_SUFFIX)
    bmw_debug = cu.function(bmw_model, BMW_PREFIX + '1' + BMW_SUFFIX)
    producers = [first_model]
    result = {'baseline': Route(first, mid, bmw, bmw_debug, 256, False, 64, 24576)}
    if args.candidates:
        manifest = json.loads((args.candidates / 'manifest.json').read_text())
        move = cu.function(mid_model, MOVED_MIDDLE)
        for label, r in manifest.items():
            module = cu.module(args.candidates / (label + '.cubin'), r['sha256'])
            result[label] = Route(first, move, cu.function(module, 'mona_cube2_bmw'),
                                  cu.function(module, 'mona_cube2_bmw_debug'), r['block'], True, 64, 24576)
    for directory in args.middle_images:
        manifest = json.loads((directory / 'middle-manifest.json').read_text())
        for label, spec in manifest.items():
            if label in result:
                raise ValueError(f'Duplicate route label: {label}')
            module = cu.module(directory / (label + '.cubin'), spec['sha256'])
            producer = first
            if spec.get('fused_head'):
                producer = None
                producers.append(module)
            elif spec.get('producer'):
                producer = cu.function(module, FIRST)
                producers.append(module)
            tail_block = spec.get('tail_block', 0)
            tail, tail_debug = bmw, bmw_debug
            if spec.get('complete'):
                tail, tail_debug = None, None
            elif tail_block:
                tail = cu.function(module, 'mona_cube2_bmw')
                tail_debug = cu.function(module, 'mona_cube2_bmw_debug')
            prefix_block = spec.get('prefix_block', 0)
            prefix = cu.function(module, 'mona_lyra_prefix') if prefix_block else None
            result[label] = Route(producer, cu.function(module, MIDDLE), tail, tail_debug,
                                  tail_block or 256, bool(tail_block), spec['block'], spec['shared'],
                                  prefix, prefix_block, spec.get('middle_width', 1),
                                  spec.get('prefix_width', 1), spec.get('prefix_sponge', True),
                                  spec.get('tail_width', 1))
    return producers, result


def compare_hits(hits, expected, first, target):
    total, overflow = hits[:2]
    exact = {first + j for j, value in enumerate(expected) if value <= target}
    found = hits[2:2+min(total,64)]
    if total != len(exact) or overflow != int(total > 64):
        raise ValueError(f'Candidate header mismatch: {total=} {overflow=} expected {len(exact)}')
    if len(set(found)) != len(found) or not set(found) <= exact:
        raise ValueError('Duplicate, out of range, or unexpected nonce')


def prepare_ring(slots, label, route, step, totals, target, debug=False):
    sizes = {step, *(total % step for total in totals)} - {0}
    graphs = []
    for slot in slots:
        by_size = {}
        for size in sizes:
            key = ('ring', label, size, target, debug)
            if key not in slot.graphs:
                slot.graph(key, route, size, target, debug)
            by_size[size] = slot.graphs[key]
        graphs.append(by_size)
    return graphs


def scan_ring(slots, graphs, route, first, total, step, target, check=None):
    depth = len(slots)
    chunks = (total + step - 1) // step

    def collect(index):
        member = slots[index % depth]
        hits = member.collect()
        if check is not None:
            offset = index * step
            check(member, offset, min(step, total - offset), hits)

    for index, offset in enumerate(range(0, total, step)):
        member = index % depth
        if index >= depth:
            collect(index - depth)
        count = min(step, total - offset)
        slots[member].launch(route, first + offset, count, target, graph=graphs[member][count])
    for index in range(max(0, chunks - depth), chunks):
        collect(index)


def validate_ring(cu, modules, route, cases, depth):
    cases = sorted(cases, key=lambda case: case['count'])[-2:]
    if len(cases) < 2 or cases[0]['count'] < 8:
        raise ValueError('Need two CPU oracle cases of at least eight nonces to validate ring reuse')
    # Force slot reuse, a partial final chunk, and a nonce near UINT32_MAX.
    step = (cases[-1]['count'] - 1) // (depth * 2)
    if step < 1:
        raise ValueError('Need a larger CPU oracle case to validate ring slot reuse')
    slots = []
    try:
        for _ in range(depth):
            slots.append(Slot(cu, step, route.uses_sponge))
        for index, case in enumerate(cases):
            start, total, expected = case['first'], case['count'], case['upper']
            target = (1 << 64) - 1 if index == 0 else sorted(expected)[7]
            install(cu, modules, case)
            graphs = prepare_ring(slots, 'validation', route, step, (total,), target, debug=True)
            for slot in slots:
                slot.set_canaries()

            def check(slot, offset, count, hits):
                segment = expected[offset:offset + count]
                got = struct.unpack(f'<{count}Q', slot.get(slot.upper, count * 8))
                if list(got) != segment:
                    raise ValueError(f'CUDA Graph ring CPU oracle mismatch: {offset=} {count=}')
                slot.check_canaries(count)
                compare_hits(hits, segment, start + offset, target)
                slot.set_canaries()

            scan_ring(slots, graphs, route, start, total, step, target, check)
    finally:
        for slot in reversed(slots):
            slot.close()
    return dict(result='PASS', depth=depth, full_chunk=step, vector_cases=len(cases),
                cpu_upper_comparisons=sum(case['count'] for case in cases))


def validate(cu, module, all_routes, fixture, graph_label):
    count = max(case['count'] for case in fixture['cases'])
    split_sponge = any(route.uses_sponge for route in all_routes.values())
    slot = Slot(cu, count, split_sponge)
    second = Slot(cu, count, split_sponge)
    graph_route = all_routes[graph_label]
    comparisons = 0
    try:
        for case in fixture['cases']:
            n, first, expected = case['count'], case['first'], case['upper']
            install(cu, module, case)
            # Small nonoverflow hit-set and a forced overflow. The benchmark
            # also covers graph replay after changing the nonce at a fixed shape.
            threshold = sorted(expected)[min(n-1, 7)]
            for label, route in all_routes.items():
                for target in (threshold, (1 << 64)-1):
                    slot.set_canaries()
                    hits = slot.scan(route, first, n, target, debug=True)
                    got = struct.unpack(f'<{n}Q', slot.get(slot.upper, n*8))
                    if list(got) != expected:
                        raise ValueError(f'{label} failed the CPU oracle: {n=} {first=}')
                    slot.check_canaries(n)
                    compare_hits(hits, expected, first, target)
                    comparisons += n
            # Each graph is tied to this shape and threshold. Reuse the graph
            # with the original nonce after exercising a different launch.
            graph = slot.graph((n, 'debug'), graph_route, n, threshold, True)
            slot.set_canaries()
            other = 0 if first else 1
            slot.scan(graph_route, other, n, threshold, debug=True, graph=graph)
            hits = slot.scan(graph_route, first, n, threshold, debug=True, graph=graph)
            got = struct.unpack(f'<{n}Q', slot.get(slot.upper, n*8))
            if list(got) != expected:
                raise ValueError(f'Graph replay failed the CPU oracle: {n=}')
            slot.check_canaries(n)
            compare_hits(hits, expected, first, threshold)
            comparisons += n
            # Dispatch two disjoint buffers at the same time. Partition the
            # known range so the two nonces are independently verifiable.
            if n > 1:
                split = n // 2
                both = ((slot, first, expected[:split]), (second, first+split, expected[split:]))
                for index, (member, start, segment) in enumerate(both):
                    member.set_canaries()
                    handle = member.graph((n, 'pair', index), graph_route, len(segment), threshold, True)
                    member.launch(graph_route, start, len(segment), threshold, debug=True, graph=handle)
                for member, start, segment in both:
                    result = member.collect()
                    size = len(segment)
                    got = struct.unpack(f'<{size}Q', member.get(member.upper, size*8))
                    if list(got) != segment:
                        raise ValueError(f'Concurrent graph failed the CPU oracle: {n=}')
                    member.check_canaries(size)
                    compare_hits(result, segment, start, threshold)
                    comparisons += size
    finally:
        second.close()
        slot.close()
    return dict(result='PASS', cpu_upper_comparisons=comparisons, vector_cases=len(fixture['cases']))


def telemetry(device):
    fields = ('power.limit', 'power.draw', 'temperature.gpu', 'utilization.gpu')
    output = subprocess.check_output(['nvidia-smi','-i',str(device),'--query-gpu=' + ','.join(fields),
                                      '--format=csv,noheader,nounits'], text=True)
    return dict(zip(fields, (float(value.strip()) for value in output.split(',')), strict=True))


@contextmanager
def events(cu):
    all_events = []
    try:
        for _ in range(4):
            event = P()
            cu.run('EventCreate', C.byref(event), 0)
            all_events.append(event)
        yield all_events
    finally:
        for event in all_events:
            cu.run('EventDestroy_v2', event)


def phase_times(slot, route, n):
    cu = slot.cu
    readings = []
    with events(cu) as markers:
        for i in range(10):
            slot.scan(route, i*n, n, 0, events=markers)
            sample = []
            for left, right in zip(markers, markers[1:]):
                value = C.c_float()
                cu.run('EventElapsedTime', C.byref(value), left, right)
                sample.append(value.value)
            readings.append(sample)
    return [statistics.median(column) for column in zip(*readings)]


def paired(cu, module, all_routes, fixture, args):
    durations = []
    graph_label = args.graph_route + '-graph'
    dual_label = args.graph_route + '-dual'
    graph_route = all_routes[args.graph_route]
    criteria = ['baseline', graph_label, dual_label,
                *(name for name in all_routes if name != 'baseline')]
    batch_variants = {f'{label}-batch{size}': (label, size) for label in args.batch_route
                      for size in args.batch_variants}
    criteria.extend(batch_variants)
    rings = {f'{route}-ring{depth}x{step}': (route, depth, step, False)
             for route in args.ring_route for depth, step in args.ring}
    if args.compare_ring_drain:
        rings.update({f'{label}-drain': (route, depth, step, True)
                      for label, (route, depth, step, _) in rings.items()})
    criteria.extend(rings)
    rng = random.Random(157)
    install(cu, module, fixture['cases'][3])
    split = any(route.uses_sponge for route in all_routes.values())
    slot = Slot(cu, NATIVE_BATCH, split)
    second = Slot(cu, NATIVE_BATCH, split)
    ring_slots = []
    try:
        if args.only:
            unknown = set(args.only) - set(criteria)
            if unknown:
                raise ValueError(f'Unknown benchmark routes: {sorted(unknown)}')
            criteria = [label for label in criteria if label == 'baseline' or label in args.only]
        rings = {label: spec for label, spec in rings.items() if label in criteria}
        if rings:
            largest = max(step for _, _, step, _ in rings.values())
            needs_sponge = any(all_routes[route].uses_sponge for route, _, _, _ in rings.values())
            for _ in range(max(depth for _, depth, _, _ in rings.values())):
                ring_slots.append(Slot(cu, largest, needs_sponge))
        for n in args.batch:
            smaller = args.compare_dual_batch
            graph = slot.graph((n, 'timing'), graph_route, n, 0)
            dual_n = smaller or n
            dual_graph = graph if dual_n == n else slot.graph((dual_n, 'dual'), graph_route, dual_n, 0)
            second_graph = second.graph((dual_n, 'timing'), graph_route, dual_n, 0)
            stage_ms = {label: phase_times(slot, route, n) for label, route in all_routes.items()}
            batch_sizes = {label: size for label, (_, size) in batch_variants.items()}
            report = {'batch': n, 'dual_batch': dual_n, 'batch_variants': batch_sizes,
                      'phase_ms': stage_ms, 'trials': []}
            if rings:
                report['rings'] = rings
            ring_graphs = {
                label: prepare_ring(ring_slots[:depth], route, all_routes[route], step,
                                    (n,) if drain else (n * 10, n * args.repeats), 0)
                for label, (route, depth, step, drain) in rings.items()
            }
            if args.warmup_seconds:
                before = telemetry(args.device)
                start = 0
                deadline = time.perf_counter() + args.warmup_seconds
                while time.perf_counter() < deadline:
                    slot.scan(graph_route, start, n, 0, graph=graph)
                    start = (start + n) % ((1 << 32) // n * n)
                report['warmup'] = dict(seconds=args.warmup_seconds, before=before, after=telemetry(args.device))
            for trial in range(args.rounds):
                order = list(criteria)
                if trial % 2 == 0:
                    rng.shuffle(order)
                else:
                    order = list(reversed(previous))
                previous = order
                results = {}
                for label in order:
                    captured = label in (graph_label, dual_label)
                    is_dual = label == dual_label
                    ring = rings.get(label)
                    route_label, step = batch_variants.get(label, (label, n))
                    if ring:
                        route_label, depth, chunk, drain = ring
                    route = graph_route if captured else all_routes[route_label]
                    selected_graph = graph if captured else None

                    def run_batches(repeats):
                        if ring:
                            members, graphs = ring_slots[:depth], ring_graphs[label]
                            if drain:
                                for i in range(repeats):
                                    scan_ring(members, graphs, route, i * n, n, chunk, 0)
                            else:
                                scan_ring(members, graphs, route, 0, n * repeats, chunk, 0)
                        elif is_dual:
                            for i in range(0, repeats * (n // dual_n), 2):
                                slot.launch(route, i*dual_n, dual_n, 0, graph=dual_graph)
                                second.launch(route, (i+1)*dual_n, dual_n, 0, graph=second_graph)
                                slot.collect()
                                second.collect()
                        elif label in batch_variants:
                            whole, remainder = divmod(repeats * n, step)
                            for i in range(whole):
                                slot.scan(route, i * step, step, 0)
                            if remainder:
                                slot.scan(route, whole * step, remainder, 0)
                        else:
                            for i in range(repeats):
                                slot.scan(route, i*n, n, 0, graph=selected_graph)

                    run_batches(10)
                    before = telemetry(args.device)
                    now = time.perf_counter()
                    run_batches(args.repeats)
                    elapsed = time.perf_counter()-now
                    results[label] = dict(mhs=n*args.repeats/(elapsed*1e6), before=before, after=telemetry(args.device))
                report['trials'].append(results)
            report['paired_median_pct'] = {
                name: statistics.median(100*(x[name]['mhs']/x['baseline']['mhs']-1) for x in report['trials'])
                for name in criteria if name != 'baseline'
            }
            durations.append(report)
            print(json.dumps(report['paired_median_pct']), flush=True)
    finally:
        for member in reversed(ring_slots):
            member.close()
        second.close()
        slot.close()
    return durations


def ring_shape(value):
    try:
        depth, chunk = map(int, value.split('x'))
    except ValueError as exc:
        raise argparse.ArgumentTypeError('Use DEPTHxCHUNK, for example 3x304640') from exc
    if not 2 <= depth <= 4 or not 1 <= chunk <= NATIVE_BATCH:
        raise argparse.ArgumentTypeError('Use 2 to 4 slots and a chunk within the native batch limit')
    return depth, chunk


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sm120', type=Path, required=True)
    p.add_argument('--oracle', type=Path, required=True)
    p.add_argument('--candidates', type=Path)
    p.add_argument('--middle-images', type=Path, action='append', default=[])
    p.add_argument('--device', type=int, default=0)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--benchmark', action='store_true')
    p.add_argument('--batch', nargs='+', type=int, default=[262144, 524288, 1048576])
    p.add_argument('--compare-dual-batch', type=int)
    p.add_argument('--batch-route', action='append', default=[],
                   help='Route used with alternate batch sizes at equal total work. May be repeated.')
    p.add_argument('--batch-variants', nargs='+', type=int, default=[])
    p.add_argument('--ring', type=ring_shape, action='append', default=[],
                   help='Continuously replenish CUDA Graph slots, e.g. 3x304640')
    p.add_argument('--ring-route', action='append', default=[],
                   help='Route to run in the CUDA Graph ring. Defaults to --graph-route.')
    p.add_argument('--compare-ring-drain', action='store_true',
                   help='Also time the ring waiting for all work at each baseline batch boundary')
    p.add_argument('--graph-route', default='baseline', help='Use the selected route in Graph validation and timing')
    p.add_argument('--only', nargs='+', help='Limit benchmark routes (verification still tests all loaded routes)')
    p.add_argument('--rounds', type=int, default=4)
    p.add_argument('--repeats', type=int, default=900)
    p.add_argument('--warmup-seconds', type=int, default=0,
                   help='Keep the GPU busy before timing the first paired round')
    a = p.parse_args()
    if min(a.batch) < 1 or max(a.batch) > NATIVE_BATCH or a.rounds < 2 or a.repeats < 2 or a.repeats % 2 or max(a.batch)*a.repeats > 1<<32:
        p.error('Use an even number of repeats within the 32-bit nonce space')
    if a.warmup_seconds < 0:
        p.error('Warm-up time must be nonnegative')
    if a.compare_dual_batch is not None and (a.compare_dual_batch < 1 or any(n % a.compare_dual_batch for n in a.batch)):
        p.error('The dual comparison batch must evenly divide each baseline batch size')
    if a.batch_variants and (not a.batch_route or not a.benchmark or
                             any(n < 1 or n > NATIVE_BATCH for n in a.batch_variants)):
        p.error('Alternate batch sizes require --benchmark and a route, within the native batch limit')
    if a.benchmark and telemetry(a.device)['utilization.gpu'] >= 25:
        p.error('Other GPU activity detected. Close the other GPU workload for timed A/B or use the correctness mode.')
    fixture = json.loads(a.oracle.read_text())
    cu = Driver(a.device)
    try:
        module, all_routes = routes(cu, a, fixture)
        if a.graph_route not in all_routes:
            p.error(f'Graph route not loaded: {a.graph_route}')
        a.ring_route = list(dict.fromkeys(a.ring_route or [a.graph_route]))
        missing_ring_routes = set(a.ring_route) - all_routes.keys()
        if a.ring and missing_ring_routes:
            p.error(f'Ring comparison routes not loaded: {sorted(missing_ring_routes)}')
        missing_batch_routes = set(a.batch_route) - all_routes.keys()
        if a.batch_variants and missing_batch_routes:
            p.error(f'Batch comparison routes not loaded: {sorted(missing_batch_routes)}')
        res = {name: cu.resources(route.tail, route.tail_block, 0) if route.tail is not None else None
               for name, route in all_routes.items()}
        mid_res = {name: cu.resources(route.middle, route.middle_block, route.shared)
                   for name, route in all_routes.items()}
        prefix_res = {name: cu.resources(route.prefix, route.prefix_block, 0)
                      for name, route in all_routes.items() if route.prefix is not None}
        report = {'device': cu.name, 'tail_resources': res, 'middle_resources': mid_res,
                  'prefix_resources': prefix_res,
                  'correctness': validate(cu, module, all_routes, fixture, a.graph_route)}
        if a.ring:
            depth = max(depth for depth, _ in a.ring)
            report['ring_correctness'] = {route: validate_ring(cu, module, all_routes[route],
                                                              fixture['cases'], depth)
                                          for route in a.ring_route}
        if a.benchmark:
            report['benchmark'] = paired(cu, module, all_routes, fixture, a)
    finally:
        cu.close()
    a.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'validation': report['correctness'], 'tail_resources': res}))


if __name__ == '__main__':
    main()
