# SM120 オフライン性能検証

Mona Miner v2.0.1 の公開 `main`、`82813b6800ed4b109944dfef476346d61514f79e` を比較の起点にした。RTX 5090 の電力上限は 440 W。公開 cubin 3 個は SHA256 を照合して直接ロードした。試験には決定的に生成したヘッダーを使用し、プールには接続しない。

## 初回結果

以下の差は各試行で対にした wall-clock ハッシュレートの比の中央値。通常バッチは 1,048,576 nonce。各比較は順序を反転しながら 4 組または 6 組実行した。各区間は約 1〜1.5 秒の短時間スクリーニングで、消費電力は区間前後の点サンプル。

| 試作 | 基準 | ハッシュレート差 | 結果ログ |
| --- | --- | ---: | --- |
| CubeHash 後段を BMW と融合、3 構成 | 現行 | −1.47%〜+0.09% | `screen-native.json` |
| 中央カーネルのブロックサイズ、同期数、展開を変更 | 現行 | −3.94%〜−0.79% | `screen-middle.json` |
| 中央カーネルの共有メモリのストライドを固定 | 現行 | −4.08%〜−1.33% | `screen-promising.json` |
| Lyra2 の行展開、列の先読み、その併用 | 現行 | −3.50%〜−1.36% | `screen-rows.json` |
| 1 ストリームで CUDA Graph | 現行 | +0.68%〜+1.55% | `screen-native.json`、`screen-middle.json`、`screen-submission.json` |
| 2 ストリーム、各 262,144 nonce | 同じ 262,144 nonce の直列投入 | +10.24% | `screen-submission.json` |
| 2 ストリーム、各 262,144 nonce | 現行の 1,048,576 nonce の直列投入 | −0.82% | `screen-promising.json` |

現行バッチの CUDA イベント計測では中央の融合カーネルが約 88% を占めた。中央カーネルでは最も軽い変更でも今回の実装は現行の公開 cubin を下回った。Windows の Python Driver API 検証器で観測した CUDA Graph の差には Python 側の投入コストも含まれる。C++ ランタイムでの再測定が必要。今回は製品ランタイムへ採用する変更なし。

正しさの照合は CPU Lyra2REv2 の上位 64 bit と GPU 出力を比較する。8 種類の nonce 範囲に部分ブロックと UINT32_MAX 近傍を含め、閾値での候補集合、64 件を超えたときの overflow、バッファ境界、Graph の再投入、2 ストリームの独立性も検証する。`correctness-all.json` は 16,093 件、行展開と先読みの追加試験 `correctness-rows.json` は 6,189 件の上位 64 bit 照合に成功した。実際のプールや長時間稼働での性能ではない。

## 再現手順

Windows の Python 3.10 以上、CUDA 13.4 SDK の NVRTC、SM120 の CUDA Driver、WSL の GCC を使用する。以下は生成物用の別ディレクトリから実行する。`$repo` は公開 v2.0.1 のチェックアウト、`$tool` はこの README と同じフォルダ、`$repoWsl` と `$toolWsl` はそれらを WSL から参照するパスに設定する。

```powershell
$sm120 = Join-Path $repo 'v2/sm120'
wsl.exe -- sh "$toolWsl/build_oracle_c.sh" "$repoWsl"
python -B "$tool/make_fixture.py" --snapshot-header "$sm120/source/include/mona2/job_snapshot.hpp" --input oracle-c.json --output oracle.json
python -B "$tool/build_tail.py" --sm120 $sm120 --out candidate-images
python -B "$tool/build_middle.py" --sm120 $sm120 --out middle-full
python -B "$tool/build_middle.py" --sm120 $sm120 --row-trials --out middle-rows
python -B "$tool/gpu_trial.py" --sm120 $sm120 --oracle oracle.json --candidates candidate-images --middle-images middle-full --output correctness-all.json
python -B "$tool/gpu_trial.py" --sm120 $sm120 --oracle oracle.json --middle-images middle-rows --output correctness-rows.json
```

GPU の別負荷を外した状態で、計測には `--benchmark` を指定する。同じバッチの比較では `--batch 1048576` を使い、2 ストリームの小バッチを現行バッチと交互に計測するには `--compare-dual-batch 262144 --only baseline-dual` を追加する。`--only` は計測対象だけを絞り、渡した候補の正しさはすべてを確認する。

CUDA Graph を検討する場合は、C++ でバッチサイズと target の更新、nonce の巻き戻し、overflow の再試行、ジョブ更新と所有権の切り替えを検証する。10% 級の改善を追う場合は、中央の融合カーネルについて Lyra2 のシャッフルと共有メモリ待ちを切り分け、4 レーンで 4 nonce を順に処理する構成を見直す。現行の公開 cubin 全体に対する持続的な差を採用基準にする。

## 第2回の試作と再現手順

中央カーネルの Lyra2 に絞り、nonce の割り当てと行列の格納先を個別に変更した。1・2レーンの候補は4列を各レーンにまとめ、対角置換の一部をレーン内で処理する。4レーンの行列候補では、列ごとに4行をレジスタへ置き、残りの列を共有メモリに置く。動的に選ばれる行はレジスタ側で選択し、入力行と更新行が一致する場合も既存の更新順序を維持する。両方式とも、中央カーネルの入出力と後続の BMW は既存の構成を使う。

