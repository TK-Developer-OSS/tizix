#ifndef _KMEM_H
#define _KMEM_H

/* ==================================================================
 * tizix カーネルメモリ配置  (このセッションで実態に合わせて改訂)
 *
 * RAM は 0x8000 から。4KB ごとにブロックに分割する。
 *   ブロック0        = カーネル RAM(ワークメモリ)
 *   ブロック1        = DRIVER 常駐(飛び地。0x9000 固定。プロセスではない)
 *   ブロック2〜7      = プロセス用(各 4KB がプロセスの実アドレス空間)
 *
 * ブロック番号 n の先頭アドレス = 0x8000 + n * 0x1000
 * 外部コマンドはストレージから読み出し、割り当てたブロックへロードして
 * 実行する。iy にはそのブロック先頭(base)が入り、iy_reg.py が補正した
 * ラベル参照が実アドレスに解決される。
 * ================================================================== */

#define RAM_BASE        0x8000
#define BLOCK_SIZE      0x1000
#define NBLOCK          8                 /* ブロック 0〜7 */

/* ブロック番号 → 先頭アドレス */
#define BLOCK_ADDR(n)   (RAM_BASE + (n) * BLOCK_SIZE)

/* ブロック割り当て:
 *   block0    = カーネル RAM(idle/シェル文脈)
 *   block1    = DRIVER 常駐(飛び地。0x9000 固定。プロセスではない)
 *   block2〜7 = プロセス枠
 * DRIVER を block0 直上(連続番地)に置くのは、実機で 64KB 上端(block7)を
 * プロセス/拡張のために空けておくため、およびカーネル境界に隣接させて
 * 将来のチューニング余地を残すため。block7 を離島ドライバにすると上端が
 * 塞がる(64KB の壁)。 */
#define PROC_BLOCK_MIN  2
#define PROC_BLOCK_MAX  7
#define DRIVER_BLOCK    1                 /* DRIVER 常駐ブロック */
#define DRIVER_BASE     0x9000            /* = BLOCK_ADDR(1) */

/* ------------------------------------------------------------------
 * ブロック0(カーネル RAM: 0x8000〜0x8FFF, 4KB)の実配置
 *
 *   0x8000-0x830A  ~778B  C statics(DATA/BSS)。DATA_LOC=0x8000。
 *                         内訳: FATFS fs ~560B(FatFs の共有セクタ
 *                         バッファ win[512] が主) / fat_cat buf 64B /
 *                         ff.c DirBuf 等 / redir_on,in_on 2B。
 *                         ※ FF_FS_TINY=1 なので FIL/DIR は小さく
 *                           (FIL=34B, DIR=40B)、スタックを圧迫しない。
 *   0x830B-0x838A   128B  cmdtbl_name[block][16] (KW_CMDNAME)
 *   0x838B-0x83CA    64B  cmdtbl_args[block][8]  (KW_CMDARGS)
 *   0x83CB-0x83FF    53B  空き(gap1 残り)
 *   0x8400-0x8418    25B  スケジューラ PCB(下記)。絶対番地固定。
 *   0x8419-0x8518   256B  VFS ノード配列(KW_VTREE, 16×16B)
 *   0x8519           1B   fd 採番カウンタ(KW_NEXTFD)
 *   0x851A-0x8521    8B   per-block 出力ルート表(KW_OUTROUTE)
 *   0x8522-0x8525    4B   Unix 秒 epoch(KW_EPOCH_SEC, 32bit)
 *   0x8526           1B   サブ tick(KW_SUB_TICK, 0-99)
 *   0x8527-0x8528    2B   生 tick(KW_TICKS, 16bit)
 *   0x8529-0x8530    8B   proc blocked[block]  (KW_BLOCKED)
 *   0x8531-0x8538    8B   proc wakepend[block] (KW_WAKEPEND)
 *   0x8539-0x85C1  ~137B  カーネルパイプ struct kpipe (KW_PIPE)
 *   0x85C2-0x85ED   ~44B  外部 ls 用 struct kdir (KW_LSDIR)
 *   0x85EE-0x85FF   ~18B  空き
 *   0x8600-0x86F0  ~240B  DRIVER の fd_table[6](37B×6)+ fd_inited
 *                         (user/driver.c が 0x8600/0x86F0 固定で参照)
 *   0x86F1-0x86FF   ~15B  空き
 *   0x8700-0x891F  ~544B  外部 sh(/bin/sh.bin)のセッション状態 (KW_SHSTATE)
 *                         cwd / 行編集バッファ / argpack / scr1・scr2 等。sh は
 *                         書込み可能な file-scope static を持てない(iy_reg 未変換
 *                         + gsinit 非実行)ため、絶対番地固定のこの帯に置く。
 *                         3 ブロック(11838B)化でスタックが ~180B しかないため、
 *                         トークン/パス作業バッファもここへ逃がす。user/sh.c の
 *                         struct sh_state と一致させること。末尾 2B は #45 の
 *                         ヒストリ位置(実体は /root/history、ここは写し)。
 *                         使用 540B / 枠 544B。
 *   0x8920-0x894F    48B  カレントディレクトリ (KW_CWD)。プロセスではなくカーネルが
 *                         持つ。sh は cd/pwd をベクタ経由で叩くだけで、パス解決には
 *                         関与しない(src/fatcmd.c kpath 参照)。
 *   0x8950-0x8C4F   768B  パス正規化スクラッチ (KW_PATHS)。8 ブロック × 2 枠 × 48B。
 *                         ブロック(プロセス)ごとに分けるのは、共有 1 枚だと
 *                         「解決 → プリエンプト → 別プロセスが上書き → 別の
 *                         ファイルを開く」競合が起きるため。2 枠は kfs_rename
 *                         が src/dst を同時に要るから。
 *   0x8C50-0x8C67    24B  DRIVER の生ブロックデバイス fd 付帯情報 (KW_DEVFD, #32)。
 *                         kind[8] + sect[8](16bit)。fd_table(0x8600)を広げると
 *                         fd_inited(0x86F0)に当たるので、別枠でここに置く。
 *   0x8C68-0x8DFF   408B  空き(socket bridge 記述子 / 将来枝の実体用)
 *   0x8E00-0x8FFF   512B  カーネルスタック(SP=0x9000 から下へ、512B 枠)
 *
 * ------------------------------------------------------------------
 * スタックについて(重要・改訂点):
 *   旧記述は block7(0xF000-0xFFFF)/SP=0xFFFE だったが、これは crt0.s の
 *   実値 SP=0x9000 と矛盾していた(旧コメントの遺物)。実態は block0 内、
 *   SP=0x9000 から下へ。以前は 0x8419 まで約 2.9KB を無条件にスタック
 *   予約していたが、実測上カーネル文脈の最深は FatFs auto(cp で FIL×2=
 *   68B)+ ff.c 呼出深で 1KB 未満。3KB 予約は過大だった。
 *
 *   よって「スタックは 512B(0x8E00-0x8FFF)」を運用上の約束とし、
 *   0x8419-0x8DFF をデータ(VFS/socket/将来枝)に開放する。
 *   ※ SP の初期値そのものは 0x9000 のまま(512B 枠の頂上)。crt0.s に
 *     コードの変更は無い。512B を超えて潜るとデータ域(上端 0x8DFF)を
 *     破壊するので、深い再帰・ISR 多重ネストに注意。溢れたら SP 起点を
 *     上げる(=スタック枠を広げる)か、データ配置を下げる。
 *     VFS ノードは最下端 0x8419 に置いてあり、スタック(頂上 0x9000)
 *     から最も遠い ── 実測 1KB 未満に対し 2.5KB の余裕があるため、
 *     512B を多少超えても VFS 破壊には至りにくい配置になっている。
 * ------------------------------------------------------------------ */

