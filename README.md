# Mona Miner

Monacoin（Lyra2REv2）向けの NVIDIA CUDA マイナーです。
tpruvot/ccminer を基に、CUDA 13.4.x / SM120 向けに最適化しています。

## 公開ソース

- [Public v1 / SM120](v1/sm120/): RTX 5090 / Windows 11 x64 で実機検証
- ソースの識別tag: `v1.0.0`
- Developer Fee: なし
- 実用参考値: 約395.93 MH/s、電力上限440 W、約0.900 MH/J

最適化の内容、比較条件、ビルド方法、検証済みバイナリのSHA256は
[SM120 README](v1/sm120/)を確認してください。
Public v1の案内は [v1/README.md](v1/README.md) に記載しています。
実行用ZIPは [GitHub Releases](https://github.com/neneneru/mona-miner/releases/tag/v1.0.0) で配布しています。

## ソースコード / ライセンス

ソースはversion / architectureごとに収録しています。
GPL v3の条件に従って利用・改変・再配布してください。
[LICENSE.txt](LICENSE.txt)、[第三者ライセンス](licenses/)、
[SM120 NOTICE](v1/sm120/NOTICE.txt) にライセンスと来歴を記載しています。

## 無保証

本ソフトウェアは無保証で提供されます。
検証済みGPU以外の動作・性能を保証するものではありません。