拡張照合は26種類のヘッダーと nonce 範囲を使う。32・64・96・128・192・256の各ブロック境界と最大8,193 nonce の区間を含める。比較用の生成物ディレクトリで以下を実行する。

```powershell
wsl.exe -- sh "$toolWsl/build_oracle_c.sh" "$repoWsl" --extended
python -B "$tool/make_fixture.py" --snapshot-header "$sm120/source/include/mona2/job_snapshot.hpp" --input oracle-c.json --output oracle-extended.json
python -B "$tool/build_lyra_layout.py" --sm120 $sm120 --out layout-images
python -B "$tool/build_lyra_layout.py" --sm120 $sm120 --register --out register-images
python -B "$tool/build_lyra_layout.py" --sm120 $sm120 --tune-register --out tuned-images
python -B "$tool/gpu_trial.py" --sm120 $sm120 --oracle oracle-extended.json --middle-images register-images --middle-images tuned-images --benchmark --batch 1048576 --only lyra-register4-block32 lyra-gpr-b128-u1 lyra-gpr-b64-u1-prmt --rounds 6 --repeats 1500 --output register-final.json
```

### 第2回の結果

RTX 5090 の電力上限を 440 W に設定した状態で比較した。短時間の探索では1レーンが −51.13%、2レーンが −15.45%〜−15.79%。2レーンは共有メモリの増加で最大常駐ワープ数が8から4へ下がり、1レーンにはローカルメモリへの退避もあった。4レーンの再実装を対照とした短時間試験は −1.22% だった。

別の GPU 負荷を停止後、同じ 1,048,576 nonce の通常バッチを各構成 1,500 回連続で実行し、順序を反転しながら6組比較した。

| 構成 | ハッシュレート中央値 | 各組の現行比の中央値 |
| --- | ---: | ---: |
| 公開 v2.0.1 | 417.01 MH/s | 基準 |
| 全4列をレジスタに格納、32スレッド | 437.26 MH/s | +5.07% |
| 全4列をレジスタに格納、64スレッド、PRMT回転 | 431.41 MH/s | +3.53% |
| 全4列をレジスタに格納、128スレッド | 429.12 MH/s | +2.32% |

32スレッド版は6組すべてで速く、各組の差は +3.70%〜+5.78% だった。中央カーネルの CUDA イベント中央値は 1.830 ms から 1.704 ms。レジスタ数は150から168に増えたが、動的共有メモリが24,576 Bから0 Bになったため、ドライバが算出するSMあたりの最大常駐ワープは8から12に増えた。双方ともローカルメモリへの退避は0 B。区間後のGPU電力は双方とも約440 Wだった。

拡張CPU照合は、全レジスタ版を含む6構成と追加調整した8構成、公開版について計696,575件を通過した。テストデータはカーネルの正しさと短いオフライン処理の比較に使った。長時間のプール稼働や製品ランタイムへの組み込みは今後検証する。

生データは `layout-screen.json`、`register-screen.json`、`tune-screen.json`、`register-final.json`。最良候補の生成イメージ `lyra-register4-block32.cubin` の SHA256 は `cbdd27948a6229c79d095ff9a008e5796594c8a44e5f2e866ad02faf340cef79`。

## 第3回の試作と再現手順

第2回の全レジスタ版を比較に加え、レジスタ行の条件付き XOR 更新と、2〜4 nonce の独立したスポンジラウンドを交互に実行する構成を作った。どちらも拡張CPU照合を通過した。短時間比較では全レジスタ版を上回らなかった。

次に CubeHash と Skein の実行位置を調整した。スカラー行列処理は同じ全レジスタ版を使う。3カーネル構成では、Skein と2回目の CubeHash を BMW と同じカーネルに置いた候補を長めの比較へ進めた。2カーネル構成では BMW の候補抽出まで中央へ統合した。短時間比較ではカーネル起動を1回減らしても3カーネル構成の最良候補を上回らなかった。

第2回と同じ拡張CPU照合用の `oracle-extended.json` を使う。最終比較で用いた `register-images` は第2回に生成したイメージ。

```powershell
python -B "$tool/build_lyra_layout.py" --sm120 $sm120 --schedule --out schedule-images
python -B "$tool/build_lyra_fusion.py" --sm120 $sm120 --out fusion-images
python -B "$tool/build_lyra_fusion.py" --sm120 $sm120 --complete --out complete-images
python -B "$tool/build_lyra_fusion.py" --sm120 $sm120 --tune-tail --out tail-images
python -B "$tool/build_lyra_fusion.py" --sm120 $sm120 --vector-io --out vector-images
python -B "$tool/gpu_trial.py" --sm120 $sm120 --oracle oracle-extended.json --middle-images register-images --middle-images fusion-images --benchmark --batch 1048576 --only lyra-register4-block32 lyra-gpr-tail-skein-cube2-256 lyra-gpr-both-cube1-cube2 --rounds 6 --repeats 1800 --output final-compare.json
```

### 第3回の結果