/* ------------------------------------------------------------------
 * ps 用コマンド名/引数テーブル (block0 の gap1、0x830B-0x83CA)
 *   本家 Unix の struct proc の p_comm と同じ発想: プロセス自身のメモリ
 *   ではなく中央のカーネル表に持つ。z80 系 kexec_argv がロード完了時に
 *   fname の basename(拡張子除く)/ argpack の先頭 argc トークンを
 *   PIDTBL と同じブロック番号でここへ書く。ps はここを直に覗くだけ
 *   (メモリ保護が無いので可能)。block0(sh)/block1(DRIVER) 用のスロット
 *   は使わない(ps 側で block0 を "(sh)" と決め打ち、block1 は非走行)。
 *   PLAT_FLAT32 のポートは同じ形の表を kwork に持つ(下の節)。x86-ia16 は持たない。
 * ------------------------------------------------------------------ */
#define KW_CMDNAME      0x830B            /* cmdtbl_name[block][16] */
#define KW_CMDNAME_LEN  16
#define KW_CMDARGS      0x838B            /* cmdtbl_args[block][8]  */
#define KW_CMDARGS_LEN  8

/* ------------------------------------------------------------------
 * スケジューラ PCB (block0 内 0x8400-0x8418, 25B)
 *   pid_tbl[block] @ 0x8400  block 0..7 (8B)   0=free, nonzero=占有
 *   sp_tbl[block]  @ 0x8408  block 0..7 (2B×8=16B)  退避 SP
 *   current        @ 0x8418  現走行中ブロック番号 (1B)
 *   block0 は常時占有(idle/シェル文脈)。round-robin は 0..7 を巡回し、
 *   pid==0(free) と pid==PID_DRIVER(block1) を飛ばす。block7 も巡回対象
 *   (プロセス枠に復帰)。
 * ------------------------------------------------------------------ */
#define KW_PIDTAB       0x8400            /* pid_tbl[block] block 0..7 */
#define KW_PIDTAB_SIZE  8
#define KW_SPTBL        0x8408            /* sp_tbl[block] block 0..7 */
#define KW_SPTBL_SIZE   16
#define KW_CURRENT      0x8418            /* current block (1B) */

/* ------------------------------------------------------------------
 * VFS 名前空間 + fd 採番 (block0 の開放帯)
 *   PCB 直後 0x8419 から。C static ではなく絶対番地固定(SDCC statics の
 *   成長が PCB に衝突するのを避けるため。PCB と同じ流儀)。
 * ------------------------------------------------------------------ */
#define KW_VTREE        0x8419            /* struct vnode[KW_VTREE_N] */
#define KW_VTREE_N      16                /* 16 ノード(固定3 + 動的13) */
#define KW_VTREE_SIZE   256              /* 16 × 16B                  */
#define KW_NEXTFD       0x8519            /* fd 採番カウンタ (1B)      */

/* ------------------------------------------------------------------
 * per-block 出力ルート表 (block0 の開放帯)
 *   out_route[block] を KW_CURRENT で引き、putchar が出力先を分岐する。
 *   グローバル redir_on(io.c)は既存の builtin "> file"(FAT)用に温存。
 *   こちらはプロセス毎のルート ── "> /dev/null &" のように背景プロセス
 *   だけ出力を捨てる/後に UART やパイプへ向ける、の土台。
 *   起動時 kexec_file が CONSOLE で初期化、必要なら sh が上書き。
 * ------------------------------------------------------------------ */
