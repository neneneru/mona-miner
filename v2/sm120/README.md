# Mona Miner v2 / SM120

## 概要

- 現在のPublic v2 / SM120 releaseはv2.0.1です。
- SM120（RTX 50 シリーズ）向けの Public v2 実装です。
- RTX 5090 / Windows 11 x64 / CUDA 13.4.x で実機検証しています。
- 固定 Developer Fee 2% を採用しています。

## 対応環境

- GPUアーキテクチャ: SM120
- 実機検証: NVIDIA GeForce RTX 5090
- CUDA基準: 13.4.x

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

| 測定 | Total MH/s | USER実効 MH/s | Fee work | 平均ボード電力 | MH/J |
|---|---:|---:|---:|---:|---:|
| A | 423.778 | 415.352 | 1.9883% | 437.10 W | 0.9694 |
| B | 426.614 | 418.114 | 1.9925% | 438.60 W | 0.9726 |

代表値は約425 MH/s total / 約417 MH/s USER実効です。
Public v1 SM120 約395.93 MH/s比では、total 約 +7.3%、USER実効 約 +5.3% です。

上記はRTX 5090と検証環境における実測値であり、すべてのSM120 GPUで同一性能を保証するものではありません。

## 検証済みGPUイメージ / ソース

Public v2.0.1の検証済みcubinは以下の3本です。

- N02_EXP01_PRODUCER: `9463bb3d1242371f7ebaef19aa528b63debeb6fdc2fc900f8f1a0d8e7d85ec19`
- R02_ACCEPTED: `c9a9a6872df92f529b88cf6a80eabb925b515c76a1c4a3791788a52afb7e8a84`
- R02_CUBE2_TAIL_FUSION: `84f3b1b105a7cb4df7dec50968e3ea81e2c4a0c97f83afe4872e413376f5e2bd`

対応するdevice sourceは `device-source/` に収録しています。
privacy sanitizationで変更したのはdebug/source-path metadataのみで、
runtime/device codeの不変性を確認したうえでGPU・実プール検証を行っています。

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

`--credentials-stdin` は自動化用途でも利用できます。
通常利用では必須ではなく、CLI credential modeとの同時指定は拒否します。

Developer Feeの接続先認証情報は、公開前提のmining専用情報として
`source/src/developer_destination.cpp` の1か所に集約しています。

## Console出力

通常起動ではPublic v1と同じ簡潔な集約表示を使用します。
Public v2ではDeveloper Feeを1行だけ追加表示します。

```text
[YYYY-MM-DD HH:MM:SS] Mona Miner - Lyra2REv2
[YYYY-MM-DD HH:MM:SS] Pool: stratum+tcp://HOST:PORT
[YYYY-MM-DD HH:MM:SS] GPU #0: NVIDIA GeForce RTX 5090, SM 12.0
[YYYY-MM-DD HH:MM:SS] Developer Fee: 2%
[YYYY-MM-DD HH:MM:SS] Stratum authorized
[YYYY-MM-DD HH:MM:SS] Waiting for next pool job...
[YYYY-MM-DD HH:MM:SS] Mining started | diff 2156.3256
[YYYY-MM-DD HH:MM:SS] 425.00 MH/s | accepted: 4/4 (+4) | diff 2156.3256
```

認証後に次のpool jobを待っている場合は待機状態を表示し、採掘開始時に
poolから指定されたdifficultyを表示します。

最初のMH/s集計は、Stratum handshakeやjob待ち時間を含めず、
実際に採掘可能になってから開始します。job更新だけでは不要に集計windowをresetしません。

- 既定: 60秒ごとの集約表示
- `--interval N`: 集約間隔を変更
- `--all`: USER share結果を都度表示
- `--json`: 検証・診断用の詳細JSON event stream

## 検証

Public v2のGPU構成は、正しさ試験とCompute Sanitizerの
memcheck / racecheck / initcheck / synccheckで検証しています。
制御されたローカルGPU比較では、USER専用実行に対するDeveloper Fee専用実行の
total throughputの対応比較中央値差は約 -0.006%でした。

Public v2.0.1のconsole/BAT変更はhost UXのみで、GPU backend、Fee accounting、
Stratum protocol、qualified cubin、device-sourceは変更していません。
Windows Release build、helper 2/2、CPU/mock CTest 9/9、console表示、
RTX 5090 correctness/canary、通常表示と診断JSONの実プール検証、privacy scanはPASSです。
configure-time privacy toolchainの再検証でも、検証済みEXEとのbyte一致を確認しています。

30分の実プール検証A/BではUSER-side rejectを各1件観測しましたが、
DEVFEE rejection、local stale、UNKNOWN、not-sentはありませんでした。
その後のpersistent-session diagnosticではUSER submit 355/355 accepted、reconnect 0を確認しており、
再現性のあるminer defectやrelease blockerとは分類していません。

## ソース / Release同一性

### Public v2.0.1

公開ソースの識別tag: [`sm120-v2.0.1`](https://github.com/neneneru/mona-miner/tree/sm120-v2.0.1/v2/sm120)

Release実行ファイル SHA256:

```text
9A3FFBE32924C5F227B90DAF3BD55993DE3E2A32C24B1F38F07CD90E752CB800
```

同じsourceからの再buildでも、byte一致しないbinaryは自動的に検証済みbinaryと同一とは扱いません。

検証済みsourceとbinaryの来歴は `SOURCE_SCOPE.json` に記録しています。
起動と動作範囲の要点は [source/shipping_docs/README.md](source/shipping_docs/README.md) にも記載しています。

## Public GPUビルド

公開リポジトリのqualified cubin 3本からimage objectを生成し、
GPU executableをbuildする手順は [build-tools/README.md](build-tools/README.md) に記載しています。

公開helperでqualified cubinからimage objectを生成し、既存の
`MONA2_IMAGE_OBJECT` / `MONA2_GENERATED_INCLUDE` を指定してbuildします。
