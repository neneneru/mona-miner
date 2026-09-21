# Mona Miner Public v1

## 概要

Mona Miner Public v1 は、Monacoin の Lyra2REv2 向け NVIDIA CUDA マイナーです。

対応アルゴリズムは Lyra2REv2（`-a lyra2v2`）のみです。Public v1 では、GPUアーキテクチャごとにネイティブビルドを分けています。

- SM120 — RTX 5090 で実機検証
- SM89 — RTX 4070 Ti で実機検証

実機検証済みGPUは上記2機種ですが、実行時の対応判定は個別の製品名ではなく、SM（GPUアーキテクチャ）を基準とします。同じCUDAアーキテクチャを持つGPUは設計上の対応範囲に含まれますが、個別の動作・性能をすべて検証しているわけではありません。

## 収録GPUアーキテクチャ

- [SM120](sm120/)
- [SM89](sm89/)

Stable構成、実機検証結果、ビルド条件、実機検証済みバイナリのSHA256などは、各GPUアーキテクチャのREADMEを確認してください。

## バージョン管理

SM120版とSM89版は、それぞれ独立したリリース系列として管理します。片方だけを更新したり、将来どちらか一方の保守を終了したりすることができます。

- 既存の初回SM120版は、製品バージョン `v1.0.0` / 既存Git tag `v1.0.0`
- 初回SM89版は、製品バージョン `v1.0.0` / Git tag `sm89-v1.0.0`
- 今後のSM120更新は `sm120-vX.Y.Z`
- 今後のSM89更新は `sm89-vX.Y.Z`

既存の `v1.0.0` tag は初回SM120版の履歴として保持し、移動・書き換えは行いません。

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
