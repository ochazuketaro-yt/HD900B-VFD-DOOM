# ビルド

`dist/` のバイナリは `build/build.sh` で再現できます。ツールチェーンは Bootlin の `mips32el--uclibc--stable-2024.05-1`(GCC 13.3、uClibc-ng 1.0.50)です。

## 必要なもの(ホスト)

bash、make、python3、xz、tar、sha256sum(Linux)。

## 手順

```sh
build/build.sh                       # ツールチェーンを取得(SHA-256 検証)して、ビルド
# すでに展開済みのツールチェーンを使う場合:
TOOLCHAIN_DIR=/opt/mips32el--uclibc--stable-2024.05-1 build/build.sh
```

生成物(`dist/`):

| ファイル | 内容 |
|---|---|
| `fbdoom_vfd` | fbDOOM + VFD 描画・入力・ロゴ/メニュー(`src/fbdoom`) |
| `vfdmux` | VFD の表示プログラム(`src/tools/vfdmux.c`) |
| `vfdctl` | VFD の簡易制御(`pt HD900B` で表示を戻す) |
| `evlog` | キー入力ログ(`/dev/input/event0〜2`、読み取りのみ) |
| `tt` | 時計の確認用 |
| `SHA256SUMS` | 上記のハッシュ |

コンパイルフラグ: `-march=mips32r2 -mtune=24kc -mfp32 -mhard-float -fno-pie`、静的リンク。 `fbdoom_vfd` は `NOSDL=1`(SDL を使わない)で、 `-Os -DNORMALUNIX -DLINUX -DSNDSERV`。

## 変更点の確認

`patches/fbdoom-vfd.patch` は、元の fbDOOM のソースに対する差分(変更・追加したファイルのみ)です。

## 注意

- 完全静的リンクの実行ファイルです。uClibc-ng(LGPL-2.1)を静的リンクしているため、別の C ライブラリで再リンクしたい場合は、`build/build.sh` のコンパイラを差し替えてください([`NOTICE.md`](../NOTICE.md))。
- `src/fbdoom/` には、使っていない DOOM のコード(SDL 向けの入力・音など)も含まれます。元のソース構成を保つためです。
