# Mona Miner

Monacoin（Lyra2REv2）向けの NVIDIA CUDA マイナーです。
tpruvot/ccminerを基に、CUDA 13.4.xと各GPUアーキテクチャ向けに最適化しています。
ソースはversion / architectureごとに収録しています。

## Public v1

- [SM120 / v1.0.0](v1/sm120/): RTX 5090で実機検証、tag `v1.0.0`
- [SM89 / v1.0.1](v1/sm89/): RTX 4070 Tiで実機検証、tag `sm89-v1.0.1`
- Developer Fee: なし
- SM120の実用参考値: 約395.93 MH/s、電力上限440 W
- SM89の実プール長時間平均: 約160.024 MH/s、通常約185〜188 W

改善内容と比較条件は [Public v1 README](v1/README.md)、
ビルド方法・検証済みバイナリのSHA256は各アーキテクチャのREADMEを確認してください。
実行用ZIPは [GitHub Releases](https://github.com/neneneru/mona-miner/releases) で配布しています。

## 共通ポリシー

- 対応アルゴリズムはLyra2REv2のみです。
- 隠しプール、テレメトリ、自動更新確認はありません。
- GPUの電力上限・電圧・クロック・ファン設定は変更しません。

## ソースコード / ライセンス / 無保証

GPL v3の条件に従って利用・改変・再配布してください。
詳細は [LICENSE.txt](LICENSE.txt)、[第三者ライセンス](licenses/)、
各アーキテクチャのNOTICEを確認してください。本ソフトウェアは無保証です。
実機検証済みGPU以外の動作・性能を保証するものではありません。
