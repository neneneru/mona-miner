#!/usr/bin/env python3
"""Generate a non-self-referential build identity for MONA backend builds.

The identity intentionally excludes final executable hashes.  It binds the
source/generator inputs, generated CUDA sources, pinned upstream tree, selected
compile profile and compiler/toolchain identities.  Final EXE/DLL hashes are
bound later by tools/certify.py in a sidecar certification manifest.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
from pathlib import Path
from typing import Iterable

SCHEMA = "mona.build-identity.v1"
EXCLUDED_GENERATED = {"mona_build_identity.hpp", "mona-build-identity.json"}


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def canonical_bytes(value: object) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def norm_bool(value: str) -> str:
    v = value.strip().upper()
    if v in {"1", "ON", "TRUE", "YES"}:
        return "ON"
    if v in {"0", "OFF", "FALSE", "NO", ""}:
        return "OFF"
    raise ValueError(f"not a CMake boolean: {value!r}")


def file_record(root: Path, path: Path) -> dict:
    rel = path.relative_to(root).as_posix()
    return {"path": rel, "sha256": sha256_file(path), "bytes": path.stat().st_size}


def project_inputs(root: Path) -> list[dict]:
    paths: set[Path] = {root / "CMakeLists.txt", root / "tools" / "prepare.py", root / "tools" / "build_identity.py"}
    for directory, patterns in ((root / "src", ("*.cpp", "*.cu", "*.cuh", "*.c", "*.h")),
                                (root / "include" / "mona", ("*.hpp", "*.h"))):
        for pattern in patterns:
            paths.update(directory.glob(pattern))
    missing = [p for p in paths if not p.is_file()]
    if missing:
        raise FileNotFoundError(f"missing project build input: {missing[0]}")
    return [file_record(root, p) for p in sorted(paths, key=lambda p: p.as_posix())]


def generated_inputs(root: Path) -> list[dict]:
    if not root.is_dir():
        raise FileNotFoundError(f"generated source directory missing: {root}")
    files = [p for p in root.rglob("*") if p.is_file() and p.name not in EXCLUDED_GENERATED]
    if not files:
        raise ValueError("generated source directory is empty")
    return [file_record(root, p) for p in sorted(files, key=lambda p: p.as_posix())]


def git_output(args: list[str]) -> str:
    return subprocess.check_output(args, text=True, encoding="utf-8", errors="strict").strip()


def upstream_inputs(root: Path) -> tuple[str, list[dict]]:
    commit = git_output(["git", "-C", str(root), "rev-parse", "HEAD"])
    status = git_output(["git", "-C", str(root), "status", "--porcelain"])
    if status:
        raise ValueError("upstream checkout must be clean for build identity")
    raw = subprocess.check_output(["git", "-C", str(root), "ls-files", "-z"])
    names = [x.decode("utf-8") for x in raw.split(b"\0") if x]
    records = []
    for name in sorted(names):
        p = root / name
        if not p.is_file():
            raise FileNotFoundError(f"tracked upstream file missing: {name}")
        records.append(file_record(root, p))
    return commit, records


def binary_identity(path_text: str, version: str) -> dict:
    p = Path(path_text) if path_text else None
    out = {"version": version}
    if p and p.is_file():
        out.update({"name": p.name, "sha256": sha256_file(p), "bytes": p.stat().st_size})
    else:
        out.update({"name": Path(path_text).name if path_text else "", "sha256": None, "bytes": None})
    return out


def infer_ptxas(cuda_compiler: str) -> dict:
    if not cuda_compiler:
        return {"name": "ptxas", "sha256": None, "bytes": None}
    base = Path(cuda_compiler).resolve().parent
    candidates = [base / "ptxas.exe", base / "ptxas"]
    for p in candidates:
        if p.is_file():
            return {"name": p.name, "sha256": sha256_file(p), "bytes": p.stat().st_size}
    return {"name": "ptxas", "sha256": None, "bytes": None}


def classify_profile(settings: dict[str, str]) -> str:
    exact = {
        "MONA_SM": "120",
        "MONA_MAX_REGISTERS": "0",
        "MONA_LYRA_ACTIVE_MASK": "OFF",
        "MONA_LYRA_STABLE_BALLOT": "ON",
        "MONA_LYRA_FULL_MASK": "OFF",
        "MONA_LYRA_DUMMY_TAIL": "OFF",
        "MONA_LYRA_PREFETCH_ROWOUT": "ON",
        "MONA_LYRA_PREFETCH_NEXTCOL": "OFF",
        "MONA_LYRA_STATIC_SHARED64": "OFF",
        "MONA_LYRA_ROW3_FIRSTREAD": "OFF",
        "MONA_CUBE_IMAD_PHASE_MASK": "15",
    }
    if all(settings.get(k) == v for k, v in exact.items()):
        return "sm120-stable-base-v1"
    baseline = exact | {
        "MONA_LYRA_STABLE_BALLOT": "OFF",
        "MONA_LYRA_PREFETCH_ROWOUT": "OFF",
        "MONA_CUBE_IMAD_PHASE_MASK": "0",
    }
    if all(settings.get(k) == v for k, v in baseline.items()):
        return "sm120-all-off-reference-v1"
    return "custom-unqualified"


def make_payload(args: argparse.Namespace) -> dict:
    settings: dict[str, str] = {}
    for item in args.setting:
        if "=" not in item:
            raise ValueError(f"--setting requires NAME=VALUE: {item}")
        k, v = item.split("=", 1)
        if k.startswith("MONA_LYRA_"):
            v = norm_bool(v)
        settings[k] = v
    upstream_commit, upstream_files = upstream_inputs(args.upstream)
    payload = {
        "upstream_commit": upstream_commit,
        "project_files": project_inputs(args.project_root),
        "generated_files": generated_inputs(args.generated),
        "upstream_files": upstream_files,
        "settings": dict(sorted(settings.items())),
        "toolchain": {
            "cmake_version": args.cmake_version,
            "generator": args.generator,
            "build_type": args.build_type,
            "cuda_compiler": binary_identity(args.cuda_compiler, args.cuda_version),
            "ptxas": infer_ptxas(args.cuda_compiler),
            "cxx_compiler": binary_identity(args.cxx_compiler, f"{args.cxx_id} {args.cxx_version}".strip()),
        },
    }
    return payload


def write_outputs(payload: dict, out_json: Path, out_header: Path) -> dict:
    build_id = hashlib.sha256(canonical_bytes(payload)).hexdigest()
    profile = classify_profile(payload["settings"])
    doc = {"schema": SCHEMA, "build_id": build_id, "profile": profile, "payload": payload}
    out_json.parent.mkdir(parents=True, exist_ok=True)
    out_header.parent.mkdir(parents=True, exist_ok=True)
    out_json.write_text(json.dumps(doc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    header = (
        "#pragma once\n"
        "namespace mona_build_identity {\n"
        f'inline constexpr char kBuildId[] = "{build_id}";\n'
        f'inline constexpr char kBuildProfile[] = "{profile}";\n'
        f'inline constexpr char kSchema[] = "{SCHEMA}";\n'
        "}\n"
    )
    out_header.write_text(header, encoding="utf-8", newline="\n")
    return doc


def verify_doc(doc: dict) -> None:
    if doc.get("schema") != SCHEMA:
        raise ValueError("unknown build identity schema")
    payload = doc.get("payload")
    if not isinstance(payload, dict):
        raise ValueError("build identity payload missing")
    expected = hashlib.sha256(canonical_bytes(payload)).hexdigest()
    if doc.get("build_id") != expected:
        raise ValueError("build identity digest mismatch")
    if doc.get("profile") != classify_profile(payload.get("settings", {})):
        raise ValueError("build profile classification mismatch")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--project-root", type=Path, required=True)
    ap.add_argument("--upstream", type=Path, required=True)
    ap.add_argument("--generated", type=Path, required=True)
    ap.add_argument("--out-json", type=Path, required=True)
    ap.add_argument("--out-header", type=Path, required=True)
    ap.add_argument("--setting", action="append", default=[])
    ap.add_argument("--cuda-compiler", default="")
    ap.add_argument("--cuda-version", default="")
    ap.add_argument("--cxx-compiler", default="")
    ap.add_argument("--cxx-id", default="")
    ap.add_argument("--cxx-version", default="")
    ap.add_argument("--cmake-version", default="")
    ap.add_argument("--generator", default="")
    ap.add_argument("--build-type", default="")
    args = ap.parse_args()
    args.project_root = args.project_root.resolve()
    args.upstream = args.upstream.resolve()
    args.generated = args.generated.resolve()
    doc = write_outputs(make_payload(args), args.out_json, args.out_header)
    verify_doc(doc)
    print(f"MONA build_id={doc['build_id']} profile={doc['profile']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