#define KW_OUTROUTE     0x851A            /* out_route[block] 0..7 (8B) */
#define KW_OUTROUTE_N   8

/* ---- 時刻(Unix タイムスタンプ、100Hz 積算) --------------------
 *   epoch_sec : 1970/1/1 00:00:00 からの通算秒(32bit)。初期値 0。ISR が
 *               sub_tick を 100 数えるごとに +1。将来ブートで NTP 秒を
 *               date <秒> 経由で流し込む。ファイル日時等でも使う共通基盤。
 *   sub_tick  : 0-99。100Hz を秒に畳むサブカウンタ。
 *   ★カーネルは epoch を「持って刻んで渡す」だけ。除算・乗算は一切持たない
 *     (前回 date を builtin にして 32bit 除算ランタイムが ROM を 224 セクタ
 *      超に押し上げ起動不能になった。変換は外部 DATE.BIN 側=減算のみ)。
 *   32bit read/write は Z80 で非アトミック。time_get/time_set は di/ei で囲む。 */
#define KW_EPOCH_SEC    0x8522            /* Unix 秒 (4B, 32bit)       */
#define KW_SUB_TICK     0x8526            /* 0-99 サブカウンタ (1B)    */

/* ---- 生 tick(100Hz, delay 用) -------------------------------------
 *   旧: kernel.c の C グローバル `volatile unsigned int ticks`。
 *       これは _DATA 先頭(0x8000)に置かれ、FatFs の共有セクタバッファ
 *       win[512] と同じ帯に重なるため、FAT アクセスのたびに破壊された
 *       (getticks() が 0x8001 等のゴミを返す実害あり)。
 *   新: PCB/EPOCH と同じく絶対番地固定へ追い出す。 */
#define KW_TICKS        0x8527            /* 生 tick (2B, 16bit)       */

/* ---- プロセス wait/wake (5a。パイプ/SD 段の block/wake 土台) ----------
 *   blocked[block]  : 1 = ブロック中。z80 crt0.s sched_pick が飛ばす。
 *   wakepend[block] : proc_block 前に proc_wake が来た時の取りこぼし防止。
 *   両方 crt0.s が起動時にゼロクリア。更新は di/ei で囲む(1 バイト store)。 */
#define KW_BLOCKED      0x8529            /* blocked[block] 0..7 (8B)  */
#define KW_WAKEPEND     0x8531            /* wakepend[block] 0..7 (8B) */
/* ブロック n を新しいプロセスに渡す前に park 状態を落とす(#72)。park 中に
 * kill(sh の Ctrl+C 含む。どちらも pid_tbl を 0 にするだけ)されたプロセスの
 * blocked[n]=1 が残ると、次の占有者が sched_pick に永久に飛ばされる。
 * wakepend の残りは「proc_block が 1 回素通りする」だけで、呼び出し側は
 * 条件を見直すループなので無害 ── ROM 節約のためこちらは落とさない。
 * 起動時の初期化は z80 は crt0.s の PCB 初期化、それ以外は kwork が BSS。 */
#define PROC_CLEAR_BLOCKED(n) (((volatile unsigned char *)KW_BLOCKED)[n] = 0)

/* 生モード(tty の ISIG 無効に相当)のスロット番号。0 = 通常(Ctrl+C は割り込み)。
 * con_setraw(PLAT_FLAT32: src/io.c / z80: DRIVER の drv_conraw)が立て、con_break が見る。
 * 要求元が終わっていれば con_break は通常どおり扱い、sh が前景ジョブの終了後に
 * 0 へ戻す(Unix のシェルが前景ジョブの後で端末モードを戻すのと同じ)。
 * KW_CONRAW の番地は z80 / PLAT_FLAT32 それぞれの節で決める(x86 は持たない)。 */
#define KCONRAW             (*(volatile unsigned char *)KW_CONRAW)

/* ---- カーネルパイプ (#27: 4KB ブロックバッファ版。同時 1 本) --------------
 *   struct kpipe(src/pipe.c): active/wblk/rblk/bufblk/weof/rgone/ovf/head/tail
 *   のヘッダ 11B のみ(データ本体は pipe_setup が確保するプロセス枠ブロック
 *   BLKBASE(bufblk) の 4096B、wrap 無し)。
 *   writer の putchar → ROUTE_PIPE → pipe_putc(4096B 到達で ovf、-1)。
 *   reader の getchar → pipe_is_reader → pipe_getc(空なら proc_block、
 *   writer 終了 or ovf で EOF)。crt0.s 起動時に header をゼロクリア。
 *   x86 は Step 14(pipe.c を stub 化して無害にビルド)。 */
#if !defined(ARCH_X86_IA16) && !defined(PLAT_FLAT32)
#define KW_PIPE         0x8539            /* struct kpipe ヘッダ (11B, 0x8539-0x8543)。
                                          * 旧 128B buf[] 廃止で 0x8545-0x85C1 は空き。 */
#define KW_CONRAW       0x8544            /* 生モードのスロット (1B)。crt0 のクリア範囲の末尾 */
#define KW_LSDIR        0x85C2            /* struct kdir(外部 ls 用、~44B, 0x85C2-0x85ED) */
#define KW_SHSTATE      0x8700            /* 外部 sh のセッション状態 (~544B, 0x8700-0x891F)。
                                          * カーネルは使わない(番地予約のみ)。DRIVER の
                                          * fd_table(0x8600-0x86F0)より上に置くこと。
                                          * scr1/scr2 込み。user/sh.c の struct sh_state と一致必須。 */
