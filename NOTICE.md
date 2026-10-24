# NOTICE — サードパーティ・ソフトウェアとライセンス

`hd900b-vfd-doom` は、複数のライセンスのコードを含みます。ファイルごとに、先頭の表記が優先されます。

| コンポーネント | ライセンス | 備考 |
|---|---|---|
| `src/tools`(vfdmux / vfdctl / evlog / tt)、`device`、`tools`、`build` | **GPL-3.0-or-later**(`LICENSE`) | 本リポジトリで新規に書いたもの。`tools/hdtelnet.py` は `hd900b-media-station` / `hd900b-youtube-relay` と同じもの |
| `src/fbdoom`(DOOM ソース由来) | **GPL-2.0-or-later**(`licenses/GPL-2.0.txt`) | id Software の DOOM ソース(1997 年公開、1999 年に GPL で再公開)、Simon Howard らの Chocolate Doom 由来のファイル、fbDOOM(framebuffer 向けの移植)に基づきます。各ファイルの著作権表示・ライセンス表記は維持しています。本プロジェクトの追加・変更は、主に `i_video_fbdev.c`(VFD 描画、ロゴ、メニュー、HUD、エイムアシスト)、`i_input_tty.c`(evdev 入力)、`i_timer.c`(`times()` による時間取得)で、元のツリーとの差分は `patches/` にあります |
| uClibc-ng 1.0.50(C ライブラリ、`dist/` の各バイナリに静的リンク) | **LGPL-2.1**(`licenses/LGPL-2.1.txt`) | Bootlin の GCC 13.3 ツールチェーンに同梱。LGPL-2.1 §6 のため、完全なソースとビルド手順(`src/`、`build/`)を公開しています。別の C ライブラリで再リンクしたい場合は `build/build.sh` のコンパイラを差し替えてください |
| libgcc | GPL-3.0 + GCC Runtime Library Exception 3.1 | 例外条項により、リンクしたプログラムのライセンスに追加の制約は生じません |

## 配布していないもの
- **DOOM のゲームデータ(`DOOM.WAD` / `DOOM1.WAD` など)**: id Software / ZeniMax Media の著作物で、GPL の対象ではありません。ご自身で正規に入手したものを使ってください。
- **Bootlin のクロスツールチェーン**: 公式サイトから取得します(`build/fetch.sh`、SHA-256 検証付き)。
- **純正ファームウェア**(DvdPlayer など): 一切含みません。変更もしません。

## 商標・非関与
DOOM は id Software / ZeniMax Media の商標です。HiMedia、Realtek、その他の名称は各社の商標です。本プロジェクトはこれらの会社と提携・承認関係になく、非公式です。
