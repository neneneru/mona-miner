# Mona Miner v2 / SM120

## 概要

- SM120（RTX 50 シリーズ）向けの Public v2 実装です。
- RTX 5090 / Windows 11 x64 / CUDA 13.4.x で実機検証しています。
- 固定 Developer Fee 2% を採用しています。

## 対応環境

- GPU architecture: SM120
- 実機検証: NVIDIA GeForce RTX 5090
- CUDA baseline: 13.4.x

## 手数料

- Developer Fee: 2%
- completed-work 基準: USER 98q → DEVFEE 2q（q = 64）

Developer 接続が利用できない場合も USER mining を継続します。
未実行のFeeをdebtとして持ち越さず、復旧後のcatch-upも行いません。
USER mining が利用できない場合は、DEVFEE-only のGPU miningを継続しません。

## 主な最適化

Public v2 SM120 の最終構成は以下です。

```text
N02 + EXP01 + R02_CUBE2_TAIL_FUSION_B64
```

主な改善は、GPU処理ステージ間の境界削減、同じ処理主体で完結できる範囲の融合、
不要な中間表現・転送の削減、後段tail処理の融合です。

最終tail fusionの採用判断では、直前のaccepted構成との対応比較24組で
**24/24が改善し、MH/s改善率中央値は約 +0.886%** でした。

この約 +0.886% はtail optimization単体の採用根拠です。
Public v1からPublic v2への最終的な製品性能差や、各最適化の単純合計を示す値ではありません。

## パフォーマンス

RTX 5090で30分の実プール検証を2回実施しています。

| Run | Total MH/s | USER実効 MH/s | Fee work | 平均board W | Board MH/J |
|---|---:|---:|---:|---:|---:|
| A | 423.778 | 415.352 | 1.9883% | 437.10 | 0.9694 |
| B | 426.614 | 418.114 | 1.9925% | 438.60 | 0.9726 |

代表値は約425 MH/s total / 約417 MH/s USER実効です。
Public v1 SM120 約395.93 MH/s比では、total 約 +7.3%、USER実効 約 +5.3% です。

上記はRTX 5090と検証環境における実測値であり、すべてのSM120 GPUで同一性能を保証するものではありません。

## Qualified GPU image / source

Public v2.0.0で使用するprivacy-sanitized cubinは以下の3本です。

- N02_EXP01_PRODUCER: `9463bb3d1242371f7ebaef19aa528b63debeb6fdc2fc900f8f1a0d8e7d85ec19`
- R02_ACCEPTED: `c9a9a6872df92f529b88cf6a80eabb925b515c76a1c4a3791788a52afb7e8a84`
- R02_CUBE2_TAIL_FUSION: `84f3b1b105a7cb4df7dec50968e3ea81e2c4a0c97f83afe4872e413376f5e2bd`

対応するdevice sourceは `device-source/` に収録しています。
privacy sanitizationで変更したのはdebug/source-path metadataのみで、runtime/device codeの不変性を確認したうえでGPU・実プール検証を行っています。

## 起動 / USER pool認証情報

Public v1 と同じ通常CLIを使用できます。

```text
mona-miner.exe -a lyra2v2 -o stratum+tcp://HOST:PORT -u USER -p PASS
```

`-o / --url`、`-u / --user`、`-p / --pass` を使用できます。
`-p / --pass` の要否は接続先poolの仕様に従います。

Release package の `start_vippool.bat` はVIP Pool向けの起動例です。
BATの利用は必須ではなく、対応するStratum poolをCLIから指定して直接起動できます。
BATへ保存したUSER worker / passwordは平文になるため、mining専用の認証情報を使用してください。

`--credentials-stdin` はvalidation / automation互換用として保持しています。
通常利用では必須ではなく、CLI credential modeとの同時指定は拒否します。

Developer destinationは公開用のmining専用認証情報で、
`source/src/developer_destination.cpp` の1か所に集約しています。

## 検証

Public v2.0.0では以下を確認しています。

- GPU correctness: PASS
- Compute Sanitizer: memcheck / racecheck / initcheck / synccheck PASS
- controlled GPU Local `DEVFEE_ONLY_FSA / USER_ONLY_HARNESS`: total-throughput paired median差 約 -0.006%
- Windows 11 x64 / MSVC 19.44 / CUDA 13.4.x build: PASS
- public image-object helper: 2/2 PASS
- CPU/mock CTest: 8/8 PASS
- CLI acceptance: 20/20 PASS
- linked qualified GPU payload verification: PASS
- RTX 5090 correctness: 4,232 CPU comparisons / canary checks / 3 qualified modules load PASS
- direct `-o/-u/-p` VIP Pool live gate: PASS
- USER accepted: 1 / rejected: 0
- USER / Developer: connect 1 each / reconnect 0
- local stale / UNKNOWN / not-sent: 0
- clean shutdown: exit 0
- public package privacy scan: PASS

30分の実プール検証A/BではUSER-side rejectを各1件観測しましたが、
DEVFEE rejection、local stale、UNKNOWN、not-sentはありませんでした。
その後のpersistent-session diagnosticではUSER submit 355/355 accepted、reconnect 0を確認しており、
再現性のあるminer defectやrelease blockerとは分類していません。

## ソースとRelease identity

Public v2.0.0のGit tagは `sm120-v2.0.0` です。

最終Release executable SHA256:

```text
D2E172A34871287B9C120D6502F1C966899AB923F7BE5E0D5A0FF3DDC9DE3386
```

CLI互換修正ではqualified cubin、device-source、GPU backend、
98q/2q accounting、Stratum/session、Developer destinationを変更していません。
検証済みsourceとbinaryのprovenanceは `SOURCE_SCOPE.json` に記録しています。

Release後に同じsourceから再buildしたbinaryでも、byte identityが異なる場合は
自動的にPublic v2.0.0の検証済みbinaryと同一とは扱いません。

`source/shipping_docs/README.md` に残る `private product prototype` / `review prototype`
という表記は、validation時点のhost treeを保持するための歴史的文面です。
現在の公開状態を示すものではありません。

## Public GPUビルド

public repoのqualified cubin 3本からimage objectを生成し、
GPU executableをbuildする手順は [build-tools/README.md](build-tools/README.md) に記載しています。

private sealed runnerは不要です。
qualified cubinとfrozen sourceを変更せず、既存の
`MONA2_IMAGE_OBJECT` / `MONA2_GENERATED_INCLUDE` を指定してbuildします。