/* ---- 生ブロックデバイス fd の付帯情報(#32)-----------------------------------
 *   DRIVER の fd_table[] は 0x8600 固定・37B×6 で、直後の fd_inited(0x86F0)まで
 *   詰まっている。構造体を広げると当たるので、デバイス fd の 2 項目だけ別枠に置く。
 *     kind[fd] : 0=通常の FAT ファイル / 1=/dev/null / 2=/dev/fda / 3=/dev/fdb
 *     sect[fd] : 生デバイスの現在位置(512B セクタ番号。16bit で足りる)
 *   user/driver.c が絶対番地で参照する(fd_table と同じ流儀)。 */
#define KW_DEVFD_KIND   0x8C50            /* unsigned char[8] */
#define KW_DEVFD_SECT   0x8C58            /* unsigned      [8] (16bit) */
/* ---- カレントディレクトリ(カーネル所有)------------------------------------
 *   cwd はプロセスではなくカーネルが持つ。外部コマンドは相対パスをそのまま
 *   drv_tbl へ渡し、カーネル側の入口(kdir_open / kfs_* / redir_begin /
 *   in_begin / DRIVER の drv_open)が kpath() で cwd 起点に解決する。
 *   これにより sh が「どの引数がパスか」を推測する必要が無くなる
 *   (旧 is_path_cmd / prev_optarg / is_kv / skip_first を撤去した理由)。
 *   単一の cwd で足りるのは cd を打つのが sh だけだから。背景ジョブ(&)は
 *   起動時点の cwd で解決されるので、従来(sh が起動時に絶対化)と同じ挙動。 */
#define KW_CWD          0x8920            /* カレントディレクトリ文字列 (48B) */
#define KW_CWD_MAX      48

/* パス正規化スクラッチは **ブロック(プロセス)ごと** に持つ。
 *   単一の共有バッファにすると次の競合が起きる: プロセス A が kpath で解決 →
 *   f_open を呼ぶ前にプリエンプトされる → プロセス B が同じバッファを上書き →
 *   A が B のパスを開く。パイプ(`cat a | tee b`)や `&` で実際に到達しうる。
 *   スケジューラは KW_CURRENT に実行中ブロックを持つので、それで添字する。
 *   1 ブロックあたり 2 枠(kfs_rename が src/dst を同時に要るため)。
 *   KW_PATHS[blk][slot] = KW_PATHS + blk*KW_PATH_STRIDE + slot*KW_PATH_MAX  */
#define KW_PATHS        0x8950            /* 8 ブロック × 2 枠 × 48B = 768B   */
#define KW_PATH_MAX     48                /* CWD_MAX と同じ。超過は解決失敗   */
#define KW_PATH_SLOTS   2
#define KW_PATH_STRIDE  (KW_PATH_MAX * KW_PATH_SLOTS)   /* 96B / ブロック */

/* ------------------------------------------------------------------
 * ネットワーク中継バッファ(2026-09-16、outbound TCP のみ・同時1本)
 *
 *   client socket #1(port 50/51、iosim.c の ATD dial-on-demand 拡張)は
 *   カーネルに焼き込むには重すぎるので、常駐する外部コマンド net.bin
 *   (`net &` で起動)がポーリングして中継する。wget/ftp 等の呼び出し側は
 *   ここを直接読み書きする(保護なし、KW_PATHS 等と同じ流儀)。
 *
 *   プロトコル:
 *     1. 呼び出し側が KW_NETHOST に NUL 終端 "host:port" を書き、
 *        KW_NETCMD に NETCMD_CONNECT を立てる。
 *     2. net.bin がそれを拾い "ATD<host>:<port>\n" を port 51 へ送る。
 *        iosim.c 側の connect() はこの `\n` を受けた瞬間に同期的に完了する
 *        (netd1_out 参照)ので、net.bin は送り終えた時点で KW_NETSTATE を
 *        NETSTATE_CONNECTED にしてよい。ただし成否を区別するビットは
 *        iosim.c 側に無いため、実際に繋がったかは呼び出し側がタイムアウト
 *        や応答の有無で判断すること(見た目上は「楽観的に接続済み」扱い)。
 *     3. 以後は RX/TX リングバッファで生バイトをやり取りする(プロトコル
 *        解釈はしない、素通し)。
 *     4. net.bin が(kill 等で)いなくなった場合の復旧は「もう一度
 *        `net &` で起動し直す」だけでよい。net.bin は起動のたびに
 *        KW_NETCMD/KW_NETSTATE/リング全部を初期化する。
 *
 *   同時に扱えるのは client socket #1 と同じく 1 接続のみ。着信(RING/ATA)
 *   や PPP のような上位プロトコルは対象外(意図的なスコープ外)。 */
#define KW_NETCMD       0x8C68            /* 0=none / 1=connect 要求 / 2=close 要求 (1B) */
#define KW_NETSTATE     0x8C69            /* 0=idle / 1=(未使用) / 2=connected / 3=error (1B) */
#define KW_NETHOST      0x8C6A            /* "host:port" NUL終端 (40B) */
#define KW_NETHOST_MAX  40
#define KW_NETRXH       0x8C92            /* rx ring head (1B) */
#define KW_NETRXT       0x8C93            /* rx ring tail (1B) */
#define KW_NETTXH       0x8C94            /* tx ring head (1B) */
#define KW_NETTXT       0x8C95            /* tx ring tail (1B) */
#define KW_NETRXBUF     0x8C96            /* rx ring 本体 (128B) */
#define KW_NETTXBUF     0x8D16            /* tx ring 本体 (128B) */
#define KW_NETBUF_SIZE  128
/* 末尾 0x8D96(exclusive)。予約枠 0x8C68-0x8DFF(408B)に収まる(余白 ~105B)。 */

