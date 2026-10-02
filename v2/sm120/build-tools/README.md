# Public SM120 GPU executable build

Windows x64 / Python 3.10以上 / Visual Studio 2022 C++ Build Tools / CMake 3.26以上 /
CUDA SDK 13.4.xを使用します。CMake、PythonをPATHへ設定してください。
GPU executableのbuild自体にはGPUやpool credentialsは不要です。

repo rootでPowerShellから実行します。CUDA SDKの場所は自分のインストール先を
指定してください。以下の標準パスは例で、helper内にmachine pathは埋め込みません。

```powershell
$sm120 = (Resolve-Path 'v2/sm120').Path.Replace('\', '/')
$build = "$sm120/build/public-gpu"
$object = "$build/mona2_images.obj"
$cudaSdk = 'C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.4'

python "$sm120/build-tools/embed_images.py" --output $object
if ($LASTEXITCODE -ne 0) { throw 'image generation failed' }

cmake -S "$sm120/source" -B $build -G 'Visual Studio 17 2022' -A x64 `
  -DMONA2_FROZEN_GPU=ON "-DMONA2_CUDA_ROOT=$cudaSdk" `
  "-DMONA2_IMAGE_OBJECT=$object" "-DMONA2_GENERATED_INCLUDE=$sm120/source/include"
if ($LASTEXITCODE -ne 0) { throw 'configure failed' }

cmake --build $build --config Release --target mona-miner-prototype --parallel
if ($LASTEXITCODE -ne 0) { throw 'build failed' }

python "$sm120/build-tools/embed_images.py" --output $object --verify-only `
  --executable "$build/Release/mona-miner-prototype.exe"
if ($LASTEXITCODE -ne 0) { throw 'post-build image verification failed' }

& "$build/Release/mona-miner-prototype.exe" --help
if ($LASTEXITCODE -ne 0) { throw 'help smoke test failed' }
```

出力は `v2/sm120/build/public-gpu/Release/mona-miner-prototype.exe` です。
名称はvalidated CMake targetをそのまま保持しています。ここで生成したEXEは
release済みbinaryやfreeze receiptのEXEとbyte一致すると主張するものではありません。
`--help` はGPU / network resourcesを作成しません。mining開始・実プール接続・
GPU再qualificationはこのbuild手順には含めません。

## Image / payload / object契約

helperはPython標準ライブラリだけでAMD64 COFF objectを生成します。
nvcc、device sourceの再compile、private validation runnerは使用しません。

入力順はN02_EXP01_PRODUCER、R02_ACCEPTED、R02_CUBE2_TAIL_FUSIONです。
各入力のSHA256をqualified値と照合し、既存 `source/include/image_contract.hpp` の
payloadサイズ・offsetsが固定契約と一致することを確認します。

| Image | Bytes | Offset |
|---|---:|---:|
| N02_EXP01_PRODUCER | 8,935,376 | 0 |
| R02_ACCEPTED | 3,995,800 | 8,935,424 |
| R02_CUBE2_TAIL_FUSION | 4,827,400 | 12,931,328 |

- 256-byte境界へzero paddingを挿入。payload合計: 17,758,728 bytes
- COFF machine: AMD64、read-only `.rdata` section、256-byte alignment
- runtimeが参照する外部C symbol: `mona2_images`
- relocationsなし、timestampは0。object生成は同一入力で決定的
- 書き出し後にobject全体を再読込し、header/section/symbol/string table/payloadを照合
- source cubinsを再hashし、各payload sliceとpadding、元のoffset契約を再確認
- 不一致時は非zero終了。configure/buildへ進めないでください
- `--verify-only` は既存objectを上書きせず再検証します
- `--executable` はリンク済みAMD64 PE内のread-only sectionにqualified payloadが
  一意に存在し、256-byte alignmentと全payload bytesが一致することを検証します

`MONA2_GENERATED_INCLUDE` には既存の `source/include/` を渡します。
qualified contract headerを再生成・変更する必要はありません。
生成物は既存gitignoreが対象とする `build/` 配下へ置き、commitしないでください。

## CPU / mock再検証

```powershell
python -m unittest discover -s v2/sm120/build-tools -p 'test_*.py'
cmake -S v2/sm120/source -B v2/sm120/build/cpu-mock `
  -G 'Visual Studio 17 2022' -A x64 -DMONA2_FROZEN_GPU=OFF
cmake --build v2/sm120/build/cpu-mock --config Release --parallel
ctest --test-dir v2/sm120/build/cpu-mock -C Release --output-on-failure
```

各コマンドのexit codeを確認してください。helper試験にはobject/header/payload改変、
不正cubin、不正contractを拒否するCPU-only試験が含まれます。
