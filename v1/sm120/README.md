# Mona Miner Public v1 - SM120

## 概要

このディレクトリは、Mona Miner Public v1 の SM120 向けソースです。

- 対象GPUアーキテクチャ: SM120
- 実機検証: NVIDIA GeForce RTX 5090
- 検証OS: Windows 11 x64
- 対応アルゴリズム: Monacoin Lyra2REv2
- CUDA: 13.4.x

RTX 5090 は実機検証に使用したGPUです。同じSM120アーキテクチャを持つGPUは設計上の対応範囲に含まれますが、個別の動作・性能をすべて検証しているわけではありません。

## Public v1 Stable 構成

- Stable Ballot: ON
- RowOut Prefetch: ON
- NextCol: OFF
- Cube IMAD: mask 15 / 100%
- batch: 1,048,576
- fusion: 0
- CUDA Graph: OFF
- blocks: `64,32,64,64,256`
- 最大レジスタ数の制限: なし

## Lyra2REv2

SM120 では、主要な処理経路について複数の候補を比較し、性能だけでなく正しさ / 収束性を含めて最終構成を選定しています。

- ActiveMask + RowOut Prefetch 候補は、従来の Lyra 基準構成に対する継続測定で約 +3.63%
- 最終 v1 Stable では、正しさ / 収束性を優先して Stable Ballot を採用
- Stable Ballot は ActiveMask との直接比較で約 -0.63%だったため、高速化ではなく安定化・正しさを優先した判断として採用
- NextCol は最終確認で +0.3120%でしたが、事前設定した +0.50% の採用基準に届かなかったため不採用

## CubeHash

SM120 では CubeHash の演算命令を見直し、IMAD を利用する構成を Public v1 Stable に採用しています。

60秒の対称 A/B 比較では、従来の基準構成に対して:

- ハッシュレート: 439.779 MH/s → 464.251 MH/s（約 +5.565%）
- ボード消費電力: 574.465 W → 575.014 W（約 +0.096%）
- MH/J: 0.76554 → 0.80738（約 +5.465%）

30秒の IMAD100 / IMAD75 比較では:

- IMAD100: 467.022 MH/s
- IMAD75: 458.826 MH/s
- IMAD100 が約 +1.786%

これらの高電力ベンチマーク値はCubeHashの採用判断に使った比較結果です。約440 Wでの長時間実用参考値と同一の測定条件ではありません。

## 過去実績との参考比較

RTX 5090 / 約440 W:

- 旧 CUDA 10 参照 ccminer: 約330 MH/s、約440 W、約0.750 MH/J
- Mona Miner v1 Stable: 約395.93 MH/s、電力上限 440 W、約0.900 MH/J
- 参考上の差:
  - ハッシュレート 約 +20%
  - MH/J 約 +20%

これは、過去の実運用時の記録をもとにした参考比較です。同一のソースコード・同一の開発環境・完全に同じ電力条件で行った厳密な比較ではないため、tpruvot/ccminer 原版に対して上記の割合だけ厳密に高速化・高効率化したことを示すものではありません。

## 実機検証済みバイナリ

Public v1 SM120 は RTX 5090 / Windows 11 x64 で実機検証しています。

`mona-miner.exe` SHA256:

```text
00D42BD105974F90613CA8268827B69E3369926A8DFE2BEB236A762260D36CB1
```

実行用ZIP:

```text
Mona-Miner-v1-SM120-Windows-x64.zip
```

ZIP SHA256:

```text
6BF65983B50FAB5C72E0ED8C618E45A0AC883E53AB9DFC34FF79405BD39E488F
```

実行用バイナリはソースリポジトリには置かず、GitHub Releases で配布します。

## 主な表示オプション

デフォルトでは60秒ごとの集約表示です。

`--all`

: 各share結果を表示します。

`--interval N`

: 集約表示間隔を秒単位で変更します。有効範囲は `1..86400`、デフォルトは60秒です。

`--all` と `--interval` を同時に指定した場合は `--all` が優先されます。reject / reconnect / stale / error は集約表示中でも即時表示されます。

## GPU設定について

Mona Miner 自体は、GPUの電力上限・電圧・クロック・ファン設定を変更しません。必要なGPU設定は、利用者自身の管理下で外部から行ってください。

## ビルド環境

実機検証したPublic v1 SM120のビルド条件:

- Windows 11 x64
- Visual Studio 2022 / Build Tools の C++ x64 toolchain
- CMake 3.26 以上
- Ninja
- NVIDIA CUDA Toolkit 13.4.x
- SM120対応NVIDIA driver

CUDA 13.4.x 以外は、このPublic v1 SM120の検証済みビルド条件ではありません。

## ビルド

この `sm120` ディレクトリをcurrent directoryとして、x64 Developer PowerShellから実行します。

```powershell
cmake -S source -B out/clean -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DPHASEC_BUILD_MINER=ON `
  -DPHASEC_BUILD_TESTS=ON

cmake --build out/clean --parallel 2

ctest --test-dir out/clean -C Release --output-on-failure
```

生成されるマイナー:

```text
out/clean/mona-miner.exe
```

## ソース構成

```text
sm120/
  README.md
  NOTICE.txt
  SOURCE_SCOPE.json
  source/
    certified-backend/
    phasec-host/
    pinned-upstream/
  tests/
```

`source/CMakeLists.txt` が Public v1 SM120 の統合ビルド設定です。

## 既知事項

Public v1では、数値CLIの一部について厳密な末尾文字検証を行わない既知事項（内部識別: C06）が残っています。

サポート対象の入力形式として、数値引数には符号・空白・末尾文字を付けず、10進数字のみを指定してください。

## Public v1 の範囲

- 開発者手数料（Developer Fee）: なし
- 寄付用マイニング: なし
- 隠し / 予備プール: なし
- 隠しテレメトリ: なし
- 自動更新確認: なし
- GPUの電力上限・電圧・クロック・ファン設定の自動変更: なし

## ソースコード / ライセンス

Mona Miner は tpruvot/ccminer を基にしています。GPL v3 の条件に従って利用・改変・再配布してください。

このSM120固有の来歴:

- `NOTICE.txt`

ソース範囲:

- `SOURCE_SCOPE.json`

ライセンス本文:

- repository root の `LICENSE.txt`

第三者ライセンス / NOTICE:

- repository root の `licenses/JANSSON_LICENSE.txt`

## 無保証

本ソフトウェアは無保証で提供されます。詳細は repository root の `LICENSE.txt` を確認してください。
