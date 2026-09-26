/* tizix.h - tzcc(--tizix-user)で tizix 外部コマンドを書くための stdio。
 *
 *   tizix の user/stdio.h は ((fn_x_t)drv_tbl[N])(...) という関数ポインタ
 *   マクロで DRIVER(0x9000)を叩くが、tzcc は関数ポインタ codegen 未対応。
 *   代わりに crt0_tizix.s のトランポリン(_fopen 等 → ld hl,(0x90xx); jp (hl))
 *   に素の名前で解決させる。ここはその宣言だけ。
 *
 *   FILE / size_t は tzcc の組込み型キーワード(int 相当・2byte)。
 *   putchar/getchar/puts/printf は crt0_tizix.s 内の実装(カーネル低位
 *   ベクタ 0x3E/0x41 経由)。fopen 以降は drv_tbl トランポリン。
 *
 *   使い方: tizix の user/xxx.c 冒頭の #include "stdio.h" を残したまま、
 *   `make tizixcmd CMD=xxx` がこのファイルを stdio.h として横に置いてビルドする。
 */
#ifndef _TIZIX_H
#define _TIZIX_H

#ifndef NULL
#define NULL ((void *)0)
#endif
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* vfs 書込み系の戻り: 0=OK / 0xFF=permission denied / 1..19=FatFs FRESULT */
#define FS_DENIED 0xFF

/* --- crt0_tizix 実装(カーネル低位ベクタ) --- */
int  putchar(int c);
int  putc(int c);
int  getchar(void);
int  puts(char *s);
int  printf(char *fmt, ...);

/* --- libtzc tzcout: printf を引き込まない軽量出力 (tizix #31) ---
 *   コマンドのメッセージが %s と 10 進数だけなら printf(546B)ではなく
 *   これを使う。prs は改行なし、puts は改行あり。 */
void prs(char *s);
void prnum(unsigned n);
void prnuml(unsigned long v);

/* --- drv_tbl(0x9000)トランポリン: FS / dir / sched --- */
FILE *fopen(char *path, char *mode);
int   fclose(FILE *fp);
int   fread(void *ptr, int size, int nmemb, FILE *fp);
int   fwrite(void *ptr, int size, int nmemb, FILE *fp);
int   fputs(char *s, FILE *fp);
char *fgets(char *buf, int n, FILE *fp);
int   fgetc(FILE *fp);
int   fputc(int c, FILE *fp);
int   fseek(FILE *fp, long off, int whence);
long  ftell(FILE *fp);
int   feof(FILE *fp);
int   ferror(FILE *fp);
int   fflush(FILE *fp);
int   kbhit(void);
int   getc_timeout(int ticks);

void  proc_block(void);
void  proc_wake(unsigned char blk);

int   opendir(char *path);       /* 0=ok / -1=fail */
int   readdir(char *name13);     /* 0=end / 1=file / 2=dir */
void  closedir(void);
unsigned long readdir_size(void);

int   mkdir(char *path);
int   unlink(char *path);
int   rename(char *from, char *to);

/* rsyslog: /var/log/message へ 1 行追記(drv_tbl[46])。
 * カーネルが "YYYY-MM-DD HH:MM:SS [pid] " を前置して msg を続け、256 文字で
 * 切って改行する。 */
void  klog(char *msg);

/* df(#56): sel=0 総容量 KB / sel=1 空き KB(drv_tbl[47])。
 * 取れなければ 0xFFFFFFFF。トランポリンは tzcdf.s(使うコマンドだけ引く)。 */
unsigned long kfs_df(unsigned sel);

/* --- #35: 同期 exec(プロセス分割) ---
 *   krun_wait("/bin/foo.bin", pack, argc) で子を起動し、終了まで待つ。
 *   pack は NUL 区切りのトークン列("tok0\0tok1\0…")、argc はその個数。
 *   戻り: 1=起動して終了 / 0=空きブロック不足 / 0xFF=ファイル無し。
 *
 *   tizix にメモリ保護は無いので、**子は親のバッファを絶対アドレスで直接
 *   書ける**。アドレスは 10 進文字列にして pack に載せ、子側でポインタへ戻す。
 *   大きなプログラムを「本体 + コマンド」へ割るときの基本形。
 *   子の .BIN は毎回 FAT から読むので 1 打鍵ごとには呼ばないこと。 */
