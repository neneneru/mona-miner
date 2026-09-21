# Mona Miner

## 概要

Mona Miner は、Monacoin の Lyra2REv2 向け NVIDIA CUDA マイナーです。

tpruvot/ccminer をベースに、現行の NVIDIA CUDA 環境と Lyra2REv2 / CubeHash の実装を対象として、現行GPU向けの最適化と検証を行っています。
公開中の Public v1 では、CUDA 13.4.x を前提に、GPUアーキテクチャごとのネイティブビルドを用意しています。

- SM120 — RTX 5090 で実機検証
- SM89 — RTX 4070 Ti で実機検証

実機検証済みGPUは上記2機種ですが、実行時の対応判定は個別の製品名ではなく、SM（GPUアーキテクチャ）を基準とします。
同じCUDAアーキテクチャを持つGPUは設計上の対応範囲に含まれますが、個別の動作・性能をすべて検証しているわけではありません。
このリポジトリでは、公開済みのバージョン / GPUアーキテクチャごとにソースコードを分けて収録しています。

## 主な改善点

### CUDA / GPUアーキテクチャ

どちらも Public v1 Stable として構成を固定し、実機検証を完了しています。

#### SM120

- CUDA 13.4.x に対応
- SM120向けネイティブビルド
- RTX 5090 で実機検証

#### SM89

- CUDA 13.4.x に対応
- SM89向けネイティブビルド
- RTX 4070 Ti で実機検証

### Lyra2REv2

SM120 / SM89 それぞれでGPUアーキテクチャに合わせて候補を比較し、
Public v1 Stable の構成を個別に検証・固定しています。

#### SM120

SM120 では、Lyra2REv2 の主要な処理経路について複数の候補を比較し、
性能だけでなく正しさ / 収束性を含めて最終構成を選定しています。

- ActiveMask + RowOut Prefetch 候補は、従来の Lyra 基準構成に対する継続測定で約 +3.63%
- 最終 v1 Stable では、正しさ / 収束性を優先して Stable Ballot を採用
- Stable Ballot は ActiveMask との直接比較で約 -0.63%だったため、高速化ではなく安定化・正しさを優先した判断として採用
- NextCol は最終確認で +0.3120%でしたが、事前設定した +0.50% の採用基準に届かなかったため v1 Stable には不採用

最終的な Public v1 Stable 構成:

- Stable Ballot: ON
- RowOut Prefetch: ON
- NextCol: OFF
- batch: 1,048,576
- fusion: 0
- CUDA Graph: OFF
- blocks: `64,32,64,64,256`

#### SM89

SM89 では RTX 4070 Ti 上で候補を比較し、
SM89向けの Public v1 Stable 構成を選定しています。

- RowOut Prefetch は16組の対応比較で、MH/s の改善率中央値が約 +0.562%、MH/J が約 +0.586%となり、事前設定した +0.50% の採用基準を満たしたため採用
- 同実験における RowOut 構成の中央値は 159.994 MH/s、187.829 W、0.8519 MH/J
- fusion / CUDA Graph / ブロックサイズ / バッチサイズは複数候補を比較し、最終的に fusion 6、Graph OFF、batch 2,097,152、blocks `256,256,256,256,256` を採用
- 最終 Stable 構成の実プール長時間検証では、平均約160.024 MH/s、中央値約160.140 MH/s、通常約185〜188 Wを記録

最終的な Public v1 Stable 構成:

- Stable Ballot: ON
- RowOut Prefetch: ON
- NextCol: OFF
- batch: 2,097,152
- fusion: 6
- CUDA Graph: OFF
- blocks: `256,256,256,256,256`

### CubeHash

CubeHash は、SM120 と SM89 で異なる方針の最適化を採用しています。

#### SM120

SM120 では CubeHash の演算命令を見直し、IMAD を利用する構成を実測比較して Public v1 Stable に採用しています。

60秒の対称 A/B 比較では、従来の基準構成に対して:

- ハッシュレート: 439.779 MH/s → 464.251 MH/s（約 +5.565%）
- ボード消費電力: 574.465 W → 575.014 W（約 +0.096%）
- MH/J: 0.76554 → 0.80738（約 +5.465%）

また、30秒の IMAD100 / IMAD75 比較では:

- IMAD100: 467.022 MH/s
- IMAD75: 458.826 MH/s
- IMAD100 が約 +1.786%