RTX 5090、440 W 上限、1,048,576 nonce の同じバッチを各構成 1,800 回実行し、6組の順序を反転して比較した。

| 構成 | MH/s 中央値 | 各組の公開版比の中央値 |
| --- | ---: | ---: |
| 公開 v2.0.1 | 410.08 | 基準 |
| 第2回の全レジスタ版 | 430.94 | +4.75% |
| Skein と2回目の CubeHash を BMW 側へ移動 | 433.10 | +5.36% |
| 最初の CubeHash を先頭に、2回目を BMW 側へ移動 | 429.88 | +4.58% |

最良候補は、公開版には6組すべて、全レジスタ版には6組中5組で勝った。全レジスタ版に対する各組の差の中央値は +0.63%、範囲は −0.16%〜+0.91%。最良候補の短時間スクリーニングでは公開版比 +9.36% の回もあった。採用判断には長めの交互比較を使用する。

1区間を 3,000 バッチに延ばした再確認は4組で、全レジスタ版に対して中央値 +0.72%、4組中3組で速かった。2つの測定を合わせた10組では、公開版比の中央値 +5.36%、全レジスタ版比の中央値 +0.70%。全レジスタ版に対して8勝で、各組の差は −1.61%〜+1.10% だった。

最良候補の再ビルドは最初の試作と同じ SHA256 `71a67921438b67d40d28fedee009079ff635acbfcacc57114c36311f10411663` だった。6組比較に含めたルートは26ケース、609,503件の拡張CPU照合を通過。カーネル間の中間データを128ビット単位で読み書きする候補と、同じ処理を CUDA Graph で単一・2ストリーム投入する候補も照合に通った。短時間比較ではどちらも最良の3カーネル構成に対する安定した上積みが確認できなかった。製品ランタイムへの組み込みと稼働検証は今後の作業。

生データは `schedule-screen.json`、`fusion-screen.json`、`complete-screen.json`、`tune-tail-screen.json`、`vector-screen.json`、`final-compare.json`、`confirm-compare.json`。

## 第4回の試作と再現手順

第3回の最良候補を比較に加え、演算配置とレジスタ使用を変えた。

