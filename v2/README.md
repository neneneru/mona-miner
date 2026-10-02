# Mona Miner Public v2

## 概要

- Monacoin（Lyra2REv2）向けの CUDA 13.4 対応 NVIDIA CUDA マイナーです。
- Public v2 は SM120 を対象とし、RTX 5090 で実機検証しています。
- 固定 Developer Fee 2% を採用しています。

## 対応環境

- SM120（RTX 50 シリーズ）
- 実機検証: RTX 5090 / Windows 11 x64
- CUDA 13.4.x

## 手数料

- Developer Fee: 2%
- completed-work 基準で USER 98q → DEVFEE 2q（q = 64）を割り当てます。

Developer Fee の未実行分を後からまとめて回収する debt / catch-up はありません。
Developer 接続が利用できない場合も USER mining を継続し、復旧後に追加徴収しません。
USER mining が利用できない場合は DEVFEE-only の GPU mining を継続しません。

## パフォーマンス

RTX 5090 の30分実プール検証では、2回の測定で以下を確認しています。

- total: 423.778 / 426.614 MH/s
- USER 実効: 415.352 / 418.114 MH/s
- 代表値: 約 425 MH/s total / 約 417 MH/s USER 実効
- Public v1 SM120 約395.93 MH/s比: total 約 +7.3%、USER 実効 約 +5.3%

上記は実機検証した RTX 5090 とその検証環境における値です。
すべての SM120 GPU で同一性能を保証するものではありません。

## 主な最適化

Public v2 SM120 の最終構成は以下です。

`N02 + EXP01 + R02_CUBE2_TAIL_FUSION_B64`

GPU処理ステージ間の境界削減、同じ処理主体で完結できる範囲の融合、
不要な中間表現・転送の削減、後段tail処理の融合を行っています。

詳細なGPU image / source provenance、検証済みSHA256、正しさ試験、
Compute Sanitizer、実プール検証は [SM120 README](sm120/) を確認してください。

## 起動

Public v1 と同じ `-o / -u / -p` CLIでUSER poolを指定できます。

```text
mona-miner.exe -a lyra2v2 -o stratum+tcp://HOST:PORT -u USER -p PASS
```

`-p / --pass` の要否は接続先poolの仕様に従います。VIP Poolを利用する一般的な
ケースではpasswordを指定します。

Release package にはVIP Pool向けの起動例として `start_vippool.bat` を同梱します。
BATの利用は必須ではなく、対応するStratum poolをCLIから指定して起動できます。

通常のconsole出力はPublic v1と同じ60秒集約表示を基本とします。
詳細なJSON event streamは `--json` で利用できます。

## 共通ポリシー

- 隠し / 予備プールへの接続、テレメトリ送信、自動更新確認はありません。
- Mona Miner 自体は GPU の電力制限・電圧・クロック・ファン設定を変更しません。
- USER pool の接続先・worker・password は利用者が指定します。

## 収録

- [SM120](sm120/)
