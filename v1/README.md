# Mona Miner Public v1

概要
----------------------------
Mona Miner Public v1 は Monacoin の Lyra2REv2 用 NVIDIA CUDA miner です。

対応アルゴリズムは Lyra2REv2 (`-a lyra2v2`) のみです。

architecture固有の対応GPU、実機検証状況、ビルド条件、validated binary SHA256などは、
各architectureのREADMEを確認してください。


収録architecture
----------------------------
- [SM120](sm120/)


GPU設定について
----------------------------
Mona Miner 自体は次のGPU設定を書き換えません。

- Power Limit
- 電圧
- クロック
- ファン

※必要なGPU設定は、利用者自身の管理下で外部から行ってください。
※Mona Miner本体はこれらの設定を変更しません。


Public v1 の範囲
----------------------------
Developer Fee: なし
Donation mining: なし
Hidden/fallback pool: なし
Hidden telemetry: なし
Update check: なし
GPU power/clock/voltage/fan の自動変更: なし


バイナリ配布
----------------------------
実行用ZIPは GitHub Releases 側でarchitecture別に配布します。

runtime ZIPとsource repositoryは分離し、
runtime ZIPには実行に必要な最小ファイルのみを含めます。


ソースコード / ライセンス
----------------------------
Mona Miner は tpruvot/ccminer を基にしています。
GPL v3 の条件に従って利用・改変・再配布してください。

ライセンス本文:
repository root の LICENSE.txt

第三者ライセンス / NOTICE:
repository root の licenses/JANSSON_LICENSE.txt

architecture固有のprovenance:
各architectureディレクトリ内の NOTICE.txt