/* ---- z80board(実機)の中継元は ESP-WROOM-02(2026-09-21) ------------
 *   上のリング/コマンドの取り決めは arch 非依存で、net.bin が話す相手だけが
 *   違う。z80pack は cpmsim の port 50/51(ATD ダイヤル)、z80board は
 *   **SD カードの次の I/O ポート(hw.h ESP_PORT)に bit-bang SPI で繋いだ
 *   ESP-WROOM-02** で、ESP-AT コマンド(AT+CIPSTART / AT+CIPSEND / +IPD)を
 *   話す。実装は user/netesp.c(SPI フレーミングは user/espat.h)。
 *
 *   NETCMD_ATCMD は z80board 専用の拡張。**TX リングに積んだバイト列**を
 *   AT コマンド 1 行として ESP へ送り、応答を RX リングへ素通しする
 *   (AP 参加 AT+CWJAP / IP 確認 AT+CIFSR 用。呼び出し側は user/wifi.c)。
 *   KW_NETHOST(40B)ではなく TX リング(127B)を使うのは、SSID +
 *   パスワードが 40B に収まらないため。state が CONNECTED の間は
 *   受け付けない(RX リングへ "BUSY" を返して終わる)。 */
#define NETCMD_NONE     0
#define NETCMD_CONNECT  1
#define NETCMD_CLOSE    2
#define NETCMD_ATCMD    3               /* z80board のみ(TX リング = AT 行) */
#define NETSTATE_IDLE   0
#define NETSTATE_CONNECTED 2
#define NETSTATE_ERROR  3

/* ---- z80board: FT245 受信リング(#59) ---------------------------------
 *   Z80_INT は FT245 ~RXF と PIC GP5(タイマ、2026-09-19 配線)のダイオード
 *   OR。crt0.s の ISR が SYS_STAT_PORT の bit6(~RXF)で振り分け、~RXF 側
 *   だけここへ受信バイトを積む(tick はカウントしない。GP5 側は isr_timer
 *   が tick++ するだけでここには触らない)。src/io.c(PHYS_RXRDY/PHYS_GETC)
 *   と user/driver.c(CON_RXRDY)が読む側。ISR だけが書き込み(head)、消費側
 *   だけが読み出し(tail)を進めるので単純な SPSC リングとして di/ei なしで
 *   安全(#33 con_ung と同じ発想)。head==tail で空。
 *   満杯チェックは無い(ROM 優先)ので、溢れると一周して古いものを上書きする。
 *   #87: 以前は 64B(0x8D98)で、XMODEM の 1 パケット(133B)が isr に一気に
 *   詰められて一周し、rx が失敗した。256B をページ境界 0x9F00 に置く:
 *   添字が 8bit で自然に一周するので isr の and / add が要らない(ROM が減る)。
 *   0x9F00-0x9FFF は block1(DRIVER、0x9000 からロード)の末尾。DRIVER.BIN の
 *   コード+データは 0xF00 未満であること(python/run_regress.sh のガードが見る)。
 *   (2026-09-19 訂正の経緯: 当初 GP4 を TMR_OUT と誤認していた。実際は
 *   GP4=Z80 HALT→PIC センス入力、GP5=タイマトグル出力。[[z80board-hw-io-map]]) */
#if defined(ARCH_Z80BOARD)
#define KW_RXHEAD       0x8D96            /* ISR が書く現在位置 (1B) */
#define KW_RXTAIL       0x8D97            /* 消費側が読む現在位置 (1B) */
#define KW_RXBUF        0x9F00            /* unsigned char[256]。下位バイトは 0 であること */
#define KW_RXBUF_SIZE   256
#endif
#endif

#define ROUTE_CONSOLE   0                 /* コンソールへ(既定)         */
#define ROUTE_DISCARD   1                 /* /dev/null: 捨てる          */
#define ROUTE_PIPE      2                 /* カーネルパイプへ(writer 側)  */

/*      0x8DFF 以降/0x8E00-0x8FFF  カーネルスタック 512B                */
#define KW_STACK_TOP    0x9000            /* SP 初期値(crt0.s と一致)  */
#define KW_STACK_SIZE   512               /* 運用上のスタック枠         */

/* pid の予約値 (pid 表: 0=free 規約に統一) */
/* ---- .BIN 先頭 32B の予約ヘッダ (#38) --------------------------------------
 *   crt0(crt0cmd.s / crt0_tizix.s)が `.ds 0x20` で空けている領域。従来は
 *   完全な死に領域だったので、そこにロード情報を載せる。
 *
 *   ビルド時にツールが書く(マグが無ければ従来どおり = 追加ブロック無し):
 *     0x10 'T' / 0x11 'Z'   マグマーク。古い .BIN は全部ゼロなので誤検出しない
 *     0x12  xblk            像とは **別に** 欲しいブロック数。上限は置かない
 *                           (必要なだけ要求してよい。物理的に載らなければ kexec が
 *                            エラーを返す。黙って切り詰めることはしない)
 *
 *   ロード時に kexec が書き戻す(プログラムが読む):
 *     0x1C-0x1D imgtop      像が占める領域のサイズ。**追加ブロックの先頭**が
 *                           base + imgtop。作業領域はここから始まる
 *     0x1E      nblk        合計ブロック数(像 + 追加)
 *
 *   なぜ「像に大きな配列を持たない」のか: 像に持つと .BIN が実データの無い
 *   ゼロで太り、ロードが遅くなり、4KB 単位の切り上げで無駄が出る。
 *   SP と argv[] は **像側の上端**に置かれるので、追加ブロックは丸ごと
 *   作業領域として使える(kexec が crt0 へ像のブロック数を渡している)。 */