- CubeHash の語の交換を二つのバンクの XOR 索引で表現する。同じハッシュの依存関係を共有する語を対で演算する構成と、ループ展開数を変えた構成を比較した。CubeHash のラウンド数とパディングは維持する。[CubeHash 仕様](https://cubehash.cr.yp.to/submission2/spec.pdf)
- CubeHash の偶数語と奇数語を2レーンに分ける。2個の nonce ごとに2通りの入力を処理し、偶奇が交換される箇所だけシャッフルする。
- Lyra2 の初期24ラウンドを別カーネルで実行する。nonce ごとに128 B相当の中間状態をGPUメモリで渡し、行列を使う残りの処理は全レジスタ版を維持する。最後のワープでは端数 nonce の列を計算するパディングレーンも保存する。
- CubeHash の同一ラウンド内に IMAD と IADD を混在させる構成と、Lyra2 のレジスタ数を制限して CUDA 13 の共有メモリへの退避を使う構成を比較した。[NVIDIA の共有メモリ退避の解説](https://developer.nvidia.com/blog/how-to-improve-cuda-kernel-performance-with-shared-memory-register-spilling/)

```powershell
python -B "$tool/build_lyra_fusion.py" --sm120 $sm120 --cube-layout --out cube-flat
python -B "$tool/build_lyra_fusion.py" --sm120 $sm120 --cube-pair --out cube-pair
python -B "$tool/build_lyra_fusion.py" --sm120 $sm120 --split-sponge --out split-sponge
python -B "$tool/build_lyra_fusion.py" --sm120 $sm120 --cube-mix --out cube-mix
python -B "$tool/build_lyra_fusion.py" --sm120 $sm120 --spill --out spill
python -B "$tool/gpu_trial.py" --sm120 $sm120 --oracle oracle-extended.json --middle-images fusion-images --middle-images cube-flat --benchmark --batch 1048576 --only lyra-gpr-tail-skein-cube2-256 lyra-gpr-cube-word0-unroll1 lyra-gpr-cube-word1-unroll1 --rounds 6 --repeats 1800 --output final-compare.json
```

### 第4回の結果

RTX 5090、440 W 上限、同じ 1,048,576 nonce のバッチで比較した。下表は1区間を1,800バッチとして順序を反転した6組の結果。

| 構成 | MH/s 中央値 | 各組の公開版比の中央値 |
| --- | ---: | ---: |
| 公開 v2.0.1 | 413.15 | 基準 |
| 第3回の最良候補 | 437.92 | +5.61% |
| CubeHash の論理索引を固定 | 444.02 | +6.82% |
| CubeHash の論理索引を固定し、依存する語を対で処理 | 444.21 | +7.54% |

対で処理する構成は第3回の候補より6組すべてで速く、同一組での差の中央値は +1.52%。26ケースの609,503件のCPU照合を通過した。候補のSHA256は `98e3ae47bb8031c21802831bd06a61915dfcbd4521ee901270a2df29f8fba5ff`。

同じイメージを `--cube-mix` の `lyra-gpr-cube-mix0` として再生成し、SHA256が一致することを確認した。1区間を3,000バッチに延ばした4組の確認では、公開版比 +6.74%、第3回の候補比 +1.19%（どちらも各組の差の中央値）。2つの計測を合わせた10組すべてで第3回の候補を上回った。10組で公開版比 +7.04%、第3回の候補比 +1.41%。公開版と最良候補のハッシュレート中央値はそれぞれ 415.23、444.95 MH/s。

CubeHash を2レーンに分けた構成、Lyra2 の初期ラウンドを分割した構成、共有メモリ退避を使った構成は、第3回の候補より短時間比較で低速だった。共有メモリ退避は32スレッド・128レジスタの構成でローカルのスタックフレームを304 Bから192 Bに減らした。この構成のハッシュレートは公開版比 +1.38%。

Nsight Compute のGPU性能カウンターはこのWindowsアカウントでは権限不足だった。速度比較にはCUDAイベントとホストからの実時間を使った。第3回の候補から最良候補へのSASSの静的命令数は、中央カーネルで4,104から3,520、後段で5,952から5,368に減った。静的な命令一覧の件数であり、実行時のループ反復を含まない。

生データは `architecture-screen.json`、`throughput-screen.json`、`final-compare.json`、`confirm-compare.json`。

## 第5回の試作と再現手順

第4回の最良候補を起点に、中央カーネルの中で128ブロックに1ブロックのワープをサンプリングした。1,048,576 nonce のバッチ4回から1,024サンプルを取得。CUDAイベントによる中央カーネル時間は、元の候補が1.091 ms、計測用カーネルが1.177 msだった。サンプルの比率は待ち時間と計測負荷を含む経過サイクルの目安。[NVIDIA CUDA C++の `clock64()` 仕様](https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/cpp-language-support.html)

| 中央カーネル内の処理 | サンプルごとの時間割合の中央値 |
| --- | ---: |
| CubeHash | 27.4% |
| Lyra2の初期24ラウンド | 23.5% |
| Lyra2の行列処理 | 34.0% |
| Lyra2の最終12ラウンド | 12.5% |

この内訳から、Lyra2のレーン間転送と動的な行選択を優先した。1〜2レーンで扱う構成では、行列の一部または全体をレジスタに置いた。4レーンを維持する構成では、直接索引型のシャッフルをXOR型へ変更する案、行選択を条件付き命令へ変更する案、64ビット加算の一部を `mad.wide.u32` に移す案を比較した。最終ラウンドを独立させ、残り3語の出力と最後のシャッフルをコンパイラが除去できる構成も試した。

第4回と同じ拡張CPU照合用の `oracle-extended.json` と、`--cube-mix` で生成した `cube-mix` を使用する。

```powershell
python -B "$tool/build_lyra_fusion.py" --sm120 $sm120 --profile-phases --out phase-probe
python -B "$tool/profile_lyra_phases.py" --sm120 $sm120 --oracle oracle-extended.json --reference cube-mix --probe phase-probe --output phase-samples.json
python -B "$tool/build_lyra_fusion.py" --sm120 $sm120 --hybrid-lyra --out hybrid
python -B "$tool/build_lyra_fusion.py" --sm120 $sm120 --lyra-instructions --out instructions
python -B "$tool/build_lyra_fusion.py" --sm120 $sm120 --lyra-imad64 --out imad64
python -B "$tool/build_lyra_fusion.py" --sm120 $sm120 --lyra-final --out peeled
python -B "$tool/gpu_trial.py" --sm120 $sm120 --oracle oracle-extended.json --middle-images cube-mix --middle-images instructions --middle-images peeled --benchmark --batch 1048576 --only lyra-gpr-cube-mix0 lyra-gpr-lyra-xor2 lyra-gpr-lyra-xor-all lyra-gpr-lyra-final-xor-all --rounds 6 --repeats 1500 --output final-compare.json
python -B "$tool/gpu_trial.py" --sm120 $sm120 --oracle oracle-extended.json --middle-images cube-mix --middle-images instructions --benchmark --batch 1048576 --only lyra-gpr-cube-mix0 lyra-gpr-lyra-xor-all --rounds 4 --repeats 3000 --output confirm-compare.json
```

### 第5回のスクリーニング

1レーンで扱う構成ではローカルメモリへの退避が増え、2レーン・3列レジスタの構成では比較を繰り返すと第4回の候補に対する優位を確認できなかった。条件付き命令による行選択では1スレッドあたりのレジスタが168から186へ増え、最大常駐ブロックが12から8に下がった。64ビット加算を `mad.wide.u32` に移した5構成も第4回の候補を下回った。

4レーンを維持したXOR型シャッフルの構成は、2レーン離れた転送だけを置き換える案と、対象の転送をすべて置き換える案が上位に残った。どちらもローカルメモリへの退避は 0 B。シャッフルをすべて置き換えると、中央カーネルの静的なSASSではIDX型が550件から4件、XOR型が16件から562件になった。転送総数は566件で同じ。静的な件数にループの反復は含まれない。

生データは `phase-samples.json`、`lyra-screen.json`、`imad64-screen.json`、`peeled-screen.json`、`final-compare.json`、`confirm-compare.json`。

### 第5回の交互比較

RTX 5090、440 W 上限、1,048,576 nonce の同じバッチで、各構成1,500バッチずつ測る6組の比較を行った。追加効果は同じ組の第4回候補との比で判定した。

| 構成 | MH/s 中央値 | 各組の第4回候補比の中央値 |
| --- | ---: | ---: |
| 公開 v2.0.1 | 410.25 | −6.65% |
| 第4回のCubeHash候補 | 437.30 | 基準 |
| Lyra2の2レーン離れた転送をXOR型へ変更 | 440.31 | +0.73% |
| Lyra2の対象の転送をすべてXOR型へ変更 | 443.13 | +0.99% |
| XOR型と最終ラウンドの独立化を併用 | 441.69 | +0.77% |

対象の転送をすべて置き換えた試作は6組中5組で第4回の候補を上回った。中央カーネルは1スレッドあたり167レジスタを使い、ローカルメモリへの退避は0 Bだった。26ケース、740,111件のCPU参照照合が通過した。

同じソースから再生成した `lyra-gpr-lyra-xor-all.cubin` の SHA256 は `ad2bd0a1adf7f1921ec157caeae10f93dbdfee2d57971093f64118e0cf605c8e` で一致した。1区間3,000バッチに延ばした4組の確認では、各組の差の中央値は第4回候補比 +0.84%、公開版比 +7.06%。2つの計測を合わせた10組で第4回候補比 +0.93%（8組で高速）、公開版比 +7.14%（10組で高速）。公開版と最良候補のハッシュレート中央値はそれぞれ 408.34、437.48 MH/s。各比較はオフラインで行った。

## 第6回の試作と再現手順

第5回の最良候補を基準に、次の構成をCPU参照値との照合と実機の交互比較にかけた。

- Lyra2行列の1行または2行を共有メモリに配置し、残りをレジスタに置く。動的に選ばれた行の先読みと、カーネルの最小常駐ブロック数を指定する構成も比較した。[CUDAのレジスタと共有メモリの仕様](https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/compute-capabilities.html)
- Lyra2の最後の行更新を、次の処理で当該行を読む場合まで遅らせる。保留する2列または4列を、レジスタか共有メモリに置く。
- BLAKE・KeccakをLyra2と同じカーネルにまとめ、処理途中のハッシュをレジスタで受け渡す。後段のSkein・CubeHash・BMWも同じカーネルにまとめる構成を含める。
- Keccakのラウンドのループ展開数を変える。既存のKeccakラウンド数、出力の範囲、BLAKEのジョブ定数の扱いは維持する。
- RTX 5090の170 SMと各カーネルの最大常駐ブロック数から計算したバッチ境界を試す。1,048,576 nonceを基準に、1,044,480、1,044,736、979,200、913,920 nonceを比較した。各区間で同じ範囲を走査し、最後のバッチで端数を処理する。

```powershell
python -B "$tool/build_lyra_fusion.py" --sm120 $sm120 --lyra-row-placement --out rows
python -B "$tool/build_lyra_fusion.py" --sm120 $sm120 --lyra-defer-row --out deferred
python -B "$tool/build_lyra_fusion.py" --sm120 $sm120 --fuse-head --out fused-head
python -B "$tool/build_lyra_fusion.py" --sm120 $sm120 --keccak-unroll --out keccak
```

第5回の `instructions` と同じ `oracle-extended.json` を使用する。下記の比較では、各構成の1区間あたりの処理量を標準バッチ1,800回分に統一し、6組の計測で各組の比を求める。

```powershell
$prev = 'lyra-gpr-lyra-xor-all'
$fused = 'lyra-gpr-keccak-u2-complete'
python -B "$tool/gpu_trial.py" --sm120 $sm120 --oracle oracle-extended.json --middle-images instructions --middle-images keccak --benchmark --batch 1048576 --batch-route $prev --batch-route $fused --batch-variants 979200 913920 --only $prev "$prev-batch979200" "$prev-batch913920" $fused "$fused-batch979200" --rounds 6 --repeats 1800 --output final-compare.json
python -B "$tool/gpu_trial.py" --sm120 $sm120 --oracle oracle-extended.json --middle-images instructions --graph-route $prev --benchmark --batch 1048576 --batch-route $prev --batch-variants 979200 913920 --only $prev "$prev-batch979200" "$prev-batch913920" --rounds 4 --repeats 3000 --output confirm-compare.json
```

### 第6回のスクリーニング

- 行を共有メモリへ配置した9構成は前回候補比で −0.95%〜−6.03%。常駐ブロックを増やす設定では、ローカルメモリへの退避が発生した。
- 最後の行更新を遅らせる4構成のうち、4列をレジスタに保留する構成が一度は前回候補比 +0.70%だった。別の比較では −0.02%だった。
- Keccakを2ラウンドずつ展開した単一カーネル版は短時間の比較で前回候補比 +1.22%。1,800バッチ相当に延ばすと標準バッチで −0.27%、979,200 nonceで −0.14%だった。

### 第6回の交互比較

RTX 5090、440 W上限、オフラインで測定した。改善率は同じ組で得たハッシュレートの比の中央値。

| 構成 | MH/s 中央値 | 公開 v2.0.1 比 | 前回候補比 |
| --- | ---: | ---: | ---: |
| 公開 v2.0.1 | 423.11 | 基準 | −6.86% |
| 前回候補・1,048,576 nonce | 453.89 | +7.37% | 基準 |
| 前回候補・979,200 nonce | 455.87 | +7.63% | +0.29% |
| 前回候補・913,920 nonce | 455.91 | +7.80% | +0.46% |
| Keccak 2ラウンド展開・単一カーネル | 452.65 | +7.05% | −0.27% |
| 同上・979,200 nonce | 453.61 | +7.10% | −0.14% |

3,000バッチ相当に延ばした4組では、913,920 nonceが各組で前回候補の標準バッチを上回った。差の中央値は前回候補比 +1.40%、公開版比 +7.81%。6組の本測定と合わせた10組では、前回候補比 +1.04%（8組で高速）、公開版比 +7.81%（10組で高速）。913,920 nonceを次の試作の基準にする。979,200 nonceは同じ10組で前回候補比 +0.60%（9組で高速）、公開版比 +7.63%だった。

26入力ケースのCPU参照照合、命中nonce、結果バッファ上限とメモリ境界の検証を通過した。バッチ境界の追加検証では、32ビットnonceの上限までの1,048,576件を両設定で分割処理した。公開GPUカーネルとハッシュ上位64ビットおよび命中nonceが一致し、末尾8,193件のハッシュ上位64ビットはCPU参照値とも一致した。

現在の試作コードから再生成した `lyra-gpr-lyra-xor-all.cubin` は第5回のSHA256と一致した。

生データは `layout-screen.json`、`keccak-screen.json`、`batch-screen.json`、`final-compare.json`、`confirm-compare.json`、`boundary-correctness.json`。

## 第7回の試作と再現手順

第6回の最良構成（`lyra-gpr-lyra-xor-all`、913,920 nonce）を比較基準にした。

- Lyra2の中央カーネルで1 nonceを専用の4レーンまたは8レーンに割り当てる。第1 CubeHashを前段に移し、レジスタ・共有メモリ、4語を連続させる中間配置、スレッドブロックサイズを比較する。8レーン版はLyra2のG関数の状態をA/BとC/Dに分けた。
- CUDA Graphを複数ストリームに先行投入する。各ストリームはGPUバッファとページロックされたホストバッファを所有し、自分の処理が完了した時点で同じ枠に次の範囲を投入する。全枠が同時に空くのを待つ2ストリーム版も対照に含めた。[CUDA Graphの実行モデル](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/cuda-graphs.html)
- Skeinの2ブロックのThreefishを `uint64_t` で記述する。出力ブロックを全展開する構成と、鍵スケジュールを6サブキーずつ進めるループ構成を比較する。

第6回と同じ拡張CPU照合用の `oracle-extended.json` を使う。

```powershell
python -B "$tool/build_lyra_fusion.py" --sm120 $sm120 --nonce-groups --out groups
python -B "$tool/build_lyra_fusion.py" --sm120 $sm120 --skein-layout --out skein
```

専用4・8レーン版と対照構成は26ケース、827,183件のCPU参照照合を通過した。4レーン専用版の最良は短時間比較で第6回候補比 −0.64%。8レーン版のレジスタ使用量は1スレッドあたり108で、4レーン版より少なかった。スループットは第6回候補比 −42.05%〜−44.68%だった。

Skeinの8構成もCPU参照照合を通過した。出力側をループにした256スレッド版の後段レジスタ使用量は64から48、SMあたり最大常駐ブロックは4から5。後段の機械語は85,888 Bから69,760 Bになり、中央カーネルの機械語は一致した。短時間の交互比較では第6回候補比 +0.99%だった。同じ後段を、3ストリーム・304,640 nonceと2ストリーム・524,288 nonceの両方に組み合わせた。

```powershell
$prev = 'lyra-gpr-lyra-xor-all'
$skein = 'lyra-gpr-skein-native2-tail256'
$labels = @("$prev-batch913920", "$skein-batch913920", "$prev-ring3x304640", "$prev-ring2x524288", "$skein-ring3x304640", "$skein-ring2x524288")
python -B "$tool/gpu_trial.py" --sm120 $sm120 --oracle oracle-extended.json --middle-images instructions --middle-images skein --graph-route $prev --benchmark --batch 1048576 --batch-route $prev --batch-route $skein --batch-variants 913920 --ring-route $prev --ring-route $skein --ring 3x304640 --ring 2x524288 --only @labels --rounds 6 --repeats 1500 --output final-compare.json
$labels = @("$prev-batch913920", "$prev-ring3x304640", "$prev-ring2x524288", "$prev-ring3x304640-drain", "$prev-ring2x524288-drain")
python -B "$tool/gpu_trial.py" --sm120 $sm120 --oracle oracle-extended.json --middle-images instructions --graph-route $prev --benchmark --batch 1048576 --batch-route $prev --batch-variants 913920 --ring 3x304640 --ring 2x524288 --compare-ring-drain --only @labels --rounds 4 --repeats 2800 --output confirm-compare.json
```

### 第7回の交互比較

RTX 5090、440 W上限、オフラインで、1,500標準バッチと同じ総nonce数を処理する6組の比較を行った。増減率は各組のハッシュレートの比の中央値。

| 構成 | MH/s 中央値 | 公開 v2.0.1 比 | 第6回候補比 |
| --- | ---: | ---: | ---: |
| 公開 v2.0.1・直列 | 418.59 | 基準 | −9.02% |
| 第6回候補・直列 | 462.70 | +9.91% | 基準 |
| 第6回候補・3ストリーム × 304,640 nonce | 460.59 | +10.33% | +0.37% |
| 第6回候補・2ストリーム × 524,288 nonce | 461.76 | +10.10% | +1.12% |
| Skeinループ・直列 | 462.12 | +9.62% | −0.20% |
| Skeinループ・3ストリーム × 304,640 nonce | 463.91 | +10.22% | +0.29% |
| Skeinループ・2ストリーム × 524,288 nonce | 460.05 | +9.89% | −0.38% |

公開版との構成間比較では各組の比を用いる。各列の中央値は独立に算出しているため、MH/s中央値同士を割った値とは一致しない場合がある。

2,800標準バッチ相当の4組でも、304,640 nonceを3ストリームに継続投入する構成は公開版比 +10.45%、第6回候補比 +0.41%だった。6組の本測定と合わせた10組の中央値は公開版比 **+10.43%**、第6回候補比 **+0.37%**。各組の公開版比は +8.10%〜+11.35% で、7組が +10% 以上だった。公開版のハッシュレート中央値は 414.67 MH/s、3ストリーム版は 458.02 MH/s。

現行ホストのバッチ境界に合わせ、標準バッチごとに全ストリームの完了を待つ試作は、同じ4組で公開版比 +7.69%、第6回候補比 −1.89%。次の試作の基準には3ストリームと第6回のGPUカーネルを選ぶ。製品側で先行投入を評価する際は、ジョブの切り替え、難易度とDeveloper Feeの境界、候補バッファのoverflow再試行を含める。

ストリーム再利用、2つのヘッダーへの切り替え、端数バッチ、overflow、メモリ境界は合計12,289件の独立CPU参照値で検証した。実バッチサイズを使った追加試験では、32ビットnonce上限までの2,097,169件を公開GPUカーネルと照合し、ハッシュ上位64ビットと命中nonceが一致した。末尾8,193件はCPU参照値とも一致した。3ストリームで使用したGPUイメージは第6回と同じSHA256で再生成できた。

生データは `groups-screen.json`、`ring-screen.json`、`skein-screen.json`、`final-compare.json`、`confirm-compare.json`、`ring-boundary-correctness.json`。

## 第8回の試作と再現手順

第7回の `lyra-gpr-lyra-xor-all` と3ストリーム・304,640 nonceの継続投入を追加改善の基準にした。公開 v2 のREADMEにある約425 MH/sは実プール2回の総ハッシュレートの代表値。本節の割合はすべて、同じRTX 5090・440 W上限で行ったオフラインの対応比較から算出する。

- 10%級の追加改善を狙う試作では、CubeHashの32語を4または8レーンに分配した。4レーンで第1 CubeHashをLyra2へ直接つなぐ構成、独立した第1 CubeHashカーネル、第2 CubeHashからBMWに渡す構成を比較した。論理語とレーンの対応、ブロックサイズ、加算を使う実行経路を変えた。
- 数%の追加改善を狙う試作では、同じGPUイメージで2〜4ストリームの投入サイズを比較した。既定バッチ1,048,576 nonceの境界で完了を待つ条件も測る。長い比較では計測前にGPUをウォームアップする。

```powershell
python -B "$tool/build_lyra_fusion.py" --sm120 $sm120 --cube-tiles --out tiles
```

### 10%級を狙ったCubeHashの再配置

11構成すべてで、26ケース・914,255件のCPU参照照合と、ストリーム再利用時の照合を通過した。前回と同じ3ストリームで比較すると、全体のスループットは第7回候補比 −19.96%〜−36.86%だった。第1 CubeHashとLyra2を融合した最良の64スレッド版でも、標準バッチの中央カーネルは約1.65 msで、第7回候補の約1.07 msを上回った。レジスタ使用量は1スレッドあたり165と167で、どちらもローカルメモリへの退避は0 Bだった。

```powershell
$prev = 'lyra-gpr-lyra-xor-all'
$routes = @($prev) + @((Get-Content -Raw tiles/middle-manifest.json | ConvertFrom-Json).PSObject.Properties.Name)
$ringFlags = foreach ($route in $routes) { '--ring-route'; $route }
$labels = foreach ($route in $routes) { "$route-ring3x304640" }
python -B "$tool/gpu_trial.py" --sm120 $sm120 --oracle oracle-extended.json --middle-images instructions --middle-images tiles --graph-route $prev --benchmark --batch 1048576 @ringFlags --ring 3x304640 --only @labels --warmup-seconds 18 --rounds 4 --repeats 330 --output tiles-screen.json
```

### 数%級を狙ったストリームと投入サイズの調整

初回は2〜4ストリームの13設定を短時間で比較した。348,160 nonce・3ストリームの前回比 +1.82% と、228,480 nonce・4ストリームの +1.60% が上位だった。22秒のウォームアップ後、1区間を650標準バッチ相当に延ばした4組では、同じ設定の差はそれぞれ +0.02% と −0.24% だった。

ホストが1,048,576 nonceごとに完了を待つ場合も比較した。349,696 nonce・3ストリーム（末尾349,184 nonce）と262,144 nonce・4ストリームは、従来の304,640 nonce・3ストリームより標準バッチを均等に分割できる。

```powershell
$labels = @("$prev-ring3x304640", "$prev-ring3x261120", "$prev-ring3x348160", "$prev-ring3x304640-drain", "$prev-ring3x349696-drain", "$prev-ring4x262144-drain")
python -B "$tool/gpu_trial.py" --sm120 $sm120 --oracle oracle-extended.json --middle-images instructions --graph-route $prev --benchmark --batch 1048576 --ring 3x304640 --ring 3x261120 --ring 3x348160 --ring 3x349696 --ring 4x262144 --compare-ring-drain --only @labels --warmup-seconds 24 --rounds 6 --repeats 1400 --output final-compare.json
```

ウォームアップ後の4組と、1区間1,400標準バッチ相当に延ばした6組を合わせた結果。

| 構成 | MH/s 中央値 | 公開 v2.0.1 比 | 第7回候補比 |
| --- | ---: | ---: | ---: |
| 公開 v2.0.1 | 425.16 | 基準 | −9.40% |
| 3ストリーム × 304,640 nonce、継続投入 | 468.12 | +10.38% | 基準 |
| 3ストリーム × 261,120 nonce、継続投入 | 470.39 | +10.40% | −0.001% |
| 3ストリーム × 348,160 nonce、継続投入 | 467.90 | +10.32% | −0.035% |
| 3ストリーム × 304,640 nonce、毎バッチ完了待ち | 455.42 | +6.98% | −2.93% |
| 3ストリーム × 349,696 nonce、毎バッチ完了待ち | 459.46 | +8.04% | −2.17% |
| 4ストリーム × 262,144 nonce、毎バッチ完了待ち | 461.19 | +8.27% | −1.83% |

各比率は同じ組の比の中央値。毎バッチ完了待ちの中では、262,144 nonce・4ストリームが304,640 nonce・3ストリームを10組すべてで上回り、比の中央値は +1.13%。継続投入の採用候補は第7回の304,640 nonce・3ストリームを維持する。今回の425.16と468.12 MH/sはオフライン比較の中央値。

改修した検証器で、既存のLyra2前段分割ルート5構成の回帰照合も通過した。第7回のGPUイメージの再生成SHA256は一致した。

生データは `ring-shapes-screen.json`、`tiles-screen.json`、`phase-screen.json`、`final-compare.json`、`legacy-prefix-correctness.json`。

## Windows実機確認用ビルド

v2.1.0の `mona-miner-sm120-v210` は、第7回のGPUイメージを3ストリーム・304,640 nonceの継続投入で使う。1つの割り当ては最大7,311,360 nonceで、割り当て内のチャンクを順次回収する。次のジョブ、target、採掘ロールへ進む時点までに、すべての投入を回収する。候補の上限超過は既存の範囲分割で再試行する。Feeの量子は従来と同じ64 × 1,048,576 nonce、2%の計算も同じ。

`--lyra-instructions` で生成した `lyra-gpr-lyra-xor-all.cubin` を `embed_preview.py --candidate` に渡す。このhelperは既存のproducerと試用カーネルのSHA256を確認し、COFF objectとGPUカーネル契約を生成する。通常のGPUビルド引数に、以下を追加する。

```powershell
python -B "$tool/embed_preview.py" --candidate "$images/lyra-gpr-lyra-xor-all.cubin" --out "$build/preview-images"
# Configure の追加引数:
# -DMONA2_PREVIEW_IMAGE_OBJECT=<build>/preview-images/mona2_preview.obj
# -DMONA2_PREVIEW_INCLUDE=<build>/preview-images
cmake --build $build --config Release --target mona-miner-sm120-v210 preview_gpu_tests
ctest --test-dir $build -C Release --output-on-failure
```

出力した `mona-miner-sm120-v210.exe` は既存の起動引数を使う。実プールを使わない比較には `--benchmark --benchmark-batches 2000`、実機のCPU照合には `preview_gpu_tests.exe` を使用する。

Windows x64の試用版はLLVM-MinGWでビルドした。RTX 5090・440 W上限で、公開版構成の再ビルドと2,000標準バッチずつ4組を比較した。ハッシュレートの中央値は418.01 → 458.88 MH/s、対応比の中央値は +9.76%。CPU/mock CTest 9件と、SM120でのCUDA Graph再利用・target変更・nonce上限・overflow再試行の照合は通過した。ローカルStratumでは、USERとDeveloperの認証と提出、両者のジョブと難易度の切り替え、完了workの98対2の配分を確認した。
