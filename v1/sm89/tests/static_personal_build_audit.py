#!/usr/bin/env python3
"""Bounded source-policy heuristic, not a source/binary/Defender release gate.

Stage 1, 2026-09-19: preserve literals when masking comments, reject missing
selected inputs, and report only files actually read. No miner code is changed.
"""
from pathlib import Path
import json, os, re, sys

root = Path(__file__).resolve().parents[1]
source = root/'source'
upstream = source/'pinned-upstream'
cmake = source/'CMakeLists.txt'
layout = 'source-package'
# The current SM89 authority preserves its original integration recipe under
# ORIGINAL_CMakeLists.txt and stores pinned-upstream as a sibling. Read that
# documented layout; do not reconstruct or mutate the miner build here.
if not source.exists() and (root/'ORIGINAL_CMakeLists.txt').is_file():
    source = root
    upstream = root.parent/'pinned-upstream'
    cmake = root/'ORIGINAL_CMakeLists.txt'
    layout = 'sm89-authority-archive'


# Include local headers as well as translation units. Checked-in generated files
# are a reference snapshot; this does NOT prove the configured build's closure.
scopes = [source/'phasec-host', source/'certified-backend/src',
          source/'certified-backend/include/mona', source/'certified-backend/generated',
          upstream/'compat/jansson']
source_suffixes = {'.c', '.cpp', '.cu', '.cuh', '.h', '.hpp'}
files = set()
findings = []

def relative(path):
    return Path(os.path.relpath(path, root)).as_posix()

for directory in scopes:
    selected = {p for p in directory.rglob('*') if p.is_file() and p.suffix in source_suffixes}
    if not selected:
        findings.append({'kind': 'missing_input', 'file': relative(directory),
                         'match': 'required audit scope is missing or empty'})
    files.update(selected)

# Explicit mandatory runtime inputs prevent a missing file from vanishing from
# a glob. This fixed inventory is bounded to the supplied Stable host/backend.
for directory, names in [
    (source/'phasec-host/src', ['main.cpp', 'miner_worker.cpp', 'stratum_client_win.cpp', 'phasec_core.cpp']),
    (source/'phasec-host/include', ['miner_worker.hpp', 'stratum_client.hpp', 'phasec_core.hpp', 'console_ui.hpp']),
    (source/'certified-backend/src', ['pipeline.cu', 'miner.cpp', 'build_identity.cpp', 'reference.cpp', 'cube.cuh', 'lyra.cuh']),
    (source/'certified-backend/include/mona', ['core.hpp', 'miner.hpp', 'measured_defaults.hpp']),
    (source/'certified-backend/generated', ['cuda_helper.h', 'cuda_lyra2_vectors.h', 'upstream_blake.cuh',
        'upstream_bmw.cuh', 'upstream_cube.cuh', 'upstream_lyra.cuh', 'upstream_skein.cuh']),
    (upstream/'sph', ['sph_sha2.c', 'blake.c', 'keccak.c', 'cubehash.c', 'skein.c', 'bmw.c']),
    (upstream/'lyra2', ['Lyra2.c', 'Sponge.c']),
]:
    files.update(directory/name for name in names)

patterns = {
    'hardcoded_network_url': re.compile(r'https?://|stratum\+tcp://[^"\s]*[^/:]:(?:[0-9]+)', re.I),
    'fee_donation': re.compile(r'\b(dev\s*fee|developer\s*fee|donat(?:e|ion)|fee\s*percent)\b', re.I),
    'telemetry_update': re.compile(r'\b(telemetry|analytics|auto.?update|update.?check)\b', re.I),
    'gpu_controls': re.compile(r'\b(nvmlDeviceSet\w*|NvAPI_GPU_Set\w*|power.?limit|voltage|fan.?speed|clock.?offset)\b', re.I),
}
# Match string/character/raw-string tokens BEFORE comment tokens. Mask only
# comments, retaining newlines so evidence line numbers remain source-relative.
# This is not a C++ preprocessor: macros, concatenation and encoded strings still
# require independent Stage 2 source review and final binary inspection.
cpp_tokens = re.compile(
    r'(?P<raw>(?:u8|u|U|L)?R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=delimiter)")'
    r'|(?P<quoted>"(?:\\[\s\S]|[^"\\\r\n])*"|\'(?:\\[\s\S]|[^\'\\\r\n])*\')'
    r'|(?P<comment>//[^\r\n]*|/\*.*?\*/)', re.S)

def without_comments(text):
    return cpp_tokens.sub(lambda m: re.sub(r'[^\r\n]', ' ', m.group())
                          if m.lastgroup == 'comment' else m.group(), text)

def read_input(path):
    if not path.is_file():
        findings.append({'kind': 'missing_input', 'file': relative(path), 'match': 'required file is missing'})
        return None
    try:
        return path.read_text(encoding='utf-8-sig')
    except (OSError, UnicodeError) as exc:
        findings.append({'kind': 'unreadable_input', 'file': relative(path), 'match': str(exc)})
        return None

scanned = []
for path in sorted(files):
    text = read_input(path)
    if text is None:
        continue
    scanned.append(relative(path))
    scan = without_comments(text)
    for name, rx in patterns.items():
        for match in rx.finditer(scan):
            value = match.group(0)
            # Scheme-only parser/usage syntax is not a configured endpoint.
            if name == 'hardcoded_network_url' and value.lower().startswith('stratum+tcp://') and (
                    'host:port' in value.lower() or value == 'stratum+tcp://'):
                continue
            findings.append({'kind': name, 'file': relative(path),
                             'line': scan.count('\n', 0, match.start()) + 1, 'match': value[:120]})

# A textual check of the declared integration CMake file, not a proof of the
# transitive/generated CMake target graph. Full build-closure audit is Stage 2.
cm = read_input(cmake)
if cm is not None:
    for forbidden in ['ccminer.cpp', 'util.cpp', 'pools.cpp', 'api.cpp', 'nvml.cpp', 'nvapi.cpp', 'nvsettings.cpp']:
        if re.search(r'(?<![A-Za-z0-9_])' + re.escape(forbidden) + r'(?![A-Za-z0-9_])', cm):
            findings.append({'kind': 'legacy_host_compiled', 'file': relative(cmake), 'match': forbidden})

result = {'status': 'PASS' if not findings else 'FAIL', 'audited_files': len(scanned),
          'files_scanned': scanned, 'findings': findings, 'layout': layout, 'cmake_checked': relative(cmake),
          'release_gate': False,
          'note': 'Bounded source-policy heuristic only. No complete secret scan, configured build-closure '
                  'proof, binary audit, Defender scan, or physical qualification is implied.'}
out = root/'evidence/static-personal-build-audit.json'
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print(json.dumps(result, indent=2))
sys.exit(0 if not findings else 1)
