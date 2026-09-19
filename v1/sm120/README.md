# Mona Miner Public v1 - SM120

概要
----------------------------
Mona Miner は Monacoin の Lyra2REv2 用 NVIDIA CUDA miner です。

このディレクトリは SM120 / Compute Capability 12.0 向けで、
RTX 50シリーズでの利用を想定しています。

実機検証は RTX 5090 / Windows 11 x64 で実施しています。
RTX 5090 以外の RTX 50シリーズについては未検証です。


主な表示オプション
----------------------------
デフォルトでは60秒ごとの集約表示です。

--all
    各share結果を表示します。

--interval N
    集約表示間隔を秒単位で変更します。
    有効範囲: 1..86400
    デフォルト: 60

※`--all` と `--interval` を同時に指定した場合は `--all` が優先されます。
※reject / reconnect / stale / error は集約表示中でも即時表示されます。


GPU設定について
----------------------------
Mona Miner 自体は次のGPU設定を書き換えません。

- Power Limit
- 電圧
- クロック
- ファン

※必要なGPU設定は、利用者自身の管理下で外部から行ってください。
※Mona Miner本体はこれらの設定を変更しません。


実行環境
----------------------------
検証環境:
Windows 11 x64
NVIDIA GeForce RTX 5090 / SM120

build条件:
Visual Studio 2022 / Build Tools の C++ x64 toolchain
CMake 3.26 以上
Ninja
NVIDIA CUDA Toolkit 13.4.x
SM120対応NVIDIA driver

※CUDA 13.4.x 以外は、このPublic v1 SM120 acceptance build条件ではありません。


mona-miner.exe SHA256
----------------------------
00D42BD105974F90613CA8268827B69E3369926A8DFE2BEB236A762260D36CB1

上記SHA256は RTX 5090 でphysical qualificationをPASSした
validated binaryのものです。

実行用binaryはsource repositoryには置かず、GitHub Releases側で配布します。

Release asset名:
Mona-Miner-v1-SM120-Windows-x64.zip


Public v1 の範囲
----------------------------
Developer Fee: なし
Donation mining: なし
Hidden/fallback pool: なし
Hidden telemetry: なし
Update check: なし
GPU power/clock/voltage/fan の自動変更: なし


ビルド
----------------------------
この sm120 ディレクトリをcurrent directoryとして、
x64 Developer PowerShellから実行します。

```powershell
cmake -S source -B out/clean -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DPHASEC_BUILD_MINER=ON `
  -DPHASEC_BUILD_TESTS=ON

cmake --build out/clean --parallel 2

ctest --test-dir out/clean -C Release --output-on-failure
```

生成されるminer:

```text
out/clean/mona-miner.exe
```


ソース構成
----------------------------
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

source/CMakeLists.txt がPublic v1 SM120の統合ビルドrecipeです。


既知事項
----------------------------
Public v1では、数値CLIの一部について厳密な末尾文字検証を行わない既知事項
（内部識別: C06）が残っています。

サポート対象の入力形式として、数値引数には符号・空白・末尾文字を付けず、
10進数字のみを指定してください。


ソースコード / ライセンス
----------------------------
Mona Miner は tpruvot/ccminer を基にしています。
GPL v3 の条件に従って利用・改変・再配布してください。

このSM120固有のprovenance:
NOTICE.txt

source scope:
SOURCE_SCOPE.json

ライセンス本文:
repository root の LICENSE.txt

第三者ライセンス / NOTICE:
repository root の licenses/JANSSON_LICENSE.txt


無保証
----------------------------
本ソフトウェアは無保証で提供されます。
詳細は repository root の LICENSE.txt を確認してください。
