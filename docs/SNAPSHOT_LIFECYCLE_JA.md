# Snapshot・診断・再初期化（0.4.0）

## 所有と共有

1インスタンスの `begin` / `refresh` / `read` / 各種設定 / ゼロ設定 / `end` は1つの制御コア・実行コンテキストが所有します。通常は `setup/loop` に置きます。同じコアの割り込みとforegroundから、これらを同時に呼ばないでください。`begin/end/attachIndex/detachIndex` はforeground専用です。

別コアやログ用割り込みで許可される取得は **`latest() const`** と、その戻り値に対する操作です。保存済みSnapshotをSDKのstriped spinlockで短時間だけ保護してコピーします。ロック中はローカル割り込みを止めるため、同じコアの割り込みによる自己デッドロックを避けられます。PIO・推定器・ログ出力をロック内で実行しません。volatileだけの共有や、非atomicな構造体を読み続けるseqlockは使いません。

`latest()` はPIOを読まず、推定器も更新しません。保存値と現在の64bit時刻から `Stale` のみ再判定します。複数フィールドを使うときは1回の戻り値を保存してください。別々に呼んだgetter同士は別世代になる場合があります。

## データ

| メンバー | 単位・意味 |
|---|---|
| `position` | int64_t、実測4逓倍カウント（100 PPRなら400 step/rev） |
| `position_substeps` | int64_t、1 step=64の補間位置。実測分解能が増えるわけではない |
| `speed` | float、step/s。設定された速度周期の間は保持 |
| `timestamp_us` / `timestamp_us64` | 同じ取得時点の32bit/64bit時刻 |
| `speed_timestamp_us` | 最後の速度更新または停止検出時刻 |
| `sequence` | 公開ごとに増えるuint32_t。end/beginでリセットしない。周回する |
| `max_age_us` | この保存値に適用される鮮度上限。0で判定無効 |

推定器内部の補間範囲はint64_t substepなので、実測int64_t stepの全範囲より狭くなります。範囲外では補間を飽和させて知らせ、実測カウントは独立に保持します。実測カウントそのものもint64_t限界では飽和します。ゼロ再設定・beginで基準を作り直せます。既存のPIOカウンタの最大取得間隔・最大移動量の制約は残ります。

## 診断

`sample.has(Pico_PIO_Encoder::フラグ)` で調べます。

| フラグ | 意味 |
|---|---|
| `Initialized` | begin成功後。未初期化・end後は立たない |
| `VelocityWarmingUp` | 初回・停止からの再始動・方向反転後に速度アンカーを準備中 |
| `VelocityValid` | 2回の速度側遷移観測を得たか、停止タイムアウトを満たしたゼロ速度 |
| `Stale` | 読む時点で最終取得から `setMaxSampleAgeUs()` を超過 |
| `UpdateLate` | 直近の取得間隔が同じ上限を超過。次の正常取得で解除 |
| `SpeedSaturated` | 保持している速度の内部固定小数点値が上限/下限に到達。次の非飽和速度更新・停止で解除 |
| `PositionSaturated` | 位置/補間の計算が飽和。ゼロ設定またはbeginまで保持 |

`positionValid()` は Initialized かつ非Stale・非ReadFailed・非PioFault・非PositionSaturated。
`speedValid()` は Initialized・VelocityValid かつ非Stale・非ReadFailed・非PioFault・非SpeedSaturated。
UpdateLateは取得履歴なので、取得が再開して現在値が新鮮でも立ち得ます。速度の通常の保持期間を更新遅れとは扱いません。鮮度上限の既定値は10msで、位置1kHzなら例えば3msに設定します。これらのフラグは信号欠落・ノイズ・PIOの最大間隔超過をすべて検出するものではありません。

## 終了・再初期化

`bool end()` は所有コアから呼びます。PIOのSMを停止、FIFOを消去し、index IRQ登録を外します。SMの空きを次のbeginで再利用し、そのブロックの最後のインスタンスを終了すると全SMと32命令をSDKへ返します。GPIOのmux/pull設定は維持します。他ライブラリは再利用時に自分でGPIOを設定します。

繰り返しendは成功扱い。初期化済みインスタンスへの別コアからのendはfalseです。破棄は所有コアで行い、他コアのreaderも先に終了してください。コピー・moveは禁止です。デストラクタでもendを呼び、所有コア違反はassertします。インスタンス生成は通常のグローバル初期化またはforegroundで行ってください。

再beginではカウント・速度・準備状態・index履歴・校正学習履歴を初期化し、速度周期・停止時間・鮮度上限・確定済み位相設定は維持します。indexは再attachが必要です。自動校正を有効にしたまま再接続すると、新しい観測から学習を開始します。他ライブラリとPIOブロックを取り合う初期化はアプリ側で直列化してください。



0.4.0では初期化に `beginConsecutive(A, B)` または `beginNonConsecutive(A, B)` を使います。Snapshotには方式、ピン、SM数、取得失敗・再試行回数も含まれます。`ReadFailed` は最後の取得失敗、`PioFault` はFIFO待ちタイムアウトです。失敗時は直前の値・時刻を保持し、PIO故障は終了・再初期化で復帰します。
