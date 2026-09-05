# Pico_PIO_Encoder 0.4.0 公開ガイド

## 公開情報

| 項目 | 値 |
|---|---|
| ライブラリ・クラス名 | Pico_PIO_Encoder |
| ヘッダ | Pico_PIO_Encoder.h |
| Pico SDKのCMakeターゲット | pico_pio_encoder |
| 版・タグ | 0.4.0 / v0.4.0 |
| リポジトリURL | https://github.com/Suzu-Gears/Pico_PIO_Encoder |
| メンテナー | Ryota SUZUKI / suzuki.ryota.ua@tut.jp |
| カテゴリ・Arduinoアーキテクチャ | Sensors / rp2040 |
| ライセンス | 本体BSD-2-Clause、PIO/生成ヘッダBSD-3-Clause |
| 配布ZIP | Pico_PIO_Encoder-0.4.0.zip |

GitHub About:

> PIO encoder library for RP2040/RP235X and Arduino/Pico SDK, with explicit wiring modes, independent position and velocity updates, and cached multicore snapshots.

Topics: `arduino`, `pico-sdk`, `rp2040`, `rp2350`, `raspberry-pi-pico`, `pio`, `encoder`, `motor-control`.

Releaseタイトルは **Pico_PIO_Encoder 0.4.0**。
本文には [RELEASE_NOTES_0.4.0.md](RELEASE_NOTES_0.4.0.md) を使用します。

## 公開前の確認

1. 上記URLのリポジトリを用意し、README・library.properties・リンクのURLが一致することを確認する。
2. ソースをコミットしてpushし、GitHub Actionsの全ジョブを確認する。ローカルのビルド成功はGitHub上での実行成功とは別。
3. `library.properties` の版と一致するコミットに `v0.4.0` を付け、タグをpushする。
4. GitHub Releaseを作成し、配布ZIPとSHA-256を添付する。リポジトリとReleaseを一般公開し、ログインなしで取得できることを確認する。
5. Release ZIPをArduino IDEへ導入し、サンプルをビルドする。SDKサンプルも展開先でビルドする。
6. Library Managerへの掲載は[公式登録手順](https://github.com/arduino/library-registry#adding-a-library-to-library-manager)に従って申請する。

このガイドは公開操作そのものを実行しません。リポジトリの現在の可視性、タグ、Release、Library Manager登録状況を仮定していません。

## GitHub Actions

[ci.yml](../.github/workflows/ci.yml) はpush・pull request・手動実行で次を確認します。

| ジョブ | 内容 |
|---|---|
| host-tests | C++17の推定器・GPIO検証テスト |
| arduino | arduino-pico 6.0.0、Pico / Pico 2で全Arduinoサンプル |
| pico-sdk | Pico SDK 2.1.1、Pico / Pico 2 ARMで両配線方式のサンプルとプル設定試験ファームウェアをビルド |
| lint | Arduino形式・Library Manager提出向けの検査 |

ワークフローの権限は `contents: read` です。RP2350実機、電気的な信号品質、モーターの動作はCIでは検証しません。

RoboMaster_Control_Arduinoで使用している `Suzu-Gears/arduino-library-deploy` は、版の検証やRelease作成を自動化する別の役割です。
必要なら将来導入できますが、Arduinoライブラリとして公開するための必須条件ではありません。
本リポジトリにはまず上記の検証CIを同梱し、Release作成はタグと内容を確認して実施します。

参考：[Arduino compile-sketches](https://github.com/arduino/compile-sketches)、[Arduino Lint action](https://github.com/arduino/arduino-lint-action)、[Pico SDK](https://github.com/raspberrypi/pico-sdk)。

## ローカル確認

```sh
make -C test/host run
arduino-lint --compliance specification --library-manager submit .
arduino-cli compile --fqbn rp2040:rp2040:rpipico --library . examples/position_speed
arduino-cli compile --fqbn rp2040:rp2040:rpipico2 --library . examples/nonconsecutive
cmake -S pico_sdk_example -B build-sdk -DPICO_BOARD=pico
cmake --build build-sdk
cmake -S pico_sdk_example -B build-sdk-pico2 -DPICO_BOARD=pico2
cmake --build build-sdk-pico2
```

SDKとツールチェーン、Arduino CLIとarduino-picoコアを事前に設定します。SDKサンプルは連続・非連続の2ターゲットをビルドします。
Windowsでg++の補助DLL不足が出る場合は、同じツールチェーンのbinディレクトリをPATHへ追加します。

## 配布範囲

`src`、`examples`、`pico_sdk_example`、テストのソース、`docs`、`.github`、`LICENSES`、README/CHANGELOG/ライセンス、Arduino/CMakeメタデータを含めます。
`docs/data` は以前の試験時の生ログです。開発時の識別名が含まれていても測定記録として保持します。

`.git`、ビルド成果物、UF2、ローカルファームウェアのバックアップ、個人用作業メモは含めません。
[LICENSE](../LICENSE)、[THIRD_PARTY_NOTICES](../THIRD_PARTY_NOTICES.md)、[LICENSES](../LICENSES/)を省略しないでください。

確認結果は [RELEASE_VALIDATION.md](RELEASE_VALIDATION.md)。仕様は [Arduino Library Specification](https://docs.arduino.cc/arduino-cli/library-specification) を参照してください。
