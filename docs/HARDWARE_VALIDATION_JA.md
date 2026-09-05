# 実機確認手順

この手順は公開ライブラリ利用者向けです。特定PCのポート番号や以前の書き込み状態は前提にしません。過去の結果は [VALIDATION_RESULTS.md](VALIDATION_RESULTS.md)、Snapshot/終了処理の契約は [SNAPSHOT_LIFECYCLE_JA.md](SNAPSHOT_LIFECYCLE_JA.md) を参照してください。

## エンコーダ単体

1. A/BをGPIOへ接続し、GND・信号電圧・プルアップを確認する。連続番号のGPIOなら `beginConsecutive(A, B)`、離れたGPIOなら `beginNonConsecutive(A, B)` を選ぶ。
2. `examples/multirate` の初期化方式とGPIOを配線に合わせて変更し、実際のボードを選んでビルド・書き込む。非連続方式の導入例は `examples/nonconsecutive` にもある。
3. 手で回して、実測countが方向に応じて増減することを確認する。1回転のカウントがエンコーダ仕様のPPR×4に合うか調べる。
4. 位置を1kHzで取得し、シリアルの `1/2/3` で速度周期10/20/5msを切り替える。位置が全周期で更新され、速度が設定された更新時刻まで保持されることを確認する。
5. 停止、低速、反転、ゼロ設定を試す。速度準備中と有効なゼロ速度を区別し、低速では必要に応じてidle timeoutを延ばす。

## Snapshot・ライフサイクル

`examples/multicore_snapshot` は制御コアで取得し、ログコアでlatestだけを読みます。所有コア以外からread/refreshを呼ばないでください。

- 取得を鮮度上限より長く止めたとき、latestのStaleが立つ。
- 再取得時はUpdateLateが立ち、次の通常取得で解除される。
- end後はInitializedが落ち、再beginで位置・速度・準備状態が初期化される。
- end→beginを繰り返してもPIO不足にならない。4SMを使用し、中間の1つを終了して再利用できる。最後の終了後は全SMとプログラム領域が解放される。
- indexを使う場合は終了時にIRQ登録とゼロ予約が消え、再attachができる。物理Z捕捉の精度は別に試験する。
- `Pull::None` で再初期化したとき、以前のプルアップ・プルダウンが残らない。`Pull::Up` と `Pull::Down` の相互切り替え、省略時の `Pull::Up` も確認する。手順は [プル設定検証](PULL_CONFIGURATION_VALIDATION.md) を参照。

## Nidec/INA219ベンチ（任意）

検証済み配線はRP2040-Zero、A28/B27、DIR26、PWM15、BRAKE14、INA219のSDA4/SCL5、アドレス0x40。100PPR×4=400step/rev、Z相なしです。この例は他のモーターへそのまま適用するための汎用ドライバではありません。

PWMは負論理で、analogWrite=32767が出力0、0が最大出力。BRAKE LOWで制動、HIGHで解除です。試験は停止状態から始め、最初は低い指令・短時間で実際の向きとカウント符号を確認します。

1. `examples/nidec_wiring_check` を使い、ブレーキONでINA219電圧とA/B入力を確認する。
2. `examples/nidec_bench` を使う場合は、先に [BENCH_PROTOCOL_JA.md](BENCH_PROTOCOL_JA.md) を読む。
3. 電源電圧、モーター固定、配線、停止手段を自分の機体で確認し、短い正逆回転から試す。以前の別機体での試験条件はこの機体の許可や定格を意味しない。
4. count差分/実測時間と推定速度を比較し、missed・I2Cエラー・faultを記録する。同じA/B信号を比較した結果を絶対精度と呼ばない。
5. 終了時はSを送り、出力0・ブレーキONを確認する。

## ビルド

リポジトリ直下で実行します。ポート番号は `arduino-cli board list` で確認してください。

```sh
make -C test/host run
arduino-cli compile --fqbn rp2040:rp2040:waveshare_rp2040_zero --library . examples/multirate
arduino-cli compile --fqbn rp2040:rp2040:waveshare_rp2040_zero --library . examples/nidec_bench
```

UF2を書き込むと既存ファームウェアを置き換えます。必要なバックアップは使用環境で管理してください。公開パッケージには個人の既存ファームウェアや復元用データを含めません。
