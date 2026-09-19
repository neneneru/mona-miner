# Mona Miner

## 概要

Mona Miner は、Monacoin の Lyra2REv2 向け NVIDIA CUDA miner です。

tpruvot/ccminer をベースに、modern NVIDIA CUDA 環境と Lyra2REv2 / CubeHash の実装を対象として、現行GPU向けの最適化と検証を行っています。公開中の v1 では、CUDA 13.4.x、native SM120 / Compute Capability 12.0 に対応し、RTX 5090 で実機検証しています。

このリポジトリでは、公開済みのversion / architectureごとにソースコードを分けて収録しています。

## 主な改善点

### CUDA / SM120

- CUDA 13.4.x に対応
- native SM120 / Compute Capability 12.0
- RTX 5090 で実機検証

### Lyra2REv2

- ActiveMask + RowOut Prefetch 候補は、それ以前の Lyra baseline 比で sustained 約 +3.63%
- 最終 v1 Stable では、correctness / convergence を優先して Stable Ballot を採用
- Stable Ballot は ActiveMask との直接比較で約 -0.63%だったため、高速化項目ではなく Stable 化・correctness 側の判断として扱っています
- NextCol は最終確認で +0.3120%でしたが、事前設定した +0.50% promotion margin 未達のため v1 Stable には採用していません

### CubeHash

- Cube IMAD100 の60秒対称 A/B（従来 Active reference 比）で、Hashrate 約 +5.565%
- 同条件で、Board power 約 +0.096%
- 同条件で、MH/J 約 +5.465%
- 30秒の IMAD100 vs IMAD75 比較では、IMAD100 が 467.022 MH/s、IMAD75 が 458.826 MH/s、IMAD100 は +1.786%

### RTX 5090 / 約440Wでの過去実績との参考比較

- 旧 CUDA10 参照 ccminer / RTX 5090: 約330 MH/s、約440W、約0.750 MH/J
- Mona Miner v1 Stable / RTX 5090: 約395.93 MH/s、440W limit、約0.900 MH/J
- 参考上の差: Hashrate 約 +19.98%、MH/J 約 +20%

この約20%比較は historical practical reference です。同一 source / 同一 toolchain による厳密な A/B ではないため、tpruvot/ccminer 原版から厳密に20%高速化したという意味ではありません。

## 現在の公開ソース

現在収録している公開ソース:

- [Public v1 / SM120](v1/sm120/)

対応GPU、実機検証状況、ビルド条件、validated binary SHA256などは、各version / architectureのREADMEを確認してください。


## Public v1

Public v1 の共通仕様は以下を確認してください。

- [v1/README.md](v1/README.md)


## ソースコード / ライセンス

Mona Miner は tpruvot/ccminer を基にしています。
GPL v3 の条件に従って利用・改変・再配布してください。

ライセンス本文:

- LICENSE.txt

第三者ライセンス / NOTICE:

- licenses/JANSSON_LICENSE.txt

version / architecture固有のprovenance:

- 各ディレクトリ内の NOTICE.txt


## 無保証

本ソフトウェアは無保証で提供されます。
詳細は LICENSE.txt を確認してください。
