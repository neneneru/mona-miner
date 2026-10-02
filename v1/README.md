# Mona Miner Public v1

## 概要

Mona Miner Public v1 は、Monacoin の Lyra2REv2 向け NVIDIA CUDA マイナーです。

対応アルゴリズムは Lyra2REv2（`-a lyra2v2`）のみです。Public v1 では、GPUアーキテクチャごとにネイティブビルドを分けています。

- SM120 — RTX 5090 で実機検証
- SM89 — RTX 4070 Ti で実機検証

実機検証済みGPUは上記2機種ですが、実行時の対応判定は個別の製品名ではなく、SM（GPUアーキテクチャ）を基準とします。同じCUDAアーキテクチャを持つGPUは設計上の対応範囲に含まれますが、個別の動作・性能をすべて検証しているわけではありません。

## 対応環境

- SM120（RTX 50 シリーズ） / SM89（RTX 40 シリーズ）
- 実機検証: RTX 5090 / RTX 4070 Ti、Windows 11 x64
- CUDA 13.4.x

## 手数料

- Developer Fee: なし
- Donation: なし

## パフォーマンス

- SM120: 約395.93 MH/s、電力上限440 W、約0.900 MH/J
- SM89: 長時間実プール平均約160.024 MH/s、中央値約160.140 MH/s、通常約185〜188 W
- 旧 ccminer 比の参考値: SM120 約+20%、SM89 約+23%

過去実績との比較条件・注意書きと詳細な測定値は以下に記載しています。

## 収録GPUアーキテクチャ

- [SM120](sm120/)
- [SM89](sm89/)

Stable構成、実機検証結果、ビルド条件、実機検証済みバイナリのSHA256などは、各GPUアーキテクチャのREADMEを確認してください。

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

SM120 / SM89 それぞれでGPUアーキテクチャに合わせて候補を比較し、Public v1 Stable の構成を個別に検証・固定しています。

#### SM120

SM120 では、Lyra2REv2 の主要な処理経路について複数の候補を比較し、性能だけでなく正しさ / 収束性を含めて最終構成を選定しています。

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

SM89 では RTX 4070 Ti 上で候補を比較し、SM89向けの Public v1 Stable 構成を選定しています。

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

SM120 の `fusion 0` 構成では1バッチあたりのハッシュカーネルは合計8本のため、SM89ではCubeHash単体のIMAD化ではなく、カーネル境界を減らす方向で構成を最適化しています。

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

これらは、過去の実運用時の記録をもとにした参考比較です。同一のソースコード・同一の開発環境・完全に同じ電力条件で行った厳密な比較ではないため、tpruvot/ccminer 原版に対して上記の割合だけ厳密に高速化・高効率化したことを示すものではありません。

### Public v1 実機検証

Public v1 は、SM120 / SM89 それぞれの実機で、ビルド、正しさ確認、実行時確認、Defenderスキャン、実プール接続を含む検証を行っています。

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

公開版の短時間確認や受理試験など、各GPUアーキテクチャ固有の詳細は、それぞれのREADMEに記載しています。

## バージョン管理

SM120版とSM89版は、それぞれ独立したリリース系列として管理します。片方だけを更新したり、将来どちらか一方の保守を終了したりすることができます。

- 既存の初回SM120版は、製品バージョン `v1.0.0` / 既存Git tag `v1.0.0`
- 現在のSM89版は、製品バージョン `v1.0.1` / Git tag `sm89-v1.0.1`
- 今後のSM120更新は `sm120-vX.Y.Z`
- 今後のSM89更新は `sm89-vX.Y.Z`

各tagは対応する公開製品のソースsnapshotを示します。

## Public v1 の範囲

Public v1 には、以下の機能はありません。

- 開発者手数料（Developer Fee）
- 寄付用マイニング
- 隠し / 予備プール
- 隠しテレメトリ
- 自動更新確認

また、Mona Miner 自体は以下のGPU設定を変更しません。

- 電力上限
- 電圧
- クロック
- ファン

必要なGPU設定は、利用者自身の管理下で外部から行ってください。

## バイナリ配布

実行用ZIPは GitHub Releases でGPUアーキテクチャ別に配布します。

- SM120: `Mona-Miner-v1-SM120-Windows-x64.zip`
- SM89: `Mona-Miner-v1-SM89-Windows-x64.zip`

実行用ZIPとソースリポジトリは分離し、実行用ZIPには動作に必要な最小ファイルのみを含めます。

## ソースコード / ライセンス

Mona Miner は tpruvot/ccminer を基にしています。GPL v3 の条件に従って利用・改変・再配布してください。

ライセンス本文:

- repository root の `LICENSE.txt`

第三者ライセンス / NOTICE:

- repository root の `licenses/JANSSON_LICENSE.txt`

GPUアーキテクチャ固有の来歴:

- 各GPUアーキテクチャディレクトリ内の `NOTICE.txt`