unsigned char krun_wait(char *fname, char *argpack, unsigned char argc);

/* --- #35: プロセス間の共有メモリ ---
 *   tizix にメモリ保護は無く block2..7 は同じ 64KB に並ぶ。**配列のアドレスは
 *   すでに絶対番地**(codegen が IY を足す)なので、子へは `(unsigned)buf` を
 *   そのまま渡してよい ── 足し直してはいけない。
 *   ただし受け取った側は **それを tzcc のポインタとして参照してはいけない**
 *   (自分の IY が足されて自分の block を壊す)。下の 3 本を通すこと。 */
unsigned getbase(void);                 /* 自プロセスのベース(IY)。診断用 */
unsigned char peek(unsigned addr);      /* 絶対番地の 1 バイト読み */
void poke(unsigned addr, unsigned char v);   /* 絶対番地の 1 バイト書き */
/* tzcc のポインタ参照は 1 バイト r/w しか無い(`*p` の int* 未対応)ので、
 * 語(16bit)を受け渡すときはこの 2 本を通す。 */
unsigned peekw(unsigned addr);
void pokew(unsigned addr, unsigned v);
void absmove(unsigned dst, unsigned src, unsigned n); /* 絶対番地間 LDIR */
void absmovd(unsigned dst, unsigned src, unsigned n); /* 同 LDDR。**dst > src で重なるときはこちら** */
/* [p, p+n) から ch を前方に探す(CPIR)。見つかった番地 / 無ければ終端 p+n */
unsigned absscan(unsigned p, unsigned ch, unsigned n);
/* [p-n, p) を後方に探す(CPDR)。**最後の ch の 1 つ次** / 無ければ p-n。
 * 「行頭を求める」がそのままこの形になる。 */
unsigned absrscan(unsigned p, unsigned ch, unsigned n);

/* --- #36: オーバーレイ(自前で半ロード) ---
 *   子プロセスを起こさずに、**自分の空間へコード片を読み込んで呼ぶ**。
 *   ブロックを 1 個も余分に食わないので「本体 + コマンド」を子プロセスで
 *   やる場合の弱点(実行の瞬間に空きブロックが要る)が無い。
 *     1. オーバーレイは `make tizixovl CMD=x OVLADDR=0x1600` でその番地向けに
 *        リンクする(読み込む先のオフセットと一致必須。tzcc の PIC は
 *        IY + リンク時オフセット を実アドレスにするため)。
 *     2. 親は base+OVLADDR へ fread し、callovl(base+OVLADDR, arg) で呼ぶ。
 *   戻り値は overlay 側 main() の戻り値。 */
unsigned callovl(unsigned addr, unsigned arg);

/* --- #38: 追加ブロック(像に含めない作業領域) ---
 *   .BIN 先頭 32B の予約ヘッダに「像とは別に欲しいブロック数」を書いておくと、
 *   kexec がそのぶん多くブロックを確保する。**SP と argv は像側の上端に置かれる**
 *   ので、追加ブロックは丸ごと作業領域として使える。
 *   大きな配列を像に持つと .BIN が実データの無いゼロで太り、ロードも遅くなる。
 *     tzcc:  make tizixcmd CMD=x XBLK=1
 *     使う側: xb = getxbase(); n = getxsize();  → [xb, xb+n) が自由に使える
 *   getxsize() が 0 なら宣言していない(getxbase() の値を使ってはいけない)。
 *   中身は初期化されない。使う前に自分で埋めること。 */
/*   ★上端の扱いは従来どおり: **SP と argv[] はプロセス最上端**に置かれる。
 *     追加ブロックは「像とスタックの間に増えた空き」なので、getxsize() の
 *     **全部を使ってはいけない** ── 上端はスタックが下りてくる。
 *     目安は 1.5KB 前後を残す(FatFs を叩くなら特に)。 */
unsigned getxbase(void);
unsigned getxsize(void);

#endif