#define HDR_SIZE        0x20
#define HDR_MAG0        0x10
#define HDR_MAG1        0x11
#define HDR_XBLK        0x12
#define HDR_IMGTOP      0x1C
#define HDR_NBLK        0x1E

#define PID_FREE        0                 /* 空きブロック(pid 表の未使用マーカ) */
#define PID_IDLE        1                 /* block0 = idle/シェル文脈、常時 runnable */
#define PID_DRIVER      0xFE              /* block1 = DRIVER 常駐予約。sched/kexec が
                                           * 明示的に飛ばす。走らない飛び地。0=free と
                                           * 区別し「なぜ block1 に載らないか」を残す。
                                           * crt0.s / kexec.c と一致させること。 */
#define PID_CONT        0xFD              /* 4KB 超プロセスの継続ブロック。先頭ブロックと
                                           * 同一プロセスの一部。sched は走らせず(飛ばす)、
                                           * kexec は使用中扱い、_kexit が先頭と一緒に解放。
                                           * crt0.s / kexec.c と一致させること。 */
#define PID_PIPEBUF     0xFC              /* #27: カーネルパイプの 4KB バッファブロック予約。
                                           * pipe_setup が確保・pipe_teardown が解放。
                                           * sched_pick が飛ばす(走らない)。_kexit は
                                           * PID_CONT でないので触らない。プロセスではない。
                                           * crt0.s(sched_pick)と pipe.c を一致させること。 */
#define PID_BAD         0xFB              /* #64: 起動時メモリチェック(crt0.s)で読み書きが
                                           * 合わなかったブロック。kexec は非 0 = 使用中と
                                           * みなして掴まず、sched_pick は飛ばす。誰も解放
                                           * しない。/bin/free のマップに 'x' で出る。
                                           * crt0.s(z80pack / z80board)と一致させること。 */
/* プロセス枠の物理的な範囲(block2..block7)。block0=kernel/shell、
 * block1=DRIVER 常駐なので、外部プロセスが使えるのはここだけ。
 *   ★1 プロセスが取れるブロック数に **政策上の上限は置かない**(TK 指示)。
 *     「必要なだけ要求でき、連続した空きが無ければそれをもってエラー」。
 *     固定の上限を置くと、載るメモリが増えたときに設計が追従できず
 *     環境の伸縮性が失われる。唯一の上限は **物理的に存在する枠数**。
 *   以前は MAX_PROC_BLK=4 という定数があり、しかも要求超過を **黙って
 *   切り詰めて**いた(下記 xblk)。要求より狭い枠で起動して隣を踏むので、
 *   エラーより悪い挙動だった。 */
#define PROC_BLK_LO     2                 /* 外部プロセスが使える最初のブロック */
#define PROC_BLK_HI     8                 /* 同・最後の次(半開区間) */
#define PROC_BLK_N      (PROC_BLK_HI - PROC_BLK_LO)   /* = 6。物理上限 */

/* 割り込み禁止/許可(short critical section 用)と、待ちループから自分で譲る KYIELD。
 *   CPU の命令そのものなので、gcc 系のポート(PLAT_FLAT32)は arch の include/plat.h が
 *   持つ(ここにアーキごとの枝を足さない)。以下は plat.h を持たない z80 と x86-ia16 のぶん。 */
#if defined(IRQ_OFF)
   /* arch/<arch>/include/plat.h が定義済み */
#elif defined(ARCH_X86_IA16)
#  define IRQ_OFF()  __asm__ volatile ("cli" ::: "memory")
#  define IRQ_ON()   __asm__ volatile ("sti" ::: "memory")
#elif defined(ARCH_Z80BOARD)
   /* #59: 2026-09-19 に PIC GP5(タイマ、Timer0 オーバーフローでトグル。
    * 実測 500us/500us の 1ms 周期)をダイオードで D1(FT245 ~RXF)と
    * 同じ並びに足し、Z80_INT へワイヤード OR 配線した(GP4 は Z80 HALT
    * → PIC のセンス入力で無関係、という当初の訂正は維持)。crt0.s の
    * ISR(0x0038)は SYS_STAT_PORT の bit6(~RXF)で振り分ける:
    * ~RXF なら KW_RXBUF リングへドレインするだけ、それ以外は GP5 起因
    * として tick++ + save/pick/restore(isr_timer)。ei は解禁済み。
    * KYIELD(0x006D, call で入る kyield_entry)は save/pick/restore だけを
    * isr_timer と共有し、**tick は数えない**(#71 で修正。以前は tick++ も
    * 共有していて、getticks を回すだけで時間が進んでいた)。待ちループから
    * の cooperative yield は今までどおり必須(GP5 の周期が粗い/バースト
    * 気味な間の実質的なスケジューリング頻度を補う)。 */
