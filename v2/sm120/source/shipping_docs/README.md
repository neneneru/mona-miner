# Mona Miner Public v2.0.1 — SM120

Monacoin（Lyra2REv2）向けのWindows x64 / NVIDIA CUDAマイナーです。
SM120向けに構成を固定し、RTX 5090で実機検証しています。
公開ソースの識別tagは `sm120-v2.0.1` です。

## 起動

```text
mona-miner.exe -a lyra2v2 -o stratum+tcp://HOST:PORT -u USER -p PASS
```

VIP Pool向けの起動例は `examples/start_vippool.bat` にあります。
接続先、worker、passwordは利用者が指定してください。
`-p / --pass` の要否はpoolの仕様に従います。認証情報はmining専用のものを使い、
Webログイン・出金・wallet管理用の秘密情報は指定しないでください。
BATに保存した認証情報は平文になり、CLI引数もローカルのprocess toolsから見える場合があります。

自動化用途では `--credentials-stdin` も利用できます。
通常CLIの認証情報指定と併用はできません。

## 表示

- 既定は60秒ごとの集約表示です。
- `--interval N` で集約間隔を変更できます。
- `--all` は集約表示に代えてUSER share結果を表示します。
- `--json` は詳細な診断情報を出力します。
- 接続やjob待機の時間は、採掘開始後のMH/s集計に含めません。

## Developer Fee

Developer Feeはcompleted-work基準で固定2%です。
完全な処理単位はUSER 98q → DEVFEE 2q（q = 64 native batches）です。
途中終了時の割合は2%を下回ることがあります。
停止や再起動によって未実行Feeのdebt / catch-upは発生しません。
Developer接続が使えない場合はUSER miningを継続し、USERが使えない場合は
DEVFEE-onlyのGPU miningを継続しません。

## 動作範囲

1プロセスで明示指定した1 GPUを使用し、USERとDeveloperのStratum接続を維持します。
pool targetはサーバーのdifficultyとjobから設定します。
helpと不正CLIはnetwork / GPU resourcesを作成せず、benchmarkはpoolに接続しません。
GPU設定の自動変更、隠しpool、テレメトリ、自動更新確認はありません。

ビルド手順は [build-tools/README.md](../../build-tools/README.md)、
検証済みバイナリのSHA256と性能比較は [SM120 README](../../README.md) を確認してください。
GPL-3.0-or-later／無保証。ライセンスと第三者NOTICEもあわせて確認してください。
