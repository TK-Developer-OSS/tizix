/* kexec.h - 外部コマンドの fork/ロード */
#ifndef KEXEC_H
#define KEXEC_H

/* argv[] を渡す本体(全アーキ)。argpack = NUL 区切りトークン列 "tok0\0tok1\0…"
 * を argc 個。sh がパスの絶対化・クォート処理まで済ませて渡す。argc==0 なら
 * argpack は無視。戻り値: 先頭block / slot(0 と 0xFF 以外) / 0=空き無し・サイズ過大 /
 * 0xFF=無し。#48: x86-ia16 / m68k-mega の src/sh.c もここを使う(x86 は内部で
 * トークンを空白で繋いで kexec_file へ渡すので、引用符内の空白は保たれない)。 */
unsigned char kexec_argv(const char *fname, const char *argpack, unsigned char argc);

#if defined(ARCH_X86_IA16) || defined(ARCH_M68K_MEGA)

/* FAT 上の fname を空きセグメントへロードしてスケジューラに登録。
 * 戻り値: 1..7=slot / 0=空き無し / 0xFF=ファイル無し。 */
unsigned char kexec_file(const char *fname, const char *arg);

#else

/* 旧 2 引数入口。arg 全体を 1 トークン扱いで argv 化する(init.c の sh 起動等)。 */
unsigned char kexec_file(const char *fname, const char *arg);

/* #35: 子を起動して終了まで待つ(同期実行)。sh 以外のコマンドから使う入口。
 * メモリ保護が無いので、子は argv で渡した絶対アドレスから親のバッファを
 * 直接書ける ── 「本体 + コマンド」へのプロセス分割はこれで組む。
 * 子の .BIN は毎回 FAT から読むので 1 打鍵ごとに呼ばないこと。
 * 戻り: 1=起動して終了した / 0=空きブロック不足 / 0xFF=ファイル無し */
unsigned char krun_wait(const char *fname, const char *argpack, unsigned char argc);

#endif

/* DRIVER.BIN を block1(0x9000) へ常駐ロード(reloc 無し、非プロセス)。
 * シェル起動時に一度だけ呼ぶ。戻り値: 0=成功 / 0xFF=無い・壊れ。 */
unsigned char kload_driver(void);

#endif