となり、最終的に Cube IMAD100（mask 15 / 100%）を採用しています。

#### SM89

SM89 では Cube IMAD は採用せず、CubeHash を前後の処理と融合する構成を採用しています。

Public v1 Stable の `fusion 6` では:

- BLAKE + Keccak + 1回目の CubeHash を1つのカーネルに融合
- Skein + 2回目の CubeHash を1つのカーネルに融合
- 1バッチあたりのハッシュカーネルは合計6本

となっています。

SM120 の `fusion 0` 構成では1バッチあたりのハッシュカーネルは合計8本のため、
SM89ではCubeHash単体のIMAD化ではなく、カーネル境界を減らす方向で構成を最適化しています。

※この6本 / 8本という違いは構成上の差を示すもので、SM89とSM120の性能を直接比較する数値ではありません。

### 過去実績との参考比較

#### RTX 5090 / SM120 / 約440 W

- 旧 CUDA 10 参照 ccminer: 約330 MH/s、約440 W、約0.750 MH/J
- Mona Miner v1 Stable: 約395.93 MH/s、電力上限 440 W、約0.900 MH/J
- 参考上の差:
  - ハッシュレート 約 +20%
  - MH/J 約 +20%

#### RTX 4070 Ti / SM89 / 約180 W台

- 旧 CUDA 10 系の実運用参考値: 約130 MH/s、約180 W、約0.72 MH/J
- Mona Miner v1 Stable:
  - 実プール長時間平均 約160.024 MH/s
  - 中央値 約160.140 MH/s
  - 通常 約185〜188 W
  - 約0.85〜0.87 MH/J
- 参考上の差:
  - ハッシュレート 約 +23%
  - MH/J 約 +18〜20%

これらは、過去の実運用時の記録をもとにした参考比較です。
同一のソースコード・同一の開発環境・完全に同じ電力条件で行った厳密な比較ではないため、
tpruvot/ccminer 原版に対して上記の割合だけ厳密に高速化・高効率化したことを示すものではありません。

### Public v1 実機検証

Public v1 は、SM120 / SM89 それぞれの実機で、
ビルド、正しさ確認、実行時確認、Defenderスキャン、実プール接続を含む検証を行っています。

#### SM120 / RTX 5090

RTX 5090 / Windows 11 x64 で実機検証しています。

実用上の参考値:

- 約395.93 MH/s
- 電力上限 440 W
- 約0.900 MH/J

#### SM89 / RTX 4070 Ti

RTX 4070 Ti / Windows 11 x64 で実機検証しています。

実用上の参考値:

- 実プール長時間平均 約160.024 MH/s
- 中央値 約160.140 MH/s
- 通常 約185〜188 W
- 約0.85〜0.87 MH/J

最終リリース候補の短時間確認や受理試験など、各GPUアーキテクチャ固有の詳細は、それぞれのREADMEに記載しています。

## 現在の公開ソース

現在収録している公開ソース:

- [Public v1 / SM120](v1/sm120/)
- [Public v1 / SM89](v1/sm89/)

対応GPU、実機検証状況、ビルド条件、検証済みバイナリのSHA256などは、各バージョン / GPUアーキテクチャのREADMEを確認してください。

## Public v1

Public v1 の共通仕様は以下を確認してください。

- [v1/README.md](v1/README.md)

Public v1 は、GPUアーキテクチャごとにネイティブバイナリを分けて配布します。

- SM120: `Mona-Miner-v1-SM120-Windows-x64.zip`
- SM89: `Mona-Miner-v1-SM89-Windows-x64.zip`

Public v1 には、開発者手数料（Developer Fee）、寄付用マイニング、隠し / 予備プール、隠しテレメトリ、自動更新確認はありません。
また、Mona Miner 自体はGPUの電力上限・電圧・クロック・ファン設定を変更しません。

## ソースコード / ライセンス

Mona Miner は tpruvot/ccminer を基にしています。
GPL v3 の条件に従って利用・改変・再配布してください。

ライセンス本文:

- LICENSE.txt

第三者ライセンス / NOTICE:

- licenses/JANSSON_LICENSE.txt

バージョン / GPUアーキテクチャ固有の来歴:

- 各ディレクトリ内の NOTICE.txt

## 無保証

本ソフトウェアは無保証で提供されます。
詳細は LICENSE.txt を確認してください。