#  define IRQ_OFF()  __asm di __endasm
#  define IRQ_ON()   __asm ei __endasm
#  define KYIELD()   __asm call 0x006D __endasm
#else
#  define IRQ_OFF()  __asm di __endasm
#  define IRQ_ON()   __asm ei __endasm
#endif
#ifndef KYIELD
#  define KYIELD()                        /* タイマで回るアーキでは不要 */
#endif

/* ==================================================================
 * x86-ia16: 絶対番地ワークを実 RAM の配列に載せ替える。
 *   Z80 で 0x8400〜 に固定していたのは SDCC statics が FatFs win[512] と
 *   重なる回避策。x86 は 64KB セグメントを丸々使えるので普通の C 配列で
 *   よい。KW_* の相対オフセット(0x8400 基点)はそのまま保つ。
 * ================================================================== */
#if defined(ARCH_X86_IA16)

extern unsigned char kwork[0x160];        /* 実体は src/kernel.c */
#define KW_BASE        ((unsigned)(void *)kwork)

#undef  KW_PIDTAB
#undef  KW_SPTBL
#undef  KW_CURRENT
#undef  KW_VTREE
#undef  KW_NEXTFD
#undef  KW_OUTROUTE
#undef  KW_EPOCH_SEC
#undef  KW_SUB_TICK
#undef  KW_TICKS
#undef  KW_BLOCKED
#undef  KW_WAKEPEND

#define KW_PIDTAB     (KW_BASE + 0x000)
#define KW_SPTBL      (KW_BASE + 0x008)
#define KW_CURRENT    (KW_BASE + 0x018)
#define KW_VTREE      (KW_BASE + 0x019)
#define KW_NEXTFD     (KW_BASE + 0x119)
#define KW_OUTROUTE   (KW_BASE + 0x11A)
#define KW_EPOCH_SEC  (KW_BASE + 0x122)
#define KW_SUB_TICK   (KW_BASE + 0x126)
#define KW_TICKS      (KW_BASE + 0x127)
#define KW_BLOCKED    (KW_BASE + 0x129)
#define KW_WAKEPEND   (KW_BASE + 0x131)
/* x86 追加: スタックはプロセスのセグメント外(カーネル syscall スタック)にも
 * 出るので、コンテキストスイッチは SP だけでなく SS も退避する。 */
#define KW_SSTBL      (KW_BASE + 0x140)   /* ss_tbl[8] : 退避 SS(2B×8) */

#endif /* ARCH_X86_IA16 */

/* ==================================================================
 * PLAT_FLAT32: gcc の 32bit フラットなポート(m68k-mega / esp32-wroom-32e …)。
 *   x86-ia16 と同じ考え方で、絶対番地ワークを実 RAM の配列 kwork[] に載せる。
 *   どのアーキかは見ない ── arch の include/plat.h が名乗る次のものだけで決まる
 *   (ポートを足しても、ここに節を足さなくてよい):
 *     PLAT_FLAT32        この節を選ぶ
 *     PLAT_NSLOT         スロット数(slot0 = カーネル + init 込み)
 *     PLAT_SLOT_ADDR(n)  スロット n(1..)のメモリの先頭
 *
 *   ★z80 / x86 の数値オフセットは流用できない: int=32bit・ポインタ=4B なので
 *   KW_TICKS(unsigned int)が 2B→4B に太り、struct vnode(vfs.h)も data ポインタが
 *   4B になって 16B→20B に太る。しかも 32bit アクセスには境界の制約がある
 *   (68000 は奇数番地で Address Error、Xtensa は 4 バイト境界でないと例外)。
 *   実際に x86 のオフセットを写しただけの版は、m68k で kernel_init の `TICKS = 0`
 *   が address error になり、起動直後にハングした。
 *
 *   そこで番地は手で書かず、**前の項目の末尾から積み上げる**。32bit の項目の前では
 *   KW_A4 で 4 バイト境界に揃える(68000 にもそのまま通る)。スロット数を変えても
 *   採り直しは要らず、kwork の大きさ(KWORK_SIZE)もここから決まる。
 *   以前は m68k-mega が 63 枠ぶんを手計算した表を持っていて、スロット数を変えると
 *   表どうしが静かに重なる作りだった。
 *   32bit の項目が 4 バイト境界に乗っていることは src/kernel.c がビルド時に確かめる。
 * ================================================================== */
#if defined(PLAT_FLAT32)

extern unsigned char kwork[];             /* 実体は src/kernel.c(大きさは KWORK_SIZE) */
#define KW_BASE        ((unsigned long)(void *)kwork)

#undef  KW_PIDTAB
#undef  KW_SPTBL
#undef  KW_CURRENT
#undef  KW_VTREE
#undef  KW_NEXTFD
#undef  KW_OUTROUTE
#undef  KW_EPOCH_SEC
#undef  KW_SUB_TICK
#undef  KW_TICKS
#undef  KW_BLOCKED
#undef  KW_WAKEPEND
#undef  KW_VTREE_SIZE
#undef  KW_CMDNAME
#undef  KW_CMDARGS

/* スロット数。slot0 = kernel/init(起動時からの呼び出しスタックをそのまま使う)、
 * 1..KW_NSLOT-1 = 外部コマンド。スケジューラの走査範囲(src/kernel.c)も下の表の
 * 大きさも、必ずここから導く(#61: 以前は三者がバラバラで、スロット 8 以降に
 * 作られたプロセスが永久に走らなかった)。値は arch の include/plat.h。 */
#define KW_NSLOT       PLAT_NSLOT

