# Mona Miner Public v1 - SM89

## 概要

このディレクトリは、Mona Miner Public v1 の SM89 向けソースです。

- 対象GPUアーキテクチャ: SM89
- 実機検証: NVIDIA GeForce RTX 4070 Ti
- 検証OS: Windows 11 x64
- 対応アルゴリズム: Monacoin Lyra2REv2
- CUDA: 13.4.x

RTX 4070 Ti は実機検証に使用したGPUです。
同じSM89アーキテクチャを持つGPUは設計上の対応範囲に含まれますが、個別の動作・性能をすべて検証しているわけではありません。

## Public v1 Stable 構成

- Stable Ballot: ON
- RowOut Prefetch: ON
- NextCol: OFF
- Cube IMAD: OFF
- batch: 2,097,152
- fusion: 6
- CUDA Graph: OFF
- blocks: `256,256,256,256,256`
- 最大レジスタ数の制限: なし

## Lyra2REv2

SM89 では RTX 4070 Ti 上で候補を比較し、SM89向けの Public v1 Stable 構成を選定しています。

- RowOut Prefetch は16組の対応比較で、MH/s の改善率中央値が約 +0.562%、MH/J が約 +0.586%となり、事前設定した +0.50% の採用基準を満たしたため採用
- 同実験における RowOut 構成の中央値は 159.994 MH/s、187.829 W、0.8519 MH/J
- fusion / CUDA Graph / ブロックサイズ / バッチサイズは複数候補を比較し、最終的に fusion 6、Graph OFF、batch 2,097,152、blocks `256,256,256,256,256` を採用

## CubeHash

SM89 では Cube IMAD は採用せず、CubeHash を前後の処理と融合する構成を採用しています。

Public v1 Stable の `fusion 6` では:

- BLAKE + Keccak + 1回目の CubeHash を1つのカーネルに融合
- Skein + 2回目の CubeHash を1つのカーネルに融合
- 1バッチあたりのハッシュカーネルは合計6本

SM89ではCubeHash単体のIMAD化ではなく、カーネル境界を減らす方向で構成を最適化しています。

## 実用参考値

RTX 4070 Ti / 約180 W台:

- 実プール長時間平均 約160.024 MH/s
- 中央値 約160.140 MH/s
- 通常 約185〜188 W
- 約0.85〜0.87 MH/J

長時間の実プール検証は約2時間33分で、最終 accepted は 1323 / 1324 でした。

過去の近い電力条件における実運用参考値は、旧 CUDA 10 系で約130 MH/s / 約180 Wです。
この比較は同一のソースコード・同一の開発環境・完全に同じ電力条件による厳密な A/B ではありません。

## Public v1 実機検証

Public v1 SM89 は RTX 4070 Ti / Windows 11 x64 で、
ビルド、正しさ確認、実行時確認、Defenderスキャン、実プール接続を含む検証を行っています。

最終リリース候補の短時間確認:

- ベンチマーク: 約161.153 MH/s
- 実プールでの観測値: 約162.37 MH/s
- accepted: 1
- rejected: 0
- stale: 0
- send failure: 0
- 正常終了: PASS

短時間確認と長時間実プール検証は測定目的が異なるため、同一の性能測定としては扱いません。

## 実機検証済みバイナリ

`mona-miner.exe` SHA256:

```text
CA8C4C764113F25FF5E38E7817A30469485A5B6BD23B30E014D05342FEB62FDB
```

実行用ZIP:

```text
Mona-Miner-v1-SM89-Windows-x64.zip
```

ZIP SHA256:

```text
CC4A7BB70B287EA42B294D7059C309BAEF03674833C48554ECE721226466BAAF
```

実行用バイナリはソースリポジトリには置かず、GitHub Releases で配布します。

## 主な表示オプション

デフォルトでは60秒ごとの集約表示です。

`--all`

: 各share結果を表示します。

`--interval N`

: 集約表示間隔を秒単位で変更します。有効範囲は `1..86400`、デフォルトは60秒です。

`--all` と `--interval` を同時に指定した場合は `--all` が優先されます。
reject / reconnect / stale / error は集約表示中でも即時表示されます。

## GPU設定について

Mona Miner 自体は、GPUの電力上限・電圧・クロック・ファン設定を変更しません。
必要なGPU設定は、利用者自身の管理下で外部から行ってください。

## ビルド環境

実機検証したPublic v1 SM89のビルド条件:

- Windows 11 x64
- Visual Studio 2022 / Build Tools の C++ x64 toolchain
- CMake 3.26 以上
- Ninja
- NVIDIA CUDA Toolkit 13.4.x
- SM89対応NVIDIA driver

CUDA 13.4.x 以外は、このPublic v1 SM89の検証済みビルド条件ではありません。

## ビルド

この `sm89` ディレクトリをcurrent directoryとして、
x64 Developer PowerShellから実行します。

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
sm89/
  README.md
  NOTICE.txt
  SOURCE_SCOPE.json
  source/
    certified-backend/
    phasec-host/
    pinned-upstream/
  tests/
```

`source/CMakeLists.txt` が Public v1 SM89 の統合ビルド設定です。

## 既知事項

Public v1では、数値CLIの一部について厳密な末尾文字検証を行わない既知事項
（内部識別: C06）が残っています。

サポート対象の入力形式として、数値引数には符号・空白・末尾文字を付けず、
10進数字のみを指定してください。

## Public v1 の範囲

- 開発者手数料（Developer Fee）: なし
- 寄付用マイニング: なし
- 隠し / 予備プール: なし
- 隠しテレメトリ: なし
- 自動更新確認: なし
- GPUの電力上限・電圧・クロック・ファン設定の自動変更: なし

## ソースコード / ライセンス

Mona Miner は tpruvot/ccminer を基にしています。
GPL v3 の条件に従って利用・改変・再配布してください。

このSM89固有の来歴:

- `NOTICE.txt`

ソース範囲:

- `SOURCE_SCOPE.json`

ライセンス本文:

- repository root の `LICENSE.txt`

第三者ライセンス / NOTICE:

- repository root の `licenses/JANSSON_LICENSE.txt`

## 無保証

本ソフトウェアは無保証で提供されます。
詳細は repository root の `LICENSE.txt` を確認してください。
