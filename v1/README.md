# Mona Miner Public v1

Monacoin（Lyra2REv2）向けの NVIDIA CUDA マイナーです。
Public v1にはDeveloper Fee・寄付用マイニングはありません。

## 収録ソース

- [SM120](sm120/): CUDA 13.4.x、RTX 5090 / Windows 11 x64で実機検証
- ソースの識別tag: `v1.0.0`
- 実用参考値: 約395.93 MH/s、電力上限440 W、約0.900 MH/J

SM120向けネイティブビルドです。同じSM120でもすべてのGPUの動作・性能を保証しません。
構成、測定条件、ビルド手順、検証済みEXEとZIPのSHA256は
[SM120 README](sm120/)を確認してください。

## 共通ポリシー

- 隠しプール、テレメトリ、自動更新確認はありません。
- GPUの電力上限・電圧・クロック・ファン設定は変更しません。
- USER poolの接続先と認証情報は利用者が指定します。

## ソースコード / ライセンス / 無保証

tpruvot/ccminerを基にしています。GPL v3の条件に従って利用・改変・再配布してください。
詳細は [LICENSE.txt](../LICENSE.txt)、[第三者ライセンス](../licenses/)、
[SM120 NOTICE](sm120/NOTICE.txt)を確認してください。本ソフトウェアは無保証です。
