# Mona Miner v2 / SM120

## 概要

- SM120（RTX 50 シリーズ）向けの Public v2 実装です。
- RTX 5090 / Windows 11 x64 / CUDA 13.4.x で実機検証しています。
- 固定 Developer Fee 2% を採用し、Donation はありません。

## 対応環境

- GPU architecture: SM120
- 実機検証: NVIDIA GeForce RTX 5090
- CUDA baseline: 13.4.x

## 手数料

- Developer Fee: 2%
- Donation: なし
- completed-work 基準: USER 98q → DEVFEE 2q（q = 64）

debt / catch-up はありません。Developer 接続停止中に未実行となった Fee を
復旧後に追加回収しません。USER mining が利用できない場合は、DEVFEE-only の
GPU mining を継続しません。

## パフォーマンス

30分の実プール検証を2回実施しています。

- Run A: 423.778 MH/s total / 415.352 MH/s USER 実効
- Run B: 426.614 MH/s total / 418.114 MH/s USER 実効
- 代表値: 約425 MH/s total / 約417 MH/s USER 実効
- Public v1 SM120 約395.93 MH/s比: total 約+7.3% / USER 実効 約+5.3%

## Frozen SM120 backend

Composition: `N02 + EXP01 + R02_CUBE2_TAIL_FUSION_B64`

Qualified privacy-sanitized cubins:

- N02_EXP01_PRODUCER: `9463bb3d1242371f7ebaef19aa528b63debeb6fdc2fc900f8f1a0d8e7d85ec19`
- R02_ACCEPTED: `c9a9a6872df92f529b88cf6a80eabb925b515c76a1c4a3791788a52afb7e8a84`
- R02_CUBE2_TAIL_FUSION: `84f3b1b105a7cb4df7dec50968e3ea81e2c4a0c97f83afe4872e413376f5e2bd`

Exact corresponding device source is included under `device-source/`.
Sanitization changed debug/source-path metadata only; runtime/device code was
revalidated before GPU/live qualification.

## 起動 / USER pool credentials

Public v2 は Public v1 と同じ通常CLIをサポートします。

```text
mona-miner.exe -a lyra2v2 -o stratum+tcp://HOST:PORT -u USER -p PASS
```

`-o / --url`、`-u / --user`、`-p / --pass` を使用できます。
Release package の `start_vippool.bat` もこのCLIを直接使用し、USER worker /
password をBATへ保存して繰り返し起動できます。

`--credentials-stdin` は既存のvalidation / automation互換用として引き続き
サポートしますが、通常利用では必須ではありません。CLI credential modeと
`--credentials-stdin` の同時指定は拒否します。

The Developer destination is intentionally public mining-only authentication and
is centralized in `source/src/developer_destination.cpp`.

## Validation

GPU correctness and Compute Sanitizer memcheck/racecheck/initcheck/synccheck:
**PASS**.

Controlled GPU Local `DEVFEE_ONLY_FSA / USER_ONLY_HARNESS` total-throughput
paired median: approximately **-0.006%**.

Two 30-minute DEVFEE-only live runs:

| Run | Total MH/s | USER-effective MH/s | Fee work | Mean board W | Board MH/J |
|---|---:|---:|---:|---:|---:|
| A | 423.778 | 415.352 | 1.9883% | 437.10 | 0.9694 |
| B | 426.614 | 418.114 | 1.9925% | 438.60 | 0.9726 |

Public v1 SM120 reference: about **395.93 MH/s** with no fee. These v2 runs are
about **+4.91%** and **+5.60% USER-effective** versus that reference.

Each live run observed one low-frequency non-stale USER pool rejection. No
DEVFEE rejection, local stale, UNKNOWN or not-sent share was recorded. A later
persistent-session diagnostic forwarded **355/355 USER submits accepted** with
no rejection. The condition is retained as operational monitoring context, not
classified as a reproducible miner defect.

## Source identity

The frozen GPU backend, qualified cubins and corresponding device-source remain
unchanged. Public v2 adds a host-only v1-compatible USER CLI
(`-o / -u / -p` and long aliases); this does not alter the GPU launch contract,
98q/2q accounting, Stratum session implementation or Developer destination.

The pre-CLI-compatibility validation identity and the scope of this host-only
compatibility change are recorded in `SOURCE_SCOPE.json`. The exact rebuilt
release executable must be requalified before tag / Release publication.
A rebuilt cubin is not silently treated as the qualified image.

`source/shipping_docs/README.md` の `private product prototype` / `review prototype`
表記は、validated host tree の byte identity を保持するために保存した
validation-era snapshot の歴史的文面です。現在の Public v2 の release status を
示すものではありません。公開状況は repository / PR / GitHub Releases を確認してください。

## Public GPU executable build

public repo の qualified cubins 3本から image object を生成する手順を
[build-tools/README.md](build-tools/README.md) に記載しています。
private sealed runner は不要です。frozen `source/` と cubin bytes は変更せず、
既存の `MONA2_IMAGE_OBJECT` / `MONA2_GENERATED_INCLUDE` 引数を満たします。