#define KW_A4(x)       (((x) + 3UL) & ~3UL)
#define KW_O_PIDTAB    0UL                                             /* u8[N]  */
#define KW_O_SPTBL     KW_A4(KW_O_PIDTAB + KW_NSLOT)                   /* u32[N]: 退避 SP */
#define KW_O_EPOCH     (KW_O_SPTBL + 4UL * KW_NSLOT)                   /* u32    */
#define KW_O_TICKS     (KW_O_EPOCH + 4UL)                              /* u32(unsigned int) */
#define KW_O_VTREE     (KW_O_TICKS + 4UL)                              /* struct vnode[16] */
#define KW_VTREE_SIZE  (KW_VTREE_N * 20)                               /* 20B/個。src/vfs.c が sizeof を確かめる */
/* #82: カーネルパイプ(src/pipe.c)。struct kpipe は 16B(char 7 個 + 詰め物 +
 * unsigned int 4B x2)。32B 取ってある(超えたら pipe.c の kpipe_fits で止まる)。 */
#define KW_O_PIPE      KW_A4(KW_O_VTREE + KW_VTREE_SIZE)               /* struct kpipe(32B 枠) */
#define KW_O_CURRENT   (KW_O_PIPE + 0x20UL)                            /* u8     */
#define KW_O_NEXTFD    (KW_O_CURRENT + 1UL)                            /* u8     */
#define KW_O_SUBTICK   (KW_O_NEXTFD + 1UL)                             /* u8     */
#define KW_O_CONRAW    (KW_O_SUBTICK + 1UL)                            /* u8。生モードのスロット */
#define KW_O_OUTROUTE  (KW_O_CONRAW + 1UL)                             /* u8[N]  */
#define KW_O_EXITCODE  (KW_O_OUTROUTE + KW_NSLOT)                      /* u8[N]: そのスロットで最後に終わったプロセスの終了コード(#111) */
/* blocked / wakepend の表は持たない: 眠り / 起こすはプロセスの見出しの欄(src/phdr.h、#112) */
/* #78: ps 用のコマンド名/引数表。z80 の KW_CMDNAME/KW_CMDARGS と同じ形
 * (スロット番号 n で引く char[n][LEN])で、置き場所だけ kwork に移す。 */
#define KW_O_CMDNAME   (KW_O_EXITCODE + KW_NSLOT)                      /* char[N][16] */
#define KW_O_CMDARGS   (KW_O_CMDNAME + KW_NSLOT * KW_CMDNAME_LEN)      /* char[N][8]  */
/* z80 と同じくカーネルが cwd を持ち、パスを受け取る入口が kpath() で cwd 起点に
 * 解決する(src/fatcmd.c)。スクラッチはスロットごとに 2 枠(z80 の KW_PATHS と同じ形)。
 * ※ cwd はいずれプロセスの持ち物へ移す予定(TK 2026-09-25)。 */
#define KW_O_CWD       (KW_O_CMDARGS + KW_NSLOT * KW_CMDARGS_LEN)      /* char[48]    */
#define KW_CWD_MAX     48
#define KW_PATH_MAX    48
#define KW_PATH_SLOTS  2
#define KW_PATH_STRIDE (KW_PATH_MAX * KW_PATH_SLOTS)
#define KW_O_PATHS     (KW_O_CWD + KW_CWD_MAX)                         /* char[N][2][48] */
#define KW_O_END       (KW_O_PATHS + KW_NSLOT * KW_PATH_STRIDE)        /* 使用末端 */
#define KWORK_SIZE     KW_A4(KW_O_END)

#define KW_PIDTAB      (KW_BASE + KW_O_PIDTAB)
#define KW_SPTBL       (KW_BASE + KW_O_SPTBL)
#define KW_CURRENT     (KW_BASE + KW_O_CURRENT)
#define KW_VTREE       (KW_BASE + KW_O_VTREE)
#define KW_NEXTFD      (KW_BASE + KW_O_NEXTFD)
#define KW_OUTROUTE    (KW_BASE + KW_O_OUTROUTE)
#define KW_EPOCH_SEC   (KW_BASE + KW_O_EPOCH)
#define KW_SUB_TICK    (KW_BASE + KW_O_SUBTICK)
#define KW_TICKS       (KW_BASE + KW_O_TICKS)
#define KW_CMDNAME     (KW_BASE + KW_O_CMDNAME)
#define KW_CMDARGS     (KW_BASE + KW_O_CMDARGS)
#define KW_EXITCODE    (KW_BASE + KW_O_EXITCODE)
#define KW_PIPE        (KW_BASE + KW_O_PIPE)
#define KW_CONRAW      (KW_BASE + KW_O_CONRAW)
#define KW_CWD         (KW_BASE + KW_O_CWD)
#define KW_PATHS       (KW_BASE + KW_O_PATHS)

/* #82: プロセス枠の番号範囲と番地。z80 はブロック 2..7(4KB)、こちらはスロット
 * 1..KW_NSLOT-1。src/pipe.c(バッファ枠を取る)と src/builtin.c(ps / kill)が見る。 */
#undef  PROC_BLOCK_MIN
#undef  PROC_BLOCK_MAX
#undef  BLOCK_ADDR
#define PROC_BLOCK_MIN  1
#define PROC_BLOCK_MAX  (KW_NSLOT - 1)
#define BLOCK_ADDR(n)   PLAT_SLOT_ADDR(n)
/* 眠りの欄は見出し(src/phdr.h)にあり、像をロードした時点で 0(走れる)になっている */
#undef  PROC_CLEAR_BLOCKED
#define PROC_CLEAR_BLOCKED(n) ((void)(n))

#endif /* PLAT_FLAT32 */

#endif

