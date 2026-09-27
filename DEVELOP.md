## 引き継ぎ: arch/m68k-mega (2026-09-12、セッション切り替え時点)

このセッションでは arch/m68k-mega(旧 arch/m68k、78行のプロトタイプ放置状態
だったもの)を実機に合わせて新規に起こした。次に触るセッションはここから
読めば経緯が分かる。詳細な作業ログは task.md #47、設計意図は memory の
`m68k-mega-port-start` を参照。

### 実機
生の MC68000(4MHz)+ Arduino Mega2560 がバスホスト(/RESET・/HALT・/DTACK・
クロックを駆動、UART を MMIO でエミュレート)。RAM は AS6C4008(512K x8)を
2個(D0-7/D8-15 に振り分け16bit化)で **合計1MB**、ちょうど A20 のデコード
境界(0x000000-0x0FFFFF = SRAM / 0x100000〜 = Mega の MMIO)と一致する。
実機スケッチは tizix リポジトリの外、
`d:\ドキュメント\Arduino\MEGA2560_68000_DTACK_TEST\` にある。
**実機のハード側は今 SIGNAL_TOGGLE_TEST(CPU未実装の導通確認)の段階で
止まっている**(bk/..._stepd_4mhz_stable.ino_bk のほうが進んでいて、実際に
68000を走行させた実績があるので、ハード側を再開するときはそちらを参照)。

### ソフト側(今回やったこと)
実機の完成を待たずに検証するため、rocket68(CPUコアのみの外部ライブラリ、
arch/m68k-mega/rocket68/)を組み込んだ自作システムエミュレータ
`m68ksim`(cpmsimのm68k版、arch/m68k-mega/m68ksim.c)を作った。

- **S1**: crt0.s(GNU as構文で新規に書き直し。旧内容はsdasz80専用構文で
  m68k-elf-gccと非互換だった)、link-kernel.ld、console.c(UART MMIO
  ポーリング)、plat.h(ボードマップ)。
- **S2**: 共有カーネル(src/init.c → kernel_init → sh())を x86-ia16 と同じ
  手口(絶対番地ワークを `unsigned char kwork[]` 配列 + オフセットへ載せ替え)
  で結線。**rocky9 の `m68ksim` 上で実際に tizix シェルが起動する**:
  `[/root]#` プロンプト、pwd/cd/df/ps などの builtin が動作。
- **S4(2026-09-12、同日)で完了: kexec(外部コマンド実行)**。固定 1 スロット
  (PROC_BASE=0x8000、32KB、reloc 無し ── z80のIY/x86のセグメント+偽IRET
  に相当する仕掛けを、m68kでは「実行時再配置ゼロ」にすることで丸ごと不要
  にした)。ラウンドロビン2枠スケジューラ(slot0=kernel/shell、slot1=外部
  コマンド)を crt0.s の irq6_handler(レベル6タイマ)/trap0_handler(TRAP #0
  syscall)に実装、src/kexec.c ARCH_M68K_MEGA 分岐・arch/m68k-mega/sysfile.c
  (syscallディスパッチ、x86のfarcpy相当は flat memory なので不要)・
  user/{crt0cmd.s,cmd.ld,usyscall.s,tzstdio.h}を新規作成。tzport手口
  (共通 user/ls.c をそのままビルド)で ls が動作確認済み: `ls /`・`ls /bin`
  が実ディレクトリ内容を表示、df/tree/exitと共存して正常復帰。
  **踏んだ地雷は下記参照**。

### ビルド・実行方法
```
cd arch/m68k-mega
make clean && make      # kernel.bin / rom.h / m68ksim / disk.img(FAT12) を生成
make run                # m68ksim で対話実行。Ctrl+] で終了(Ctrl+CはゲストのCtrl+Cとして働く)
```
`M68KSIM_TRACE=1 ./m68ksim ...` で命令ごとの pc/レジスタトレースが出る
(ハングの原因調査用)。

### 踏んだ罠(次にKW_*オフセットを別アーキへ移植するときのために)
x86 の KW_* 絶対番地オフセットをそのまま m68k へ流用したら起動直後に無言で
ハングした。原因は **m68k は int=32bit・ポインタ=4B(z80/x86 はどちらも
16bit)** で、KW_TICKS(unsigned int)が2B→4Bに、struct vnode(vfs.h、data
ポインタ持ち)が16B→20Bに太ること。さらに68000は word/long アクセスに
偶数番地を要求し、奇数番地アクセスは Address Error 例外で即死する。
型サイズと整列を型ごとに再計算してから移植すること(src/kmem.h の
ARCH_M68K_MEGA 分岐のコメントに詳細あり)。もう一つ: Makefile がヘッダ依存を
宣言しておらず、kmem.h だけ直しても再ビルドされない事故も踏んだ
(HDRS 変数で全 .h へ一括依存させて対策済み。ヘッダを直したら
`make clean && make` を徹底すること)。

### kexec 実装で踏んだ地雷(2026-09-12、次に触るときのために)
- **`movem.l`で保存するレジスタとargv/pool/スタックの配置は分離すること**:
  最初 argv[]+文字列プールをブロック最上端(プロセスのスタックが伸びてくる
  領域)に置いたところ、FatFsのディレクトリ走査のような呼び出し深度で
  スタックがargvデータを踏み潰し、リターンアドレス破壊で「起動直後に戻る」
  ように見える暴走を起こした(`ls /`で再現、`ls`(空dir)は再現せず ──
  呼び出し深度に依存するため)。argv/poolはイメージ直後の固定位置に置き、
  スタック(PROC_TOPから下へ)とは完全に分離した(src/kexec.c参照)。
- **★本当の原因はもっと基本的だった: callee-saved レジスタの保存漏れ**。
  user/usyscall.sのsyscall5が引数a2を%d2にロードしていたが、m68k SysV ABI
  ではD2-D7/A2-A6はcallee-saved ── 呼び出し元(GCCコンパイル済みのC、例えば
  ls.cのmain)がD2にレジスタ割付けしたローカル変数(lflag)を、syscall5を
  呼ぶたびに無断で上書きしていた。readdir()はa2=dh(ディレクトリハンドル、
  非ゼロ)を渡すため、readdir()を1回呼んだだけでlflagが意図せず真になり、
  無関係なreaddir_size()分岐に迷い込んで暴走していた(上のスタック説離と
  合わせて2つの独立したバグが重なっていた)。**手書きアセンブラでC呼び出し
  規約をまたぐときはcallee-savedレジスタの保存/復帰を必ず確認すること**
  ── z80のIYレジスタ規約と違い、m68k(や他の標準ABI)はこの区別がある。
  診断にはM68KSIM_TRACEでは行数が多すぎたため、m68ksim.cに一時的な
  「リングバッファ+リセットベクタ再訪検知」のフライトレコーダを足して
  原因箇所を特定した(修正後は削除済み)。
- 固定1スロット(PROC_BASE=0x8000、32KB、reloc無し)のみ対応。複数プロセス
  同時実行やPIC化(合意済みの「32KB×32枠」計画)は次段。TRAP命令は
  autovector割込みと違いSRの割込みマスクを自動で上げないため、
  trap0_handlerの間だけ明示的にマスクしている(念のための多重防御、
  上記2バグの直接の原因ではなかったが残してある)。

### 未着手(次の課題)
- 外部コマンドの複数同時実行・PIC化(32KB×32枠計画、[[m68k-mega-port-start]]参照)。
- cat/echo等、ls以外のcoreutilsをtzport手口で追加ビルド(Makefile TZPORT_CMDSに足すだけの想定)。
- 実機ハード側のDTACK込みバスサイクル復帰(SIGNAL_TOGGLE_TESTから)。
- ~~SD-over-Megaのディスクプロトコル設計~~ **2026-09-12 設計・実装済み**:
  plat.hのSD_LBA_HI/MID/LO・SD_CMD・SD_STATUS・SD_DATAへ、UARTと同じ
  1バイトポーリング方式(DATA_RDYビット)で置き換えた。旧DISK_SIM_*は
  cpu->memoryへ直接fread/fwriteする方式で実機Megaでは動かなかった
  (Megaは/DTACKしか駆動せずCPUのSRAMを直接書けないため)が、新方式は
  実機・m68ksim共通のdiskio.cで動く設計。arch/m68k-mega/diskio.c・
  m68ksim.c・include/plat.hを変更、m68ksim上でdf/treeがFAT複数セクタ
  読み込み(ブートセクタ+FAT+ディレクトリ)を通して動作確認済み。
  write経路は読み込みと対称に実装したが、kexec未実装でディスクに書く
  builtinが無いため未検証(次にkexecが入ったら要確認)。
  実機Mega側の.inoファーム(tizixリポジトリ外)はこのレジスタ規約に
  合わせてまだ未実装 ── SIGNAL_TOGGLE_TESTから先に進んだら次の課題。

---

・1個の課題を出したら、その範囲だけ対応し、そこ以外の検討や調査はやってはならない
　どうしても必要ならば調査してよいか質問すること


・プリエンティブマルチタスク UNIX風OSを実装している

・iyレジスタをベースアドレスとして各プロセスに入れてやり、ジャンプ時などのベースアドレスにする
　これは鉄の掟で一切くつがえす余地はない


・gitにファイルの履歴はのこっているので、.gitさえ改変しないように中止したら
　何をやってもよい許可をする

・動作確認はSSHを許可するので、かならずz80pack ./cpmsimで動作を確認すること
ssh -i <秘密鍵> <ユーザー>@<ビルドホスト>

~/z80pack/tizix
と
\\rocky9\tk\z80pack\tizix
は同じ領域を参照sいている


　★[対応済] make 直後 cpmsim 起動 vs driveb.dsk write-back 競合(偽 FAILED/reboot):
　mkfatdisk.sh 末尾 sync + python/tzpaths.py wait_disk_ready() でポーリング。手動 sleep 不要。

　★シェルのプロンプトは "[<cwd>]# "(2026-08-31)。sh() が毎回 cwd から組む。
　　テストの read_until は "]# " / boot は "…LOADED\r\n[/root]# " をアンカーにする。

　★ファイル名は小文字統一(2026-08-31)。/bin のコマンドも tmp.pip も小文字。
　　仕組み: mtools は all-lowercase 8.3 名を「SFN + NT 小文字フラグ(DIR_NTres
　　bit3/4)」で書く(LFN 無し)。src/ff.c の get_fileinfo(非LFN)に、そのフラグを
　　見て A-Z を小文字化する 5 行を追加。FatFs は元々 case-insensitive なので
　　`cat /BIN/HELLO.BIN` でも開ける(表示だけ小文字)。ユーザーが大文字で作った
　　ファイルは大文字のまま(case-preserving)。
　　注意: get_fileinfo 非LFN 分岐に BYTE ローカルを足すと SDCC がその巨大関数の
　　codegen を壊した(ls 空 / cat クラッシュ)。dp->dir[DIR_NTres] を直参照する形に
　　したら直った。ローカルを増やさないこと。

　★FS レイアウト(2026-08-31 UNIX 風に整理):
　　/bin   … 全コマンド + DRIVER.BIN。sh の launch は "/bin/<CMD>.BIN" を開く。
　　　　　　 kload_driver も "/bin/DRIVER.BIN"。先頭 '/' 付き実行はそのまま使う。
　　/root  … シェルの初期カレント(ホーム)。無引数 cd もここへ戻る。
　　/dev   … 合成(vfs.c。ディスク上に無い)。
　　driveb.dsk は **一度作ったら mkfs しない**。make は /bin のコマンドを
　　mcopy -o で上書きするだけ(FS 永続)。丸ごと作り直しは `make cleandisk`。
　　mkfatdisk.sh が「存在すれば sync のみ / 無ければ mkfs+/bin+/root」を自己分岐。




・作業報告の際にはテスト結果とエビデンスを提示すること
　エビデンスなき修正は未完了とみなす


==== アーキテクチャ分離と識別マクロ ====

  共通ソースは src/ 、プラットフォーム依存は arch/<arch>/ に分ける。
  ビルドは各 arch がプリプロセッサ識別マクロを 1 つ立てる:

     arch/z80pack/   -DARCH_Z80PACK    (cpmsim。make          / make ARCH=z80pack)
     arch/z80board/  -DARCH_Z80BOARD   (実機 Z80。make ARCH=z80board。
                                        2026-09-12 キャッチアップ改修で
                                        crt0/console/diskio/spi+sdcard を実装。
                                        SD カード周りは実機未検証)
     arch/x86-ia16/  -DARCH_X86_IA16   (8086 リアルモード/PC-AT/QEMU。
                                        当面 make -C arch/x86-ia16)

  ・z80pack / z80board は arch/<arch>/arch.mk の PLAT_DEF で C コンパイラへ渡る
    (sdasz80 は -D 非対応なので asm には渡さない)。x86-ia16 は
    arch/x86-ia16/Makefile の CFLAGS に直接。
  ・src/ の arch 差分は「当面プリプロセッサで回避」する方針:
       #if defined(ARCH_X86_IA16)
         ... x86 版 ...
       #else   /* Z80 (sdcc) */
         ... 従来 ...
       #endif
    ファイル分割(io.c の物理層を arch/<arch>/console.c へ 等)は後回し。
  ・x86-ia16 では iy レジスタ規約は無効。CS=DS=SS=ES をプロセスごとに設定する
    セグメントが「IY=base」の代替になる(iy_reg 後処理は不要)。

  ・**x86-ia16 の位置づけ**: OS 本体の実験というより「ユーザーコマンドを速く
    実装するための手段」。iy_reg の脆さも「掟」(unsigned 限定 / 除算禁止等)も
    無く、普通の C + ia16-elf-gcc + ia16-elf-gdb で書ける。ここでコマンドを
    書き起こし、必要なら Z80 側へ規約を掛けて移す。
  ・x86-ia16 プロセス機構(実装済み・QEMU 検証済み):
      slot n (1..7) ↔ セグメント (n+1)*0x1000。slot0=kernel/idle。
      PIT 100Hz → INT 08h round-robin(crt0.s _isr08)。
      syscall = INT 80h: AH=0 exit / 1 putchar(AL) / 2 getchar / 3 getticks。
      .BIN は pseg:0x0100 起点フラットバイナリ(.COM 流儀)。
      kexec_file が farcpy でセグメントへ載せ、偽コンテキストで進入。
  ・既知の穴(後回し): src/builtin.c cmd_kill の範囲チェックが Z80 前提
    (block 2..7)。x86 は slot 1 も正規プロセス枠なので `kill 1` が弾かれる。
    #if で 1..7 に直すこと。ps 自体は正常。


 
 ==== 2026-09-01 修正履歴 ====
   ・arch/x86-ia16: カーネルパイプ(pipe_*)のスタブを pipestub.c として追加しリンクエラーを解消。
   ・arch/x86-ia16/Makefile: ブートセクタの dd 書き込みサイズを修正しシグネチャを保護。
   ・arch/x86-ia16/stage1.s: デバッグ用 '!' 出力コードを追加。
   ・VFS 認識問題: Z80 側 VFS 改修に伴い、x86 側の sysfile.c / vfs_resolve の連携再検証が必要。


==== メモリ配置(移植先: ROM 32KB / RAM 32KB ベアメタルを見据えた配置) ====

  ROM 0x0000-0x7FFF  カーネルコード(CODE_LOC=0x0120)。低位に固定ベクタ表。
  RAM 0x8000-0xFFFF  4KB × 8 ブロック
     block0 0x8000  カーネル RAM(_DATA / PCB / VFS / スタック)
     block1 0x9000  DRIVER 常駐(0x9000 固定リンク。メモリイメージを直接転送)
     block2-7       プロセス枠(動的割付。ロード先 base が IY に入る)

  固定ベクタ(ITV 直後にシステムコールを配置。crt0.s が起動時に書き込む)
     0x0038 jp isr        タイマ ISR(IM1)
     0x003B _kexit        0x003E _kputchar   0x0041 _kgetchar
     0x0044 _getticks     0x004A _time_get   0x004D _time_set
     0x0050 jp (hl)       ___sdcc_call_hl (SDCC 関数ポインタ呼び出しヘルパ)
     0x0051 ret
     0x0052-0x006C  libivt(__divuint 等 / memcmp / strcmp)を 3B 刻みで公開

  カーネル絶対番地ワーク(kmem.h)
     0x8400 pid_tbl  0x8408 sp_tbl  0x8418 current
     0x8419 VFS ノード  0x8519 next fd  0x851A out_route
     0x8522 epoch_sec(32bit)  0x8526 sub_tick  0x8527 ticks(16bit)
     0x8600 fd_table(driver常駐 6×37B)  0x86F0 fd_inited

  pid_tbl の予約値(kmem.h / crt0.s / kexec.c を一致させること)
     0    = free      1 = idle(block0)
     0xFE = DRIVER(block1 予約)
     0xFD = CONT      4KB 超プロセスの継続ブロック(下記マルチブロック参照)


==== マルチブロックプロセス (実装・検証済み) ====

イメージが 1 ブロック(4KB)を超えるコマンドは、空いている連続ブロックを必要数だけ
1 プロセスへ割り当てる。IY = 先頭ブロック base のまま(iy_reg 無改造で通る:
IY+offset はプロセス空間 <=0x3000 内で 64K 桁上がりしない)。PCB / コンテキスト /
PID は 1 個、スケジューラ・プリエンプションは無改造。

  kexec.c kexec_file:
    ・f_size() から必要ブロック数 nblk = ceil(size/4KB)。
      比較ラダー(>0x1000UL / >0x2000UL / >0x3000UL)で求める(除算/シフト helper 回避)。
      上限 MAX_PROC_BLK=3(kmem.h)。超過はエラー。
    ・block2..7 を走査し pid_tbl==0 が nblk 個連続する最初の run の先頭を n に
      (番地は固定でない)。連続空きが無ければ「空きメモリ無し」でエラー。
    ・f_read(base, nblk*0x1000)。引数文字列 = base + nblk*0x1000 - 256。
    ・偽コンテキストの BC スロットに nblk を積む。SP = 最終ブロック上端 - 14。
    ・pid_tbl[n]=n(実 PID)、pid_tbl[n+1..]=PID_CONT(継続ブロック)。
  crt0.s:
    ・sched_pick は pid==PID_CONT を runnable から除外(飛ばす)。
    ・_kexit は先頭ブロック解放後、後続の PID_CONT を連続クリア(全枠返却)。
  user/crt0cmd.s _start:
    ・BC(=nblk)から top = base + nblk*0x1000 を計算し、SP を top-0x110、
      argv 文字列域を top-0x100 に張り直す。
    ・nblk==1 なら SP=base+0x0EF0 / argv=base+0x0F00 で従来と完全一致(回帰ゼロ)。

  制約: compaction 不可(絶対番地確定・reloc 情報なし)→ 断片化はプロセス終了まで
  残る。同時プロセス数は 6 スロット固定、2 ブロックプロセス混在で実質 3-4。


==== IY 相対 PIC 変換 (user/iy_reg_claude.py) ====

sdcc の .asm 出力(テキスト)を入力に取り、0x0000 基準でリンクされたコードを
「実行時に IY(=ロード先 base)を足して実アドレスを作る」形へ変換する。
ロード後にバイナリを書き換えるロード時パッチではない(ビルド時に完結する)。

  ビルド順: sdcc -S → iy_reg_claude.py → iy_jrfix.py(sdasz80 を内包)→ sdldz80

変換対象:
  ・ld {hl|de|bc}, #label    → ロード直後に IY 加算を注入
  ・jp label / jp cc,label   → IY 相対の間接ジャンプに置換
  ・call label               → ___sdcc_call_hl 経由の IY 相対間接コール
触らないもの:
  ・jr label / jr cc,label / djnz  ★相対分岐は base 非依存。グルー 0 バイト
  ・jp (hl)/(ix)/(iy)
  ・外部固定シンボル(_drv_tbl, ___sdcc_call_hl, 各システムコールベクタ)
  ・数値リテラル(構文でシンボルと厳密に区別する)

★既知の穴: `ld a,(直値ラベル)`(定数添字のグローバル配列アクセス等)は変換対象外。
  +IY されずゼロページを読む。ランタイム添字なら `ld hl,#label`(+IY される)経由に
  なり正。回避: グローバル配列はポインタ経由で触る。

★jr を触らないことが効く理由 (2026-09-08、それまでの方針からの変更):
  jr は相対なのでロード先 base に依存せず、変換しなければコストがゼロになる。
  一時期 iy_reg が jr も間接化しており(1 個 14-16B)、その誤変換を避けるために
  Makefile が --no-peep を付けて「そもそも jr を出させない」運用になっていた。
  結果 sh.c の分岐は jp 405 / jr 0 になり、405 個すべてにグルーを払っていた。
  peephole を戻すと jp 35 / jr 198、iy_reg の変換数は 492 → 122。
    sh.bin 11589B(3 ブロック)→ 6507B(2 ブロック)
    rx 4758→3032 / date_dbg 1895→1516 / ptx 466→249 など他コマンドも縮む
  挿入を跨いだ jr が ±127 を超えることはあるが、sdasz80 が
  「Branching Range Exceeded」で報告し .rel を消す = 黙って壊れない。
  user/iy_jrfix.py がその行だけを間接化して再アセンブルする(sh で 198 個中
  7 個・1 反復で収束)。全部を先回りで間接化しないのが要点。
  ※ gcse/loop/label 最適化は引き続き無効(入れても jp 35 / jr 192 で改善が無く、
    変数を増やす意味が無い)。driver.c は iy_reg を通さないので --no-peep 据置。

★検証パス (2026-09-08 追加): 変換後に「未変換の再配置参照」が 1 個でも残って
  いたら iy_reg_claude.py がビルドを止める。これが無いと拾い損ねた絶対参照は
  0 基準のまま実行され、ゼロページへの wild jump になる(実行時にしか分からず
  フレークする ── 「フレーク wild jump」の節を参照)。ビルド時に落とす。

★sh が 2 ブロックになった効果: プロセス枠は block2..7 の 6 個。従来は
  sh(3) + パイプ(writer 1 + reader 1 + buf 1) = 6 でちょうど埋まり、背景ジョブが
  1 個でもあるとパイプが out of memory だった。sh(2) なら `a &` を走らせたまま
  `ptx 30 | wc` が通る(実機確認済)。

間接ジャンプの実装(HL/DE/AF・全フラグを保存し、スタック深さも不変):
    push hl / push af / push de
    ld   hl, #target
    push iy / pop de
    add  hl, de        ; HL = 実アドレス
    pop  de / pop af
    ex   (sp), hl      ; スタックトップの旧HLと実アドレスを交換(HL復元)
    ret               ; 実アドレスへジャンプ

条件付きジャンプは「条件を反転して jr でスキップ」する。
  ★スキップ分岐は必ず jr(相対)にすること。jp(絶対)にすると base≠0 で暴走する。
    (po/pe/p/m は jr 非対応。下記「int 禁止」参照)

補足: ネイティブ IY 相対 PIC を吐く自作コンパイラ tzcc(tizix/tzcc/ に同居)で
coreutils は iy_reg を卒業済み(#26)。iy_reg は sh + 開発スクラッチ用に残るのみで、
大改造(reloc 表化等)への投資はしない。


==== ユーザーコマンドを書く際の掟 ====

1. 符号付き int を使わない(unsigned で統一)
   符号比較は SDCC が overflow 補正の jp PO / jp M を吐く。これらは jr 非対応の
   ためスキップ分岐が絶対 jp になり base≠0 で暴走する。大小比較は unsigned 一発
   比較(→ jr c/nc)へ落とす。

2. 除算・乗算(/ % *)を素で使わない
   標準ライブラリを引けない。libivt(0x0052〜の固定ベクタ)経由か、減算ループ /
   桁重みテーブルで代用。

3. カーネルのベクタ経由で呼ぶ関数は __sdcccall(0) を付ける
   カーネル本体は sdcccall(1)、ユーザーコマンド/ドライバは --sdcccall 0。
   公開関数の宣言(kernel.h)と定義(kernel.c)の両方に __sdcccall(0) を付ける。

4. カーネル/ドライバの共有ワークは C グローバルにせず絶対番地に置く
   C グローバルは _DATA 先頭に配置され FatFs の共有セクタバッファ win[512] と
   重なって破壊される。kmem.h の空き帯へ絶対番地固定で逃がす。
   ※ _DATA 域自体は crt0.s が起動時にゼロクリアする(2026-08-30 追加。抜けていて
     FatFs[] 等がゴミポインタで立ち上がり wild jump していた。上の changelog 参照)。


==== コマンド側 libc (手書き PIC アセンブラ) ====

string.h / stdlib.h の純粋関数は driver 常駐から分離し、コマンド側 .rel として
リンクする(iy_reg のグルーが実測 +94% でコードをほぼ倍にするため)。

  string.h → string.rel(user/lstr.s)   stdlib.h → stdlib.rel(user/lstd.s)
  ・Makefile 明示ルール `string.rel: lstr.s` で sdasz80 直。iy_reg を通さない。
  ・位置独立の規約: jr/djnz のみ・jp/call ラベル禁止・ld hl,#code-label 禁止。
    静的 _DATA(_rand_seed)を触る所だけ push iy/pop de/add hl,de で IY 明示加算。
    IY 不可侵、IX は使うなら push/pop。--sdcccall 0 ABI(引数 2(sp)〜、戻り値 HL、
    long は DE:HL、呼び出し側が引数掃除)。
  ・user/string.c / user/stdlib.c は参照用に残置(未使用)。
  driver.c からは string/stdlib 本体を削除し、l__CODE は 0x142A → 0xC46(4KB 内)。
  drv_tbl(user/drvvec.s / stdio.h):
     [0..18]  stdio(putc/getc/printf/fopen/... /puts)。DRIVER 内に実体。
     [19..20] proc_block / proc_wake(5a)。カーネル本体を直接指す。
     [21..23] opendir / readdir / closedir(kdir_*、Step 6)。同上。
     [24..26] mkdir / unlink / rename(kfs_*、Step 9)。同上。
     19.. のアドレスは user/Makefile FS_SYMS が kernel.map から -g で解決
     (DRIVER にはコード無し = ベクタだけ)。


==== コマンド 実装状況と残タスク ====

  凡例: [z80] z80pack で検証済 / [x86] x86-ia16 で検証済 / [--] 未実装

  ※ コマンドの実体は全部 /bin/<NAME>.BIN(2026-08-31 レイアウト整理)。
    シェル初期カレントは /root。詳細は冒頭「★FS レイアウト」。

  builtin(src/builtin.c。両 arch 共通の builtins[] 表):
     df ps kill uptime tree exit
     ls / cat / echo / mkdir / rm / mv は表から削除し外部化(Step 6-9)。
     共通表なので x86 も同時に builtin を失う(ls は Step 7 時点で既にそう)。
     x86 側の外部コマンド化・パリティは Step 14。
     [z80] pwd / cd  #28(2026-09-08)で cwd は **カーネル所有** になった。sh は
           kchdir(drv_tbl[42])/ kgetcwd([43])を叩くだけで、引数を一切解釈しない。
           相対パスの解決はパスを受け取るカーネル入口が kpath() で行う。
           詳細は「==== カレントディレクトリはカーネルが持つ ====」。
           x86 は builtin シェル(src/sh.c)が自前 cwd を持つ旧方式のまま(横展開が残)。
           ※ それ以前は sh が「どの引数がパスか」を推測して絶対化しており、
             is_path_cmd の 16 個ハードコード / "-n" の次は数値 / '=' なら
             key=value / grep の第 1 引数は PATTERN … という積み重ねになっていた。
             `wc -l` が "cannot open -l" になる原因であり、コマンドにオプションを
             足すたび sh が壊れる原因でもあった。この節ごと不要になった。

  外部コマンド:
     hello        [z80][x86]
     ls           [z80]        opendir/readdir、/ に dev 合成(Step 6-7)
     cat          [z80]        cat FILE=fopen/fread、cat/< FILE=getchar(Step 8)
     echo         [z80]        argv[0] + 改行(puts 一発。Step 8)
     mkdir/rm/mv  [z80]        drv_tbl[24..26] kfs_mkdir/unlink/rename(Step 9)
     cp           [z80]        512B まで。dst がディレクトリなら DIR/basename(Step 10)
     touch        [z80]        空ファイル作成 / 既存は非破壊(Step 10)
     a / b        [z80][x86]   プリエンプティブ多重実行の実証用
     wc / cp / xxd [x86]       C。file syscall(open/read/write/close)を使う
     date         [z80]        x86 未(time syscall 拡張が要る)
     rx           [z80]        XMODEM 受信。x86 未
     test1 / t2 / date_dbg / cp_dbg [z80]  検証用・一時

  --- x86 の基盤(実装済み)---
     ・INT 80h syscall: AH=1 putchar / 2 getchar / 3 getticks /
       4 open(bit0=0 読み,1 書き新規) / 5 close / 6 read / 7 write /
       8 opendir / 9 readdir / 10 closedir / 11 unlink。
       sysfile.c の sys_call() が C ディスパッチャ。ポインタ引数は farcpy で
       呼び出し元セグメントとの間を移送。ulibc.c にラッパ、usyscall.s に
       syscall5 スタブ。disk_write は INT 13h AH=03。
     ・コンテキストスイッチは SS:SP 両方退避(_isr08 / kexec.c / KW_SSTBL)。
       _isr80 は専用カーネル syscall スタックへ切替えてから C を呼ぶ。

  --- GNU coreutils 比で「無いと厳しい」未実装(優先度順。中断ポイント)---
     1. [z80 済] pwd / cd   シェル側 cwd + sh 集中解決で復活。x86 へ横展開が残(次点)
     2. [z80 済] ls / cat / echo / mkdir / rm / mv / cp / touch を C 外部コマンド化(Step 6-10)
     3. head / tail         ファイル先頭/末尾 N 行(open/read で書ける)
     4. more / less         cat が長いと流れる。1 画面ページャ(端末制御が要る)
     5. grep(固定文字列)   ログ・設定検索
     6. rmdir / cmp / clear / sync / sleep / free / reboot

  --- 既知の不具合 → #29(2026-09-08)で解消済み ---
     4 件のうち 3 件は本物の不具合で、2 つの独立した原因だった。残り 1 件は
     再現せず(#28 のカーネル cwd 化で消えていた)。
     ・uniq FILE がゴミを出す = **スタック枯渇**。1 ブロックプロセスの実効
       スタックは 3776 - バイナリサイズしかなく、uniq.bin=3640B で残り 136B
       (全コマンド中 最小)。fgetc から FatFs へ降りるとスタックが _DATA 末尾の
       line[]/prev[] を踏み潰していた。`cat f | uniq` が無事なのは getchar 経路が
       浅いから。→ LINE_LEN 128→64 で 264B 確保(同じ read_line を使う grep は
       273B で正常)。**PIC グルー削減が入れば 128 に戻せる。**
     ・du FILE / tee = **tzcc のパーサが条件式中の代入を黙って誤コンパイル**。
       `while ((c = getchar()) != EOF)` は parse_expr が代入を式として扱えず、
       条件が裸の `c` になり、代入はループ外へ追い出され、比較は捨てられ、
       本体はループの後ろへ落ちる。エラーにならないのが最悪だった。
       → 両コマンドを for(;;) + 明示 break へ書換え。あわせて **tzcc 側に
       検出を入れ、この形はビルドエラーにした**(main.c reject_assign_in_cond。
       if / while / for の全条件。式としての代入実装までの安全網)。
       ついでに du FILE を実装(opendir 失敗ならファイルとして fseek(END)+ftell)。
     ・`cat f | grep pat` の末尾空行 2 つ = **再現せず**。#27 と同じ m.txt(l1..l5)
       ほか、先頭一致 / 末尾一致 / 不一致 / 20 行 / `ls | grep` / `ps | grep` /
       `grep pat FILE` のいずれも余分な改行なし。#28 で sh の skip_first
       (grep の第 1 引数を PATTERN とみなす特別扱い)が消えた際に解消したとみられる。

  --- 未処理の不具合(2026-09-08 時点で残っている唯一の実害)---
     ・**パイプとリダイレクトを併用できない。** `cat f | grep pat > o.txt` は
       `>` が grep の引数として渡り "grep: cannot open >" になる。3 段パイプ
       (`a | b | c`)も同様。sh のコマンドライン分解が 2 段パイプまでで、
       パイプ行のリダイレクト記号を見ていない。
       **エラーを出さずに変な動きをする**ので、仕様というより不具合寄り。
       最低でも「未対応」と言って落とすべき。次に手を付けるならここ。

  --- 仕様上の制限(不具合ではない)---
     ・`ls` は FAT 8.3 の短縮名(大文字)を出す。`ls | grep txt` は何も出ない
       ── `ls | grep TXT` が正しい。
     ・オプション解釈が無いコマンドがある。`wc -l` は `-l` をファイル名として
       扱い "cannot open -l" と言う ── wc にオプション解釈を実装していないだけ。
       coreutils が実際に要るのは -l -h -n N -c -i -v -a 程度なので、最小の
       optbits()(フラグをビットマスクで返す)を共有 lib に 1 本置くのが安い。
       POSIX getopt(user/getopt.c)は tzcc ビルドで +約 2015B と重く不採用。
       sh 側は #28 以降そのままトークンを渡すので、コマンドを直すだけで済む。
     ・パイプの総量が 4KB を超えると `sh: out of memory` で打ち切る(#27)。
       streaming / 動的拡張は malloc 実装後。
     ・ls は path がちょうど "/" のときだけ /dev を合成表示するので、無引数 ls に
       限って sh が cwd を 1 トークン足している(ls_default_cwd)。本来 kdir_read が
       "/" の反復で dev を返すべきで、そうすればこの特別扱いは消える。

  ==== tzcc のコストモデル(user/*.c を書くとき見る表)====

  #29-#31 で実測した「C にこう書くと何バイト出るか」。**#31 で tzcc に IX ベース
  方式を入れたので単価が下がっている**(下記)。それでも tzcc は値をレジスタに
  保持しないので、**変数名がソースに 1 回出るたびにコードが出る**という性質は
  変わらない。user/ 以下のコマンドはここを意識して書くこと。

     C で書くこと                        #30 まで   #31 以降
     ---------------------------------   --------   --------
     変数の読み (int / ポインタ)          15B        6B    ld l,d(ix) / ld h,d+1(ix)
     変数の書き (int / ポインタ)          14B        6B    ld d(ix),l / ld d+1(ix),h
     変数の読み書き (char)                12B        3B    ld a,d(ix) / ld d(ix),a
     配列名・文字列リテラル(アドレス取得) 11B        11B   (据置。IY 加算のまま)
     関数呼び出し 1 回                    13B        13B + IX 張り直し 11B
     関数の仮引数 1 個                    約 20B     約 12B
     if / ループの分岐                    2B         2B    (#30 の jr 化)

  #31 の IX ベース方式(tzcc tizix.c):
    ・**IX = IY + tzc_vb + 128** を張り、スカラ変数を (ix+d) で触る。±127 の窓に
      収めるため _DATA を並べ替えてスカラ(.dw/.db 単発)を先頭へ集めてある。
      配列(.ds)・文字列(.ascii)・long(4B)はスカラ扱いせず従来のグルーで触る。
    ・IX は 関数入口と call 直後にだけ張り直す(呼び先が IX をフレームポインタと
      して潰すため)。ラベルでは無効化しない。遅延方式(触る直前に張る)も試したが
      ラベルが密で逆に損、かつ `call f / jr L` の経路で正しくない。
    ・関数プロローグの `ld ix,#0 / add ix,sp` は撤去し、仮引数は SP 相対で読む。

  **tzcc は値をレジスタに保持しない**(全ローカルが静的な `var_*`)。したがって
  生成コードのアクセス数は **C ソースに変数名が出てくる回数と 1:1**。
  → **効くのは「変数名の出現回数を減らす」「仮引数を減らす」「関数呼び出しを
    減らす」「printf を使わない」。**

  ★★ 掟: **関数をまたいで同じローカル名を使わない。** tzcc はスコープを持たず、
    同名は同一記憶域を共有するので、A が B を呼ぶと A の値が黙って壊れる。
    #31 で user/date.c が実際にこれを踏み `1970-01-00` を出した(p2() のローカル
    `d` が main の `d` と同一だった)。**tzcc 側に検出を入れてあり、
    --tizix-user では ビルドエラーになる**(generator.c check_dup_locals)。
    仮引数も同じ記憶域なので、呼び出し側と同名の仮引数も引っかかる。その場合は
    リネームより「仮引数をやめてファイルスコープ変数を直接触る」方が
    コストモデル的にも正しい。

  ★ メッセージ出力は printf を使わない。printf(tzcprintf)は 546B の 1 モジュールで、
    %s と 10 進数しか要らないなら libtzc の **prs / prnum / prnuml**(tzcout、
    3 本で 200B 弱・共有)で組む。#31 で coreutils 14 本から printf を外した。

  ★ 文字列関数は用途別に分かれている(tzcstr / tzcstrn / tzcstrstr / tzcmem)。
    sdld は .rel 単位で引くので、strstr 1 個のために string 一式 625B を
    払わないよう #31 で分割した。

  --- #31 の実績(coreutils 22 本の合計) ---
     43689B (#30 時点)
     -> 38505B  tzcc IX ベース方式        (-11.9%)
     -> 37698B  cp を逐次コピーへ(512B 上限撤廃)
     -> 33301B  printf を 14 本から排除    (prs/prnum/prnuml)
     -> 31833B  libtzc の string 分割 + date の 32bit/桁出力の圧縮
     -> 31604B  仮引数の削減(grep/uniq/ls/tail/wc)
     **合計 -12085B (-27.7%)。cp が 2 ブロック -> 1 ブロックになり、
       全 22 本が 1 ブロックに収まった。最小スタック余裕は cp の 1108B。**

  --- 未着手の基盤拡張 ---
     ・[#31 で実施済] user/*.c のコストモデル見直し + tzcc の PIC グルー削減。
       保留していた 4 案のうち **(d) IX 相対アドレッシングを採用**した(上記)。
       残る (b) ベースを BC へ常駐 / (c) 固定ベクタ化 は **不要**。(d) で
       全 22 本が 1 ブロックに収まり、最小スタック余裕も 1108B あるため、
       今やっても機能的に変わるものが無い。数値と却下理由:
         (b) BC 常駐 → **BC を永久に失う**(LDIR/CPIR が使えず将来のレジスタ
             割り付けを 2 ペアに縛る)。
         (c) 固定ベクタ化 → カーネル変更が要る。RST 版なら更に縮むが
             **0x0000-0x0037 はハードウェア専用**なので不可。
       ★ 裏レジスタは使えない(検討済み・結論)。add hl,rr は BC/DE/HL/SP しか
         取らず、再配置したいオフセットは常に HL にある。exx は HL ごと入れ替える
         のでベースを同じバンクへ渡せず、`add hl,iy` という命令も無い。裏表を
         跨ぐ 16bit の経路はメモリ / IX / IY / SP だけ。ISR も既にプロセス
         ブロックへフルコンテキスト退避する方式なので裏を ISR 専用に召し上げ
         られない(むしろ tzcc が裏を使い始めると切替コストが 4 ペア分増えて損)。
     ・**アクセス回数を減らすのはレジスタ割り付け = コンパイラの本工事。**
       (d) を入れても「変数名の出現回数 = アクセス回数」は変わらない(単価が
       下がっただけ)。wc の 82 箇所が 20 箇所前後になる見込みだが規模が別次元。
     ・switch のディスパッチ。tzcc は式を 1 回評価して `cp #K` / `jr z` を並べる
       ので、**分岐の単価が if/else 連鎖の約 1/8**(実測: 同一 7 分岐で
       if 連鎖 994B / switch 742B = 1 分岐あたり -36B)。ただし case 本体が
       dispatch の後ろに並ぶため 2 個目以降の飛び先が ±127 を超え、jrfix が
       間接列へ戻して 1 case 18B 払っている(理想は 4B)。dispatch を case 本体の
       直前へ置くか (値, 相対オフセット) のテーブルを共有ルーチンで舐める形に
       すれば更に 1 case あたり ~14B 減る。**未着手。**
       ★掟: dispatch は `ld a,l` で **下位 8bit しか比較しない**。`switch (c)` に
         `case EOF:`(= -1)と書くと 0xFF と比較され c==255 が誤ヒットする。
         switch は 0..255 の値にだけ使い、EOF はループ側で先に弾くこと。
     ・機能を分割してフロントエンドから fork する手(cp → cp-file / cp-recursive)。
       tizix はオーバーレイも動的ロードも無いので 1 プロセス = 4096B が絶対の
       天井であり、**分割は予算そのものを増やす唯一の手段**。kexec_argv は
       drv_tbl[27] で公開済みなので起動自体は今でもできるが、先に 3 点が要る:
         (1) exec(自分を置き換える)が無い。kexec_argv は空きブロックに新しい
             プロセスを作るだけなので、親が即 exit すると sh が「コマンドが
             終わった」と判断してしまう。
         (2) リダイレクト / パイプが子に継承されない(結線はブロック単位)。
         (3) 一瞬ブロックを 2 つ食う(6 枠しかないのでパイプ中に破綻する)。
       → 導入の動機は「今あるコマンドを縮める」ではなく **再帰系(cp -r / rm -r /
         ls -R / find)を入れること**。着手はその段で。
     ・cwd のカーネル所有を x86-ia16 へ横展開(今は builtin シェルの旧方式)。
     ・x86 time syscall: epoch 秒 get/set(date 用)。
     ・pager/画面制御: 端末サイズ・カーソル制御(当面 24 行決め打ち + VT100)。
     ・getchar のブロッキング: 今 _isr80 は IF=0 で回すため、入力待ちの間
       タイマが止まり他プロセスも止まる。wait/wake が要る。

  --- 進捗(changelog)---
  2026-09-09: [task #31] user/*.c をコストモデルで見直し + tzcc をねじ曲げた。
              **coreutils 22 本で 43689B → 31604B (-12085B, -27.7%)。
              全 22 本が 1 ブロックに収まり(cp が 2→1)、最小スタック余裕 1108B。**
              カーネルは無変更。回帰 6 本 PASS + tzcc runtest z80 45/45・x86 42/42。
              (1) [tzcc] **IX ベース方式**。IX = IY + tzc_vb + 128 を張り、スカラ
                  変数を (ix+d) で触る(読み書き 15/17B → 6B、char は 3B)。_DATA を
                  並べ替えてスカラを先頭へ集め、オフセットは tizix.c が自分で決める。
                  IX の張り直しは 関数入口と call 直後だけ。仮引数は SP 相対へ。
                  43689 → 38505B。詳細は上の「tzcc のコストモデル」。
              (2) [tizix] **cp を逐次コピーへ**。(1) で cp が 1 ブロックに落ちた
                  結果スタックが 191B しか残らず、FatFs 呼び出しがイメージ末尾の
                  文字列リテラルを踏んで printf が無言になっていた(test_vfs_step10
                  が 4 件 FAIL)。512B バッファを捨てて dd と同じ fgetc/fputc の
                  逐次コピーにし、**512B 上限も撤廃**。3585 → 2668B / スタック 1108B。
                  旧設計の「同時に 2 ファイルを開かない」(FF_FS_TINY 懸念)は
                  dd が反例で、現行 FatFs では窓の再ロードが噛むだけと確認済み。
              (3) [tzcc+tizix] **printf を 14 本から排除**。tzcprintf は 546B の
                  1 モジュールで、%s と 10 進しか要らないコマンドまで丸ごと払って
                  いた(ls は printf(" <DIR>  ") = 定数文字列で引き込んでいた)。
                  libtzc に tzcout(prs / prnum / prnuml、3 本で 200B 弱)を新設。
                  38505 → 33301B。境界値 15 件(0 / 65535 / 65536 / 4000000000 等)を
                  cpmsim で確認済み。
              (4) [tzcc] **libtzc の string を用途別 4 モジュールへ分割**
                  (tzcstr / tzcstrn / tzcstrstr / tzcmem)。sdld は .rel 単位で引くので
                  strstr 1 個のために 625B 全部を払っていた。grep 2460→1881 /
                  uniq 2582→2123。
              (5) [tizix] **date を圧縮** 2590→2162B。32bit を使う範囲を
                  「epoch 秒 → 日数 + 秒余り」の 1 段に限定(days/ydays を unsigned へ)、
                  print_pad(pow10 テーブル + 桁ごとの減算ループ)を 2 桁固定の p2() に。
              (6) [tizix] **仮引数の削減**(grep / uniq / ls / tail / wc)。tzcc は
                  ローカルも静的領域なので仮引数は損。ファイルスコープを直接触る形へ。
              (7) [tzcc] **関数間の同名ローカルを検出**(generator.c
                  check_dup_locals)。tzcc はスコープを持たず同名は同一記憶域を
                  共有するため、A が B を呼ぶと A の値が黙って壊れる。#31 で
                  user/date.c が実際に踏み `1970-01-00` を出した(p2 のローカル `d`
                  が main の `d` と同一)。--tizix-user ではビルドエラー、素の経路
                  では警告にした。この検出で grep/uniq/ls/tail/wc の潜在衝突も
                  見つかり、(6) の仮引数削減で解消した。
              副産物(未着手・上の「未着手の基盤拡張」に記載): switch の分岐単価は
              if/else 連鎖の約 1/8(実測 7 分岐で -252B)。dispatch のレイアウト
              改善で更に 1 case あたり ~14B。
  2026-09-08: [task #30] tzcc の PIC グルー削減 (a) jr 化。**coreutils 22 本で
              48281B → 43689B (-4592B, -9.5%)。** カーネル・sh は無変更
              (sh は SDCC ビルドで #28 済み)。
              tizix.c がセグメント内 jp を間接 JP 列(無条件 14B / 条件 16B)へ
              展開していたのをやめ、jr / jr cc へ落とすようにした(3B → 2B、
              グルー 0B)。jr は相対分岐なので base 非依存で変換不要 ── #28 で
              SDCC 側に入れたのと同じ理屈。範囲外(±127 超)になった jr だけを
              後段の tzcc/jrfix.py が間接列へ戻す(sdasz80 の Branching Range
              Exceeded を捕まえて収束まで反復。tizix user/iy_jrfix.py の移植)。
              実測では wc/tail で 7 個・uniq 4 個・sleep/tee 2 個が範囲外で、
              いずれも 1〜2 反復で収束。
              主な削減: ls 3397→3007 / wc 3510→3126 / grep 3503→3153 /
              head 2828→2454 / tail 3056→2716 / cp 4644→4190 / mv 1972→1710 /
              sleep 1422→1210 / cat 2117→1917。id/uname/whoami は分岐が
              少なく不変。
              副次効果: uniq の実効スタックが 264B → 638B に回復したので、
              #29 でやむなく 64 へ落とした LINE_LEN を **128 へ戻した**
              (uniq.bin 3266B、スタック 510B)。
              あわせて tizix.c の潜在バグを 1 件修正。旧実装は `jp z,` しか
              個別処理せず、`jp nz,LBL` は汎用枝に落ちて `ld hl,#nz,` という
              壊れたシンボルを吐いていた(tzcc が nz を出さないので露見せず)。
              新実装は条件部を正しく切り出し、jr にできない条件(po/pe/p/m)は
              素通しせず **ビルドを止める**(素通しはゼロページへの wild jump に
              なり実行時にしか分からない)。
              残る (b)(c)(d) は検討のうえ **保留**。(a) の時点で coreutils 22 本中
              21 本が 1 ブロックに収まり、これ以上やっても機能的に変わるのは cp の
              1 ブロック化だけになったため。方針として **コンパイラの構造変更は
              最後の手段** とし、先に user/*.c をコストモデルで見直す。
              数値と却下理由は上の「tzcc のコストモデル」節。
              検証: 回帰 6 本 PASS + 全コマンド smoke(相対パス/パイプ/リダイレクト/
              ovf)+ tzcc test_all.sh(ok_tz1..tz7 含む。test99 は既存 FAIL)。
  2026-09-08: [task #29] #28 から持ち越した「既知の不具合」4 件を処理。
              3 件が実バグで、原因は 2 つに割れた。1 件は再現せず。
              (1) uniq FILE のゴミ出力 = スタック枯渇。1 ブロックプロセスの実効
                  スタックは 3776 - バイナリサイズで、uniq.bin=3640B のとき
                  残り 136B(全コマンド中 最小)。fgetc → FatFs でスタックが
                  _DATA 末尾の line[]/prev[] を踏み潰していた。LINE_LEN 128→64。
              (2) du FILE / tee = tzcc が条件式中の代入を黙って誤コンパイル。
                  `while ((c = getchar()) != EOF)` で条件が裸の c になり、代入は
                  ループ外へ、比較は捨てられ、本体はループの後ろへ落ちる。
                  両コマンドを for(;;) + break へ書換え、**tzcc 側はこの形を
                  ビルドエラーにした**(reject_assign_in_cond)。既存コマンドを
                  全数リビルドして他に該当が無いことを確認済み。
                  あわせて du FILE を実装(fseek(END)+ftell)。
              (3) `cat f|grep pat` の末尾空行は再現せず。#28 で sh の skip_first が
                  消えた際に解消したとみられる。
              副産物: tzcc の PIC グルーを実測(wc で 1618B = 46%)。削減の 3 案と
              「裏レジスタは使えない」理由を上の「未着手の基盤拡張」へ記載。
              検証: 回帰 6 本 PASS(test_5b_pipe / test_pwd_cd / test_vfs_step8/9/10 /
              test_cmds_all)。カーネル無変更。
  2026-09-08: [task #28] sh / コマンドの肥大解消 + コマンド引数の恒久対策。
              「sh・ls の 4KB 超過」「getopt」「コマンド引数」の 3 件は独立ではなく
              2 つの根本原因に還元できた。
              (1) 実行時 IY 相対 PIC のグルー。iy_reg が jr まで間接化しており
                  (1 個 14-16B)、その誤変換を避けるため Makefile が --no-peep を
                  付けて「そもそも jr を出させない」運用になっていた。結果 sh.c の
                  分岐は jp 405 / jr 0 に膨らみ、405 個すべてにグルーを払っていた。
                  jr は相対 = base 非依存なので触る必要が無い(iy_reg_claude.py
                  ヘッダ [2] に本来の方針が明記されていた)。素通しに戻して
                  peephole を復活 → jp 35 / jr 198、変換数 492 → 122。
                  範囲外になった jr だけを user/iy_jrfix.py が後段で間接化する
                  (sh で 198 個中 7 個・1 反復で収束)。sdasz80 が Branching Range
                  Exceeded で止めるので黙って壊れない。あわせて iy_reg に検証パスを
                  追加(未変換の再配置参照が残ったらビルドを止める)。
                  sh.bin 11589B(3 ブロック)→ 6507B(2 ブロック)。commit 7f4c62a。
              (2) sh が「どの引数がパスか」を推測していた。cwd をカーネル所有に
                  変え、パスを受け取るカーネル入口(drv_open / kdir_open / kfs_* /
                  redir_begin / in_begin)が kpath() で解決する形へ。sh から
                  path_norm / is_path_cmd / prev_optarg / is_kv / skip_first を
                  全廃。パス正規化スクラッチはブロック毎に持つ(共有 1 枚だと
                  「解決 → プリエンプト → 別プロセスが上書き」の競合)。
                  sh.bin 6507 → 5145B(11589B から -56%)。commit 4ea26e4。
              効果: プロセス枠 6 個に対し sh が 2 ブロックになり、`a &` を走らせた
              まま `ptx 30 | wc` が通る(従来は sh3+パイプ3 で満杯)。
              サブディレクトリからの相対パスが全コマンドで通る(sh は各コマンドを
              知らないので、未テストの組み合わせでも成立する)。
              検証: 回帰 5 本 PASS、フレーク 9/9 PASS、相対パス smoke 一式。
  2026-09-08: [task #27] カーネルパイプを 4KB ブロックバッファに統一(旧 5b の 128B
              KW_PIPE リングを置換)。pipe_setup がプロセス枠の空きブロックを 1 個
              PID_PIPEBUF(0xFC、crt0.s sched スキップ)で確保 → BLKBASE の 4096B を
              線形バッファに、pipe_teardown で解放。wrap/フロー制御なし・総量 4096B
              到達で ovf(sh が "out of memory")。streaming/動的拡張は malloc 実装後。
              A|B の起動・結線・監視は sh(iy_reg 税・3 ブロック制約)からカーネルへ
              移設: src/pipe.c krun_pipe()。sh の run_kpipe は結果コードを見るだけ。
              tail のパイプ後段は src/pipe.c pipe_tail() が 4KB 窓を後方スキャンして
              末尾 N 行を kputchar → `cat|tail` が動く(#26 のスタック枯渇解消)。
              `tail < file` は sh が `tail file` に書換え。drv_tbl[39]=pipe_ovf /
              [40]=pipe_tail / [41]=krun_pipe。前提として sh を 3 ブロックへ圧縮
              (14993→11507B、user/crt0sh.s = argv/プール域をスタックへ回収)。
              検証: test_5b_pipe / test_pwd_cd / test_vfs_step8-10 / test_cmds_all PASS。
  2026-08-28: rx を cpmsim 検証。drv_printf に %ld/%lu。date_dbg/cp_dbg 消しこみ。
  2026-08-29: [対応済] driveb.dsk 競合。arch 分離(src/ + arch/<arch>/)。
  2026-08-29: x86-ia16 M1..M5。共有 src/(ff.c 含む)を ia16-gcc で通し、二段ブート
              → FAT12(INT 13h)→ ls/cat → PIT 100Hz → プリエンプティブ context
              switch → hello/a/b(C)。C コマンドツールチェイン。z80pack バイト不変。
  2026-08-30: x86 file 系 syscall(open/read/write/opendir/readdir/…)+ wc/cp/xxd(C)。
              _isr80 の SS 切替 + SS:SP 両退避。ここで x86 コマンド作業を一旦中断。
  2026-08-30: [z80] pwd / cd 復活。src/sh.c に cwd[](ルート起点)。cd は相対/絶対/
              "."/".." を path_norm でテキスト正規化し fat_isdir で実在検証。sh は
              ディスパッチ前に ls/cat/rm/mv/mkdir の引数と > / < ファイル名を cwd 起点で
              絶対化(cwd=="/" の間は完全ゼロコスト・従来と同一)。fat_ls が引数の
              ディレクトリを列挙するよう変更。カーネルが 0x7323→0x791B に伸び
              NSEC 240→248(boot.s / arch.mk、限界 0x7900→0x7D00)。
              検証: python/test_pwd_cd.py 27/27 PASS(cpmsim)。
  2026-08-30〜: VFS 一本化(下記「==== VFS 一本化 ====」)に着手。1 ステップ毎に git コミット。
    Step 1 (a2643ea): vtree 縮小(/ + /dev + /dev/null)+ vfs_resolve() 追加。
    Step 2 (bf088cf): fat_* を vfs_resolve 経由に。ls /dev 列挙・cat /dev/null=EOF・
                      /dev 配下の書込み系拒否・redir/in_begin ガード。
    Step 3 (71c63c0): do_cd を vfs_resolve 経由に(cd /dev 可、/dev/null 不可)。
    Step 4 (aa25c2e): DRIVER drv_open を vfs_resolve 経由に + _vfs_resolve 公開。
                      外部コマンドから /dev 配下は当面弾く(実体アクセスは Step 5)。
    Step 5a(72426c7): スケジューラ wait/wake(proc_block/proc_wake、KW_BLOCKED/
                      KW_WAKEPEND、crt0.s sched_pick が blocked を飛ばす)。
    Step 5b: カーネルパイプ(src/pipe.c)。★2026-09-08 の task #27 で 128B KW_PIPE
             リング → 4KB ブロックバッファに置換、run_kpipe もカーネル移設(上の
             changelog 参照)。以下は 5b 当時の記述。
             io.c kputchar が ROUTE_PIPE→pipe_putc、kgetchar が pipe_is_reader→
             pipe_getc。sh の A|B は両側外部なら run_kpipe(reader→pipe_setup→
             writer→ROUTE_PIPE)、片側 builtin なら従来の一時ファイル方式。
             EOF は sh の wait ループが pipe_note_exit で相手へ通知。
             reader が先に終了したら run_kpipe が writer も PIDTBL=0 で刈る
             (SIGPIPE 相当。`a | echo` のように writer が -1 を無視して無限
             ループするケースのハング対策。2026-08-31)。writer が kdir 反復中
             (ls)に刈られると KD->inuse が残るので kdir_open が自己回収する。
             NSEC 248→254(5a+5b でカーネルがピーク。Step 7 以降の外部化で戻せる)。
             ★cpmsim: FatFs disk_read を di で囲むと FDC が失敗する。kexec_file を
               割り込み禁止で呼ばない(pipe 結線だけ IRQ_OFF、.BIN ロードは通常状態)。
  2026-08-30: [z80] 長年のフレークな Op-code trap / wild jump を解決(コミット 43d36f8)。
    真因: crt0.s が _DATA 域(未初期化 static)をゼロクリアしていなかった。SDCC 標準
    crt0 は必ず埋めるが、この自作 crt0 は gsinit(初期値コピー)+ 手動 KW_* ゼロ化しか
    しておらず、kernel.ihx も >= 0x8000 のレコードを持たない → ff.c の
    static FATFS *FatFs[1](番地 0x80f2)や FATFS fs の各フィールドがゴミ値で起動。
    fat_init→f_mount の「古い登録 fs をクリア」処理 cfs=FatFs[0]; if(cfs) cfs->fs_type=0;
    がゴミポインタを deref し、カーネルコード(f_opendir 等)へ 0 を書き込む → 暴走。
    cpmsim は malloc(64KB) がゼロページ(mmap)になる時だけ無事なので ~13% のフレーク
    に見えていた(実機 SRAM ならほぼ毎回踏む)。派生症状の driveb.dsk 肥大
    (256256→259584B、FAT12 ボリューム外の LBA へ書込み)も同根で解消。
    修正: crt0.s の call gsinit 直前に _DATA ゼロクリアループ(SDCC 標準 idiom)を追加。
    kernel 終端 0x7EA8→0x7EBE(+22B)、NSEC=254 の 0x7F00 制限内。
    検証: 27 コマンド列 × 155 ラウンドで修正前 ~13% クラッシュ → 修正後 0、driveb.dsk 不変。
    別途: cpmsim fdco_out の track 境界オフバイワン(track > .tracks → >= .tracks。
    .tracks は本数・track は 0 起点)も修正(z80pack-tizix、git 外)。tz80 で一度も
    再現しなかったのは kosarev z80 のメモリがゼロ初期化だから(FatFs[0]=0 で deref 回避)。
    tz80.py に履歴リング・スタック深トラッカ・pc-datazone/sp-deep watch を追加(3dbe27c)。


==== カレントディレクトリはカーネルが持つ (#28, 2026-09-08) ====

  それまで cwd は sh(user/sh.c)が持ち、sh が「どの引数がパスか」を推測して
  起動前に絶対化していた。推測の中身:
    ・is_path_cmd … パスを取るコマンド 16 個のハードコードリスト
    ・prev_optarg … 直前トークンが "-n" なら次は行数なので絶対化しない
    ・is_kv       … '=' を含むなら key=value(dd の if=/of=)なので絶対化しない
    ・skip_first  … grep の第 1 引数は PATTERN なので素通し
  この積み重ねが `wc -l` を "cannot open -l" にし、コマンドにオプションを
  足すたびに sh が壊れる原因だった。オプション解釈を入れても直らない ──
  そもそも sh が引数の意味を知ろうとするのが誤り。

  cwd はカーネルが持ち(KW_CWD)、**パスを受け取るカーネル入口が解決する**:
    drv_open(DRIVER) / kdir_open / kfs_mkdir / kfs_unlink / kfs_rename /
    redir_begin / in_begin   ← いずれも先頭で kpath() を通す
  sh は cd と プロンプトのために kchdir(drv_tbl[42]) / kgetcwd(drv_tbl[43])
  を叩くだけ。argpack はトークン化した文字列をそのまま積む(中身を見ない)。

  ★スクラッチは「ブロック(プロセス)ごと」に持つこと (KW_PATHS, 8×2×48B)。
    共有 1 枚にすると「A が kpath で解決 → f_open を呼ぶ前にプリエンプト →
    B が同じバッファを上書き → A が B のファイルを開く」競合が起きる。
    `cat a | tee b` のように両側が fopen するパイプや `&` で到達しうる。
    KW_CURRENT(実行中ブロック)で添字する。2 枠なのは kfs_rename が src/dst を
    同時に要るため。

  効果: sh.bin 6507 → 5145B(#28 の jr 対応と合わせ 11589B から -56%)。
  x86-ia16 は builtin シェル(src/sh.c)が自前の cwd を持ち絶対パスで降りて
  くるので、この機構は #ifndef ARCH_X86_IA16 で丸ごと除外している。

  残: ls は path がちょうど "/" のときだけ /dev を合成表示するため、無引数 ls
  には sh が cwd を 1 トークン足している(ls_default_cwd)。本来 kdir_read が
  "/" の反復で dev を返すべきで、そうすればこの特別扱いは消える。


==== VFS 一本化(パス解決の統合)====

  背景: これまで「パス解決器が 2 つ」あった ── sh.c の path_norm→FatFs 直行 と、
  vfs.c の vfs_lookup→vtree(/dev/null 判定でしか使われず)。vfs.h は「/ に FAT が
  マウントされている」体で書かれていたが、実行経路は誰も vnode を見ていなかった。
  これを 1 本にし、/dev オーバーレイをコンパイル時固定で効かせる。

  3 層:
    パス層  (sh.c path_norm)   cwd / "."/".." / 相対→絶対。大文字化はしない。文字列のみ。
    名前空間層(vfs.c vfs_resolve) 正規化済み絶対パス → backend 振り分け。
                                オーバーレイは "/dev" 固定 1 エントリ、他は全部 FAT。
                                fstab / mount コマンドは作らない。
    バックエンド層(fatcmd.c / vfs.c) FAT(FatFs)/ DEVFS(vtree)。解決済み対象を実処理。

  鉄則: ファイルパスを取る操作は必ず vfs_resolve を通す(cd だけでなく open/read/
        write/close/lseek/opendir/readdir/unlink/mkdir/rename 全部)。
        z80: commands/DRIVER 公開 FS ベクタを VFS 経由へ差し替え、vfs_resolve も
             公開シンボル化(user/Makefile FS_SYMS に追記)。
        x86: sysfile.c sys_call() のパス系入口で vfs_resolve。

  vfs_resolve() 戻り値(src/vfs.h):
     >= 0 : DEVFS。戻り値 = vtree index
     -1   : FAT が担当(パスは絶対パスをそのまま f_* へ)
     -2   : "/dev" 配下だが該当ノード無し(ENOENT)

  /dev の扱い:
    ・オーバーレイは /dev のみ。/proc は将来やる可能性ありだが容量都合で今回省略。
      /bin /root /var /var/log /var/run の空 stub は削除。
    ・/dev は純粋に合成(ディスク上に無い)。mkdir /dev / rm /dev は不可で弾く。
    ・ls / は dev を合成表示(外部 ls 側で対応)。
    ・> /dev/null の特別扱いは vfs_resolve→DEVFS null に正規化して吸収。

  カーネル / 外部の分割(2026-08-30 確定):
    ・builtin は cd / exit / kill / pwd の 4 つだけ。他は全部外部コマンド化。
      cd/exit はシェル自体を制御、kill は pid_tbl 直接操作、pwd は cwd 表示。
    ・外部化対象: ls cat echo mkdir rm mv cp df ps uptime tree touch head tail
      more grep … (= NSEC 上限 248/254 の解消策も兼ねる)
    ・tree は「外部化タスクを残してスケルトンだけ切り出す」。中身は作り切らない。

  パイプ(本物のカーネルパイプ。両側外部のとき一時ファイル方式は撤去):
    ・ランデブー方式。カーネル常駐バッファは持たない。writer ブロックの buf →
      reader ブロックの buf へカーネルが直接コピー(Linux は 64KB 常駐リングだが
      8bit ではカーネル RAM 節約を優先。コンテキストスイッチ増は readme 既定の
      許容範囲)。
    ・sh: A | B を見たら両方を実プロセスとして起動。pipe_setup(Wブロック,Rブロック)。
      各プロセスが自ブロック内に buf 確保 → pipe_attach(role, buf, len)。
      writer putchar/fwrite → ROUTE_PIPE、reader getchar/fread ← パイプ。
      writer は buf 満杯で pipe_flush(n) → カーネルが reader buf へコピー・reader を
      wake・コピー完了まで writer をブロック。writer 終了+出し切りで read=0(EOF)。
      reader が先に死ねば flush=-1。
    ・片側が builtin(pwd | grep 等)のときだけ従来の一時ファイル方式を fallback。
    ・土台 = スケジューラ wait/wake。readme「SD ドライバプロセス段で要る」本物の
      block/wake をここで作る(SD 段で再利用)。

  実装順(1 項目ずつ・各ステップ cpmsim 検証・各ステップ git コミット):
     1〜4  [済] vtree 縮小/vfs_resolve、fat_*・do_cd・DRIVER を VFS 経由に
     5a  [済 72426c7] スケジューラ wait/wake プリミティブ(proc_block / proc_wake)
     5b  [済 cbe32cd] カーネル pipe オブジェクト + syscall + ROUTE_PIPE
     5c  sh のパイプ経路を「両側外部→カーネルパイプ」に(builtin 絡みは一時ファイル)
     6   [済 7d41592] DRIVER/カーネルに kdir_open/read/close + vfs_dir_next 公開
     7   [済 7d41592] ls 外部化(user/ls.c、LS.BIN)。ls / に dev 合成。builtin 表から削除
     ── ここで多発したフレーク crash は crt0.s の _DATA 未初期化が真因(43d36f8 で解決)。
        Step 8 以降を再開可。──
     8   [済] cat / echo 外部化(user/cat.c, user/echo.c → CAT.BIN / ECHO.BIN)。
         ・fat_cat / fat_echo を fatcmd.c から撤去、builtin 表からも削除。
           カーネル s__INITIALIZER 0x7EAB → 0x7D2F(-380B の余裕)。
         ・sh の `> FILE` / `< FILE` 分岐を builtin 専用から外部にも開放:
           redir_begin/in_begin でグローバル redir_on/in_on を張ったまま
           exec_external を必ず前景実行 → 子ブロックの putchar/getchar が
           kputchar→redir_sink / kgetchar→in_src に乗る。終了後 redir_end/in_end。
         ・`cat FILE` `cat < FILE` `A | cat` `cat FILE | B` `echo T > FILE`
           `echo T > /dev/null` `echo T | B` 全て cpmsim 検証(test_vfs_step8.py)。
         ・外部 cat は /dev 実体を開けない(drv_open が非 FAT を弾く = Step 4 の
           "当面弾く")。旧 builtin の `cat /dev/null`=即EOF は "cannot open" に変化
           (test_vfs_step2.py 更新)。
         ・既知の制限: 端末直結の対話 `cat`(引数/</パイプ 無し)は前景待ちの
           con_break() と stdin を取り合う(memory: ctrlc-kill-fg の既知クラス)。
     9   [済] mkdir / rm / mv 外部化(MKDIR.BIN / RM.BIN / MV.BIN)。
         ・fat_rm / fat_mkdir / fat_mv + deny_nonfat + split2 を fatcmd.c から撤去。
           代わりに kfs_mkdir / kfs_unlink / kfs_rename(drv_tbl[24..26])を追加。
           vfs_resolve で非 FAT(/dev 配下)を弾き、実処理は f_mkdir/f_unlink/f_rename。
           エラー表示はコマンド側。戻り 0=OK / 0xFF=denied / 1..19=FRESULT
           (0xFF 番兵 = 掟の符号付き比較禁止を回避。コマンドは == で判別)。
         ・sh の TMP.PIP 掃除も fat_rm → kfs_unlink に(kprintf ノイズも消える)。
         ・カーネル s__INITIALIZER 0x7D2F → 0x7B22(Step 8 と合わせ -900B 超)。
         ・cpmsim 検証: test_vfs_step9.py 全 PASS(mkdir/rm/mv 正常系・
           /dev 配下 permission denied・存在しない対象 error N・cwd!="/" 相対)。
    10   [済] cp ディレクトリ宛て対応 + touch 新規。
         ・cp(user/cp.c、既に外部のみ): dst が既存ディレクトリ(opendir で開ける)
           なら dst/basename(src) へ。dst=="." は sh が cwd!="/" のとき絶対化して
           渡す/"/" のときは cp が "/" と解釈。判定は opendir/closedir(既存
           drv_tbl[21..23])だけで完結、カーネル改変なし。CP.BIN 1566→2312B。
         ・touch(user/touch.c、TOUCH.BIN 新規): fopen "r" で存在確認 → 無ければ
           "w" で空作成(既存は非破壊。"w"=CREATE_ALWAYS の切詰めを避ける)。
           /dev 配下は drv_open が弾く(cannot create)。
         ・sh resolve_arg の 1 引数グループに "touch" 追加(cwd!="/" 絶対化)。
         ・cpmsim 検証: test_vfs_step10.py 全 PASS(cp SRC DIR / cp SRC DIR/NAME /
           cp SRC . / touch 新規・非破壊・/dev 拒否・cwd!="/")。回帰群 PASS。
    11   df / ps / uptime 外部化
    12   tree 外部スケルトン化 + カーネルから撤去
    13   head / tail / more / grep 新規
    14   x86 パリティ(sysfile.c の VFS 経由化 + 同じ外部コマンド群 + パイプ)


==== 今後: SD カードドライバプロセス ====

実機では driveb.dsk(FAT イメージ)が SD カードに置き換わる。SD/SPI ドライバを
カーネルにも DRIVER 常駐(パンク寸前)にも入れず、外部の「SD カードプロセス」に
持たせる方針。

  役割分担:
    ・SD カードプロセス = 生ブロック I/O だけ。bit-bang SPI(I/O ポートビットで
      MOSI/MISO/SCK/CS) + SD コマンドプロトコル。「ブロック N から 512B(or 指定長)
      読め/書け」に答える。= diskio.c の disk_read/disk_write をプロセス化したもの。
      マルチブロックプロセスとして boot 時にロード。port 専有。小さく保つ。
    ・FatFs(ff.c)はクライアント側(カーネル/VFS)に据え置き。クラスタチェーン・
      ディレクトリ・FAT テーブルの解釈は FatFs の仕事。生ブロックを見て振る舞う。
    ・ESP32 WiFi は "UART の延長" 扱い。TCP スタックは 8bit CPU に載せない。ESP 側が
      WiFi+TCP、Z80 は薄いコマンド(AT 等)を投げるだけ。

  実装ピース:
    1. SPI + SD プロトコル → SD プロセス(既存 prior art: tizix 外の bios.s /
       spi.s / sdcard.s に bit-bang SPI・SD SPI モード init・ブロック read・
       bios_call の IX フレーム dispatch ABI まで動くものがある。移植ベース)。
    2. diskio.c の disk_read/disk_write → IPC スタブ。要求を SD プロセスへ投げ、
       呼び出し元をブロック、512×count バイトを受けて戻す。
    3. メールボックス IPC: 512B 用の共有バッファ + ドアベル(フラグ or syscall)。
       制御/通知はパイプかシグナル、データ本体は共有メモリ(コピー回避)。
    4. scheduler の wait/wake: syscall 処理中に disk_read で SD プロセスの応答を待つ
       間、呼び出し元プロセスを sleep させ SD プロセスを走らせる。
       (今の前景待ちはシェルの spin ループ(Ctrl+C 検出の con_break ポーリング付き)。
        これを本物の block/wake に。)
    5. FatFs の単一入場ロック: A が FatFs 内で SD 待ちブロック中に B が入らないよう。
       ff_req_grant / ff_rel_grant(FF_FS_REENTRANT)フックに嵌める。
    6. 起動のニワトリ卵: SD プロセスの .BIN を読むのに必要なのは生セクタ読みだけ
       (FAT 解釈不要)。boot.s かカーネルの最小 SPI ルーチンでイメージを 1 個
       引っ張れば済む。

  注意: プリエンプティブ下で SPI 転送中に ISR が入るのは可(クロック止められる)。
  SD init / CS またぎのアトミック区間は di/ei を検討。512B シフトアウト本体は
  割り込まれても平気。bit-bang で 512B 転送が既に ms オーダーなので、1 セクタあたり
  1 コンテキストスイッチのオーバーヘッドは誤差。


==== 8/31 ディスカッション(カーネル/ドライバ分割とレトロ機移植の方針) ====

  カーネル ROM 肥大(_CODE の約 62% が FatFs、像終端 0x7CCE / 上限 0x8000、
  空き 818B)と block1(driver 常駐、3736/4096B = 91%)の逼迫を発端に、
  「何をカーネルに残し、何を backend へ出し、どこまで小さくできるか」を整理した。

  --- 現状のサイズ実測(2026-08-31 ビルド)---
    ROM 像 : _CODE 0x0120-0x796B (30795B) + _HOME 847B + init 20B → 終端 0x7CCE。
             空き 818B(上限 0x8000 = DATA_LOC)。NSEC=254 は最大値(実需 248)。
             内訳: ff.c(FatFs)+static ≈ 18-20KB(~60%)。他は sh ~2.2K /
             crt0+init+kernel ~2.8K / kexec ~2K / vfs ~1.25K / fatcmd ~1.5K /
             io ~0.7K / 残小。
    block0 : 0x8000-0x8FFF。使用 ~2.2KB(FATFS fs ~560B が最大)/ 空き ~1.8KB。
             スタックは 512B「運用上の約束」でガード無し(潜在リスク)。
    block1 : 0x9000-0x9FFF。drvvec 54B + driver.c stdio 実装 ~3682B = 3736B。
             data/bss ゼロ。fd_table は block0 0x8600 へ退避済み。
    drv_tbl: 0x9000 の .dw 配列(jp 表ではない)。commands は -g _drv_tbl=0x9000 の
             1 シンボルのみでリンクし drv_tbl[N] 間接呼び。[0..18]=block1 実体、
             [19..26]=カーネル ROM 番地(FS_SYMS が kernel.map から注入)。
    → driver は iy_reg を通さず 0x9000 固定リンク。中身は「ドライバ」ではなく
      コマンド側 stdio ランタイム(実 I/O は kputchar 0x003E / kgetchar 0x0041 =
      ROM に落ちる)。「DRIVER」の名は readme 以前の CP/M-BIOS モデルの化石。

  --- 認識: 32K ROM + 32K RAM 分割は「あの電子工作基板の都合」---
    移植性の指標は ROM/RAM 別勘定ではなく「合計常駐フットプリント」。
    RAM-only 機(PC-8001 テープ起動)では ROM/RAM の区別が無く、コード+ワーク+
    プロセス枠の総和が全て。現行 ~37KB は 32KB 合計に収まらず、別々の 32KB
    プールがあるベアメタル基板でだけ成立している。32K ROM 前提を貫くと
    「ベアメタル自作一択」に近くなる(PC-8001 はマザー ROM 差し替えが必要で
    誰の魅力にもならない)。

  --- ターゲット 2 クラス ---
    hosted (MSX / PC-8001 / PC-8801 / CP/M 機):
      ベアメタルではない。ホスト BIOS/BASIC ROM がコンソールとディスクを持つ。
      tizix は中核だけ運ぶ。console = ホスト 1 文字 I/O へ trampoline。
      FS = ホストのファイル/セクタルーチンへ trampoline(MSX-DOS は FAT12 内蔵)。
      FS コードもデバイスドライバも運ばない。中核 ~7.5-9KB。
    bare-metal (手持ち Z80 基板 + SD / cpmsim):
      全部運ぶ。32K ROM がある。FatFs は ROM 常駐で可。

  --- ターゲット・ティア ---
    PC-8801 全機種 / PC-8001mkII+ / bare-metal 基板 : 快適。フル構成。
      retro のリファレンスは PC-8801(64KB RAM 標準、ROM バンクアウト可、FDD 内蔵、
      M88 / QUASI88 で検証容易)。
    PC-8001 + 32KB 拡張 / MSX(RAM 相応)          : 実用最小。中核 + 数枠。
    PC-8001 16KB / 小 RAM MSX                     : デモティア。動くには動く。非サポート。
    → 設計のベースライン = 「使える RAM 32KB」。16KB は削れば通る余地を残すだけ。
      実用は「PC-8001 なら 32KB 拡張でお願いします」。PC-8801 は楽勝。

  --- PC-8001: テープ + RAM ディスク ---
    N-BASIC が低位 ROM を占有・バンク切替なし → RAM は 0x8000-0xFFFF の最大 32KB。
    テープ(N-BASIC の CMT ルーチンへ trampoline、FSK デコードは ROM 任せ)を
    「起動時ローダ」に、FS backend は RAM ディスク(~0.5KB)に割り切る。
      起動時に「アーカイブ」(全コマンド+初期ファイル)をテープから RAM ディスクへ
      1 回シーケンシャル読み。以後すべて RAM ディスク。永続不要 or 終了時に 1 塊で SAVE。
    ストレージ/FS サブシステムが ~6-20KB → 1KB 未満に。~150B/s なので 12KB
    アーカイブで ~80 秒のブート(セッション 1 回きり、時代相応)。
    PC-8001 は独立 arch フレーバ(ARCH_PC8001)。disk/FAT 経路と一切共有しない。

  --- カーネル/ドライバ分割(結論)---
    カーネル中核(全ターゲット共通・不可分、~5.5-6.5KB):
      crt0(reset / バンク切替フック / _DATA ゼロ化 / ベクタ設置)
      scheduler(round-robin / context switch / pid_tbl / proc_block・wake)
      timer ISR + 時刻(除算を持たない)
      kexec(プロセス像ロード / 偽コンテキスト / マルチブロック)
      VFS + fd 層(パス解決 / backend ディスパッチ / fd 表。FS 本体は持たない)
      pipe(カーネルパイプ)
      最小 kprintf(パニック / バナーのみ)
      算術ランタイム(16bit。32bit long は resident 消費者が無ければ削る=FAT が
        外れれば不要になる)
      ベクタ表(ISR jp / kexit / kputchar・kgetchar / time / 算術 / call_hl)
      ※ kputchar / kgetchar はベクタだけ中核。実体は backend。
    backend(arch ごと・差し替え、= console phys + storage + FS backend):
      bare-metal SD : SPI bit-bang + FatFs / compact-FAT
      cpmsim        : FDC + FatFs
      MSX           : disk BIOS trampoline(host FAT12)
      PC-8801       : サブ CPU FIFO or N88-DISK trampoline + N88 / RAM ディスク
      PC-8001       : テープローダ + RAM ディスク
    stdio(printf / fopen / fgets / …):
      カーネルでも backend でもなくライブラリ。固定番地でコマンドから届く必要 →
      ベクタ表経由。カーネル像に畳み込む(f_* ラッパは VFS 呼び出しに置換して薄く、
      drv_printf は kprintf と共用部分あり → 純増は ~2KB 見込み)。純関数
      (string / stdlib)は従来どおりコマンド側 .rel。

  --- block1 の RAM ブロブは廃止 ---
    ROM ブート・RAM ブートのどちらでも「ドライバはカーネル像の一部」。
    kload_driver() と /bin/driver.bin を削除。drv_tbl 間接は低位(arch 依存番地)
    ベクタ表へ吸収 → commands は kputchar と同じ `call 0x00XX` 方式。二重間接が消え
    わずかに速く・小さく。
      ROM ブート : 中核 + backend + stdio が 1 ROM 像。ベクタ ROM 常駐。RAM は 100%
                   ワーク + プロセス枠。
      RAM ブート : 同じ 1 像をブートストラップが RAM へ展開、crt0 がベクタ設置。

  --- メモリレイアウト(RAM 32KB 基準)---
    ROM / 低位(ROM ブート)or RAM 先頭(RAM ブート):
      カーネル像(中核 + backend + stdio)+ ベクタ表
    block0+1(0x8000-0x9FFF, 8KB): システムワーク RAM(データのみ)。
      _DATA / PCB / VFS ノード / fd 表 / pipe struct / RAM ディスク領域 /
      スタック(SP=0xA000)。コード無し。512B スタック問題はここで解消。
    block2-7(0xA000-0xFFFF, 24KB): プロセス枠 6 個。
    ※ プロセス枠数は現行 6 のまま(block1 は元々 PID_DRIVER で枠外)。この規模で
      8 ブロックを使い切る場面は起きないので、8KB をワークに寄せても実害無し。
      ただし 8KB を「めいっぱい機能で埋める」と最小機で不足に戻る → 空きは
      「潤沢機での上振れ余白」に留める。

  --- arch パラメータにする点 ---
    1. ベクタ表ベース : 0x0038〜(ROM 制御機)/ システムワーク RAM 内固定番地
       (低位が他人の ROM の PC-8001 等。Z80 の RST 0x38 が書けない)
    2. ブロックサイズ : 4KB 既定 / 16KB 機は 2-3KB
    3. crt0 バンク切替 : 8801・8001mkII は RAM をかぶせる / stock 8001 は無し
    4. console / storage / FS backend : 上表
    5. sh / init : 常駐(最小機・枠節約)or プロセス(潤沢機)。
       ※ sh をプロセスにすると sh+コマンドで最低 2 枠 → 枠が最も少ない機で枠を
         食う逆説。最小機は sh 常駐に倒す。init/getty も最小機は kernel_init →
         直接 sh 起動。PID 1 / getty respawn は潤沢機の purity。

  --- 「FatFs をカーネル ROM に載せて将来の足かせにならない」条件 ---
    A〜E が揃えば FatFs-in-ROM は「潤沢ターゲットの backend 選択」でしかなく、
    非ウイルス性(PC-8001 / MSX は FatFs を運ばない)・可逆(compact-FAT へ
    ドロップイン)・プロセス化を妨げない(IPC 境界は VFS レベル)・バージョン
    結合が消える(1 像に畳む)。
      A. カーネル中核が f_* を直接呼ばない。FS アクセスは全部 VFS backend ベクタ
         経由(VFS 一本化 Step 11+、fatcmd frontend を backend 内側へ)
      B. backend 界面を「VFS 操作 ⇄ /dev/fda block 操作」で定義。
         FatFs / compact-FAT / RAM ディスク / N88 / host シムが drop-in 交換可能
      C. /dev/fda block device 層。FS backend は diskio / ハードを直に触らない
         (block スペシャルファイルのクライアント)
      D. ビルドで FatFs は条件リンクのモジュール(ARCH_* が backend を選ぶ)。
         常時 KOBJ ではない
      E. コマンド⇄カーネル ABI(stdio / FS ベクタ)を arch パラメータの番地に。
         0x0038 固定にしない
    残るコスト(足かせではなく天井):
      bare-metal 基板 / cpmsim の 32KB ROM 像で 中核 ~6K + stdio ~2K +
      backend(SPI+FatFs ~20K)≈ ~28K、残り ~4K。将来のカーネル成長には狭い。
      → RAM 潤沢機には天井無し。詰まったら compact-FAT が圧力弁(界面 B で
        差し替えるだけ)。config 削り + 外部化で ~2-4K の余地。

  --- 残タスク ---
    [P1] VFS 一本化を完了(条件 A):中核から f_* 直呼びを排除。fatcmd frontend を
         backend 境界の内側へ。誰も ff.c を直接参照しない状態にする。
         └ x86 syscall 経路は達成: sys_call 入口に vfs_resolve(ecc8081)、
           /dev/null(bc88ff0)、sysfile.c を fsb_* 界面へ張り替え・FIL/DIR 表を
           fsbackend_fat.c へ移動・ff.h include も除去(67545d2)。
           残: src/fatcmd.c(z80/x86 共有。ls/df/redir/kdir/kfs がまだ f_* 直呼び)、
           z80 の user/driver.c、arch/x86-ia16/kmain 系。
    [P1] backend 界面の型を定義(条件 B):VFS 操作 ⇄ block 操作。FatFs を最初の
         実装として界面の裏に収める。
         └ 済: src/fsbackend.h(fsb_* / blk_* 宣言、deadf6c)+ src/fsbackend_fat.c
           (fsb_* の FAT 実装、77ec25c)。blk_* はまだ(ff.c が disk_* 直呼び中)。
    [P1] /dev/fda を vtree に追加 + raw block backend(条件 C、~300-600B)。
         ff.c の disk_read / disk_write を blk_* → /dev/fda 経由に
         (「di 中の FatFs 禁止」規約は維持)。
    [P2] Makefile: KOBJ を core / backend に分割、backend を ARCH_* で条件リンク
         (条件 D)。
         └ 進行中: x86 Makefile を SOBJ(中核)/ FSBACKEND(fatcmd+ff)に分離
           済(f5861b6、バイナリ不変)。残: 条件リンク化、z80pack 側の分割。
    [--] x86 既知の穴: kill の下限 arch 別化(x86=slot1..7)完了(c65b102)。
    [P2] ベクタ表ベースを arch.mk 変数化(条件 E)。commands のリンクを追随。
    [P2] block1 廃止:kload_driver / driver.bin 削除、drvvec.s 撤去、stdio を
         カーネル像へ畳み込み低位ベクタで公開。kmem.h を block0+1=8KB ワーク /
         block2-7=6 枠に改訂。DRIVER_BLOCK / PID_DRIVER 除去。SP 起点 0xA000。
    [P2] 「DRIVER」の CP/M-BIOS 由来の誤称を撤去。実体はコマンドランタイム。
    [P3] ARCH_PC8001 フレーバ:テープローダ(N-BASIC CMT trampoline)+ RAM ディスク
         FS backend + アーカイブ boot。
    [P3] arch パラメータ化:ブロックサイズ、crt0 バンク切替(8801 / 8001mkII)。
    [P3] PC-8801 arch:サブ CPU FIFO ディスク or N88-DISK trampoline、ROM バンクアウト
         crt0、直 VRAM or ROM コンソール。retro リファレンスとして優先。
    [defer] compact-FAT 自作(read → write-in-place → grow → create/delete →
            rename → block 常駐化)。難所はアルゴリズムではなく iy_reg の掟税 +
            デバッガ無し cpmsim デバッグループ。bare-metal ROM 圧力が要求したとき、
            または SD 段合流時に着手。界面 B があれば FatFs からドロップイン置換。
    [note] FatFs-in-ROM は上記 A〜E 成立後なら安全。A〜E を後回しにして f_* 直呼び
           が中核に残ったまま焼き込むのだけが足かせになる。


==== ネットワーク仕様(方針メモ) ====

  ・TCP スタック / WiFi スタックは tizix 側に持たない。W5500・ESP32 など
    「相手側」にスタックを持たせ、tizix は SPI ベースでコマンド/データを
    やりとりするだけ(ESP32 は "UART/SPI の延長" 扱い)。
  ・名前空間: /dev/net/tcp を通してイーサネット/WiFi を扱う。
    /dev/net/tcp/<ホスト名 or IP アドレス:ポート番号>  をファイルとして open。
      例) /dev/net/tcp/example.com:80
  ・socket() でも puts()/fread() でも扱えるものとする。socket は UNIX 風の
    体裁を与えるだけで、実体は puts/read と同じ(open した fd に書く=送信、
    読む=受信)。接続確立・名前解決・再送は W5500/ESP32 側の仕事。
  ・devfs (docs/DEVICE_DESIGN.md) の 1 デバイスとして /dev/net を足す。
    実データは共有バッファ、制御はドアベル/syscall。カーネルは中継のみ。


==== z80board: ESP-WROOM-02 を SPI bit-bang で繋ぐ WiFi (#62, 2026-09-21) ====

上の方針(「スタックは相手側に持たせ、tizix は SPI でコマンド/データを渡す」)の
最初の実装。**z80pack で動いていたネットワーク一式(net.bin 常駐 + netcli.h)を
そのまま実機へ持っていく**のが狙いで、telnet / tzftp / atcli / ntpdate / nettest は
1 行も変えずに動く(共有リング KW_NET* の使い方が同じ)。

  層(下から):
    arch/z80board/espspi.s   … bit-bang SPI 物理層。SD(spi.s / port 0x80)と
                               同じビット割り当ての **2 本目のチャネル**。
                               手書き PIC(iy_reg を通さない)。
    user/espat.h             … SPI フレーミングと ESP 側ファームとの取り決め。
    user/netesp.c            … ESP-AT を喋る常駐デーモン。**ビルド名は net.bin**
                               (user/Makefile が ARCH=z80board のとき net.c の
                               代わりにこちらをリンクする)。
    user/netcli.c            … 既存のまま。リングの読み書きだけ。
    user/wifi.c              … AT コマンドを 1 行通す小物(z80board のみ)。

  I/O ポート: hw.h の ESP_PORT。**暫定 0x81**(「SD の次」)。
    現物の 74HC138 は A4〜A6 だけを見るので 0x81 は 0x80(SD)と同じ選択線に
    なる ── 実機では空いているデコード番号へ振り直すこと。変更点は
    hw.h / espspi.s(ESP_IO_PORT)/ z80boardsim の iosim.c の 3 箇所。
    OUT: bit7 = MOSI / bit6 = CS(1 = 非選択)/ bit5 = SCK / **bit0 = ~RST**
    IN : bit7 = MISO

  なぜ ESP 側にも自前ファームが要るか: ESP8266(ESP-WROOM-02)の純正 ESP-AT は
  **UART 専用**で、SPI/SDIO の AT インタフェースは ESP32 系しか持たない。
  そこで arch/z80board/esp/tzesp_at/ に「SPI スレーブ ⇔ ESP-AT 互換の
  コマンド解釈 + 実 TCP」のスケッチを置いた。喋る言葉を純正 ESP-AT の
  サブセットに揃えてあるので、**python/at_modem.py(既存のテストベッド)が
  そのまま ESP の代役になる**。

  シミュレータ: z80boardsim(fork 側 srcsim/iosim.c)に port 0x81 の SPI
  スレーブ模擬を足した。受け取ったバイト列を TCP で at_modem.py へ中継し、
  返りを SPI で返すだけ(AT の解釈はしない)。接続先は環境変数 TZESP_MODEM
  (既定 127.0.0.1:8080)。回帰は python/test_esp_net.py:
      1. `net &` で ESP とリンク(AT / ATE0 / AT+CIPMUX=0)
      2. `wifi AT+GMR` で AT パススルー
      3. `telnet 127.0.0.1 9100` で +IPD 受信と AT+CIPSEND 送信

  ★ESP8266 のブートストラップ: HSPI スレーブの CS は **GPIO15 固定**で、
    GPIO15 は起動時に Low でないと ESP が起動しない。Z80 側のラッチは
    CS = High で待機するため、**ラッチの bit0 を ESP の ~RST に配線**して
    Z80 からリセットを握れるようにした(netesp.c の esp_boot が「CS Low →
    ~RST Low → ~RST High」で起こす)。未配線でも bit0 は誰も見ないだけ。

  ★レベル変換(2026-09-21 TK 決定): z80board は 5V 系、ESP は 3.3V 系。
    **2 階建ての子基板を起こし、そこにレベル変換 IC を載せる**。

    - **SD の方式は流用しない。** z80board の SD は 100Ω 直列 + カード内蔵抵抗の
      「分圧風」だが、実体は分圧ではなく**カード内部の保護ダイオードでクランプ
      させて 100Ω で電流を制限している**状態(≒10mA。74HC574 の定格 ±6mA も
      超えるので出力も垂れる)。SD は耐えるし壊れても差し替えられるので実用上は
      通っているが、ESP8266 は保護ダイオードの許容電流がデータシートに規定されて
      おらず、注入先が 3.3V レールなので寄生給電・ブラウンアウト・ラッチアップに
      直結する(壊れ方が「WiFi だけ不安定」になりソフトのバグと区別できない)。
    - 方向固定のバッファで組む(**自動方向判定の TXS0108 等は SPI に使わない**
      ── TXS はオープンドレイン/I2C 用。TXB 系もドライブが弱く配線容量で転ける)。
      TK 選定: **5V トレラント系の 74541**(74LVC541A)。基板上の 74HC541 と
      ピン配置が揃う。
        Z80 バス →[74HC574 @5V (port 0x81 ラッチ)]→ 4 本 →[74LVC541 @3.3V]→ ESP
        ESP MISO →[74HCT541 @5V, ~OE = IN(0x81)]→ Z80 バス D7
      ・LVC541 は **VCC 最大 3.6V。5V では使えない**ので「LVC541 を 2 個で
        両方向」はできない。
      ・**MISO 方向にレベル変換は要らない**(2026-09-21 TK 指摘。当初 AI が
        「変換用に HCT が要る」と書いたのは誤り)。そこに要るのは元々必要な
        「バスに出すための 3ステートバッファ」で、選択は HC か HCT かだけ:
        74HC541 @5V は VIH=0.7×VCC=3.5V 必要 vs ESP の VOH ≒3.2V で**規格上は
        割れている**が、同じ 3.3V の SD カードがこの基板で現に動いている
        (HC の実しきい値は typ 2.5V 付近)。74HCT541 なら VIH=2.0V で規格内。
        新規に買うならタダの保険で HCT、という程度の差。
      ・LVC541 の ~OE はバスに出ないので Low 固定でよい。
      ・★MISO を **既存(SD 用)541 の空きビットに相乗り**させる場合は、
        読み出しポート/ビットが書き込みポートと別になる。espspi.s は
        `out (c),a` / `in a,(c)` と**同じポートの bit7 を読む**前提なので
        2 行直す必要がある(`in` の対象ポートとシフト位置)。ESP 用に入力
        バッファを新設して 0x81 で読むなら現状のまま通る。
    - **その場合 GPIO15 の 10k プルダウンは勝てない。** LVC541 が能動駆動する
      ため、電源投入時のラッチ不定値がそのまま CS に出る = **ブートストラップの
      保証は ~RST(ラッチ bit0)一本**になる。~RST は必ず配線すること。
      プルダウン自体は ~OE 未実装で試すとき用に残しておくと楽。
    - ESP は WiFi 送信時に瞬間 300mA 級を引く。**モジュール直近に 470µF + 0.1µF**。
      3.3V レギュレータが SD 用に細いとここでブラウンアウトする。
    - (レベル変換 IC が SOIC で載っているのは m68k-mega 実機の方。別基板の話。)

    抵抗分圧(1k 直列 + 2k プルダウン = High 3.33V / 等価 667Ω)でも電気的には
    成立する ── 子基板を起こさない場合の代案としてここに残しておく。

  ★タイムアウトの取り方: netesp.c の待ちループは「反復回数」と「tick」の
    **両方**を超えたときだけ諦める(= 実質 max(反復, tick))。z80board の TICKS は
    素直な 1/100 秒ではない: GP5 のハードウェアタイマは実機で出ている
    (2026-09-21 TK 確認)が周期は ≒977/1953Hz 系で TICK_HZ=100 と一致しておらず、
    さらに getticks() 自身が KYIELD を踏むので **呼ぶたびに +1** される
    (src/kernel.c)。tick だけを見ると実時間では見込みより早く諦めて WiFi の
    接続(DNS + TCP)に足りず、反復回数だけを見ると CPU が無限速のシミュレータで
    一瞬で諦める。

  未確認(実機 ESP が手元に無いため): SPI スレーブのコマンド値(0x01-0x04)と
  読み出し時の DUMMY 1 バイト、status 4 バイトのバイト順。前者は ESP8266 core
  の hspi_slave.c 実装に合わせたもので、合わなければ espat.h / iosim.c /
  .ino の 3 箇所を揃えて直す。後者は Z80 側がマジック 0x5A を先頭・末尾の
  両方で探して吸収する。

  スコープ外(z80pack 版と同じ): 同時 1 接続のみ、着信なし、UDP なし。


==== スコープ方針 ====

  ・メモリ保護は対象外(MMU 無し環境が前提。最初から諦めている)。
  ・malloc / ヒープは載せない(レトロ PC には荷が重い。変数確保はポインタで
    プログラマの仕事)。
  ・プロセス・スケジューリング制御(優先度 / wait キュー等)は今後の設計テーマ。
    SD ドライバプロセス化で wait/wake が必要になり自然に入る。
    前景 kill は暫定版(Ctrl+C → pid_tbl クリア)で実装済み。本物の block/wake は SD 段。
  ・ウォッチドッグ(ソフト実装 or 外部回路対応)は将来。


==== ソース配置(詳細は memory: arch-split-layout)====

  src/            arch 非依存カーネル(init/kernel/sh/builtin/kexec/ff/fatcmd/vfs/io
                  + ヘッダ、ivthelpers.c)。arch 差は当面 #if defined(ARCH_*)。
  arch/z80pack/   cpmsim(FDC。boot.s/crt0.s/diskio.c/console.c/arch.mk、disks/、cpmsim)
  arch/z80board/  実機 Z80(Z84C000+FT245RL+SD/bit-bang SPI。crt0.s は ROM 版
                  IVT(z80pack のような実行時自己書き換えではなくリンク時 .org
                  で焼く)。diskio.c は spi.s+sdcard.s 経由で SD カード。
                  実機 SD 動作は未検証。2026-09-12 キャッチアップ)
  arch/x86-ia16/  8086 リアルモード(boot=stage1+stage2、crt0.s、bios13.s、
                  proc.s、diskio.c、console.c、libc.c、libgcc.c、user/(C コマンド))
  libivt/         → src/ivthelpers.c へ統合済(空)
  python/         テストハーネス(tzpaths.py で cpmsim/ディスクのパス一元化)

  z80(iy_reg 変換を受ける user/*.c、手書き PIC の lstr.s/lstd.s、常駐 driver.c、
  --reserve-regs-iy のカーネル本体)の詳細は「IY 相対 PIC 変換」節と各ファイル冒頭。


==== FatFs 統合とトラブルシューティング(元 bk/README-bk-ignore.md) ====

## 検証済み事実
- FatFs R0.16 (ChaN) を sdcc-z80 でコンパイル成功。**コード ~9.7KB / RAM ~0.7KB**。
  現カーネル ~3.3KB と合わせ ~13KB（ROM 32KB に収まる）。
- ffconf.h は最小構成: read-only / FAT12 / FF_FS_MINIMIZE=2 / LFN無し / FF_FS_TINY=1 /
  512B セクタ / 1 volume。
- diskio.c は cpmsim FDC(ポート10-16, boot.s と同じ)で **drive B(FDCD=1)** を読む。
  FatFs 512B セクタ = 4×128B 物理セクタ。lin=LBA*4+i, track=lin/26, sect=lin%26+1。
- ホスト検証: driveb.dsk(FAT12) から HELLO.BIN を f_open/f_read → 実 hello.bin と**バイト一致**。

## ファイル
- ff.c ff.h diskio.h … FatFs R0.16 stock(無改変)。
- ffconf.h            … tizix 用最小構成(このディレクトリのもの)。
- diskio.c            … cpmsim FDC 版(tizix 用、新規)。
- driveb.dsk          … FAT12 サンプル(HELLO/A/B.BIN 入り)。disks/ に置く。
- mkfatdisk.sh        … driveb.dsk を作り直す/ファイルを入れるスクリプト(mtools)。

## ディスクのメンテ(ホスト)
    ./mkfatdisk.sh          # user/*.bin を FAT12 に入れて disks/driveb.dsk 生成
  個別に入れるなら mtools:
    export MTOOLS_SKIP_CHECK=1
    mcopy -o -i fatimg.raw user/foo.bin ::FOO.BIN

## 依頼者のメモ: オペコードエラーとアドレスパッチについて
`vi.c` の実行時にオペコードエラー（例: `Op-code trap at a063 ed 00`）が発生する原因と、`crt0cmd.s` のアドレスパッチとの関係について。

### 1. アドレスパッチ（再配置）の処理場所と役割
* **アドレスパッチを実行しているのは `kexec.c` (`kexec_file`)**:
  `crt0cmd.s` 自身がアドレスパッチを行っているわけではなく、カーネルの `kexec_file()` が FAT からバイナリを空きブロック（例: `0xA000`）にロードした直後、先頭ヘッダの再配置テーブル情報を基にバイナリ内の絶対アドレスにベースアドレス（`0xA000`）を加算（パッチ）しています。
* **`crt0cmd.s` の役割**:
  `base + 0x20` から実行開始し、`iy`（ベースアドレス）からスタックや `argv` を設定して `call _main` を行います。

### 2. なぜオペコードエラー（`a063 ed 00` 等）が起きるのか
オペコードエラーが発生する主な原因は以下のメカニズムです：

#### ① 古いバイナリ / レガシー再配置（0x29 単一パッチ）とのミスマッチ
* `crt0cmd.s` のコード長が変更された場合、`call _main` の即値オペランドの位置が `0x29` ではなくなります。
* もしバイナリが `mkreloc.py` でテーブル化された新しい `.bin` ではなく、古いビルドやテーブル無しの状態で `kexec.c` に渡されると、フォールバックとして固定オフセット `0x29` にベースアドレスが加算されてしまいます。
* これにより `0x29` にある本来の命令コード（オペコード）が壊され、壊れた命令を実行した結果として `a063` 等の不正なアドレスへジャンプし、未定義命令 `ED 00` で **Op-code trap** になります。

#### ② `make` のタイムスタンプ（mtime）判定による再ビルド漏れ
* ソースコード（`vi.c` や `crt0cmd.s`）を修正しても、ディスク上の `.bin` のタイムスタンプが新しかった場合、`make` がスキップして古いバイナリが FAT イメージに残ってしまう現象が発生します。

#### ③ SDCC が生成する未定義命令・関数ポインタテーブルの不整合
* SDCC が関数ポインタ呼び出しで出力するヘルパー（`___sdcc_call_hl`）が正しくリンクされていない、あるいは `drv_tbl` のインデックスやシグネチャが `driver.c` とずれていると、関数ポインタ呼び出し時に暴走してトラップします。

### 3. 解決策 / 対処手順
1. **完全クリーン再ビルド＆ディスク再インストール**:
   `user/` ディレクトリで `reinstall` を実行し、古い中間生成物や `.bin` を完全に消去して再配置テーブル付きの最新バイナリを FAT イメージに書き込みます。
   ```bash
   cd user
   make reinstall
   ```
2. **バイナリの再配置整合性チェック**:
   `checkvi.py` や `verifybin.py` を用いて、生成された `vi.bin` の再配置テーブルが正しくヘッダに埋め込まれているか検証できます。
   ```bash
   python3 user/checkvi.py user/vi.bin
   ```
3. **`crt0cmd.s` のエントリ周り**:


==== IYレジスタ方式の確定仕様と技術的考察(元 bk/riy_reg.md) ====

本ドキュメントは、tizix の当初設計である「**各プロセスに IY レジスタでベースアドレスを保持させ、実行時に動的アドレス解決を行う方式**」に基づき、ジャンプ・コール命令の技術的仕様をまとめたものである。

## 1. ユーザー空間バイナリにおけるジャンプ・コールの分類
SDCC (`sdcc -mz80 --reserve-regs-iy --sdcccall 0`) が出力する Z80 コード中のジャンプ・コールは、以下の4種類に大別される。

| 分類 | 命令パターン | 対象 | 実行時アドレスの性質 | IY 方式での対応方針 |
| :--- | :--- | :--- | :--- | :--- |
| **① 固定番地カーネル/スタブ** | `call _kexit`, `call _getticks`, `call ___sdcc_call_hl` 等 | カーネル 0x0000 空間の固定ベクタ | 固定絶対番地 | **そのまま維持** |
| **② 標準ドライバ間接コール** | `ld bc, (#_drv_tbl+N)...` | `DRIVER.BIN` の関数テーブル | 共有ドライバ固定番地 | **そのまま維持** |
| **③ 内部関数コール** | `call _func` | 同一バイナリ内の関数 | 0x0000 基準オフセット | **IY 相対間接コールへ変換** |
| **④ ローカルジャンプ** | `jr/jp` | 同一バイナリ内のラベル | 0x0000 基準オフセット | **「補正後の実距離」で判定** |

## 2. IYレジスタ方式における確定仕様・コード生成パターン

### 2.1 無条件 IY 相対ジャンプ（スタックレス・レジスタ完全保護型）
レジスタ・フラグ・スタック深さを一切破壊せずにジャンプする：

```z80
    ex   af, af'        ; 1. 表 AF を裏へ安全退避
    push hl             ; 2. 表 HL をスタックへ一時退避
    push de             ; 3. 表 DE をスタックへ一時退避
    ld   hl, #target    ; 4. ジャンプ先オフセットをロード (0x0000基準)
    push iy             ; 5. ベースアドレス (IY) を取得
    pop  de             ; 
    add  hl, de         ; 6. HL = target 実アドレス (IY + offset)
    pop  de             ; 7. 表 DE を完全復元
    ex   (sp), hl       ; 8. スタックトップの「旧HL」と「新PC」を交換
    ex   af, af'        ; 9. 表 AF を完全復元
    ret                 ; 10. スタックトップの新PCへジャンプ
```

### 2.2 条件付き IY 相対ジャンプ (`jp cond, target`)
条件が成立したときのみ上記 2.1 の無条件ジャンプを実行する：

```z80
    jr   inv_cond, _skip_jump   ; 反対条件なら直後へスキップ
    ex   af, af'
    push hl
    push de
    ld   hl, #target
    push iy
    pop  de
    add  hl, de
    pop  de
    ex   (sp), hl
    ex   af, af'
    ret
_skip_jump:
```

### 2.3 内部関数の IY 相対間接コール (`call _func`)
内部関数の呼び出しでは `call ___sdcc_call_hl` (0x0050: `jp (hl)`) を利用する：

```z80
    push de
    ld   hl, #_func     ; 内部関数オフセット
    push iy
    pop  de
    add  hl, de         ; HL = 内部関数の実アドレス
    pop  de
    call ___sdcc_call_hl ; 0x0050: jp (hl)
```

## 3. 実機検証における教訓

1. **スタック不整合の回避**: 間接ジャンプでスタックを壊すとカーネルまで巻き込んで暴走・リブートするため、上記の保護方式は厳守。
2. **レジスタ破壊の回避**: 引数・戻り値（HL/DE/BC）が入っている状態で IY 加算を行うと暴走するため、必ず保護してからレジスタ操作を行う。
3. **検証**: Python エミュレータだけでなく、必ず z80pack 実機バイナリ実行での挙動検証を行う。

   `mkreloc.py` による差分リンク方式でビルドされた `.bin` であれば、`crt0cmd.s` 内の `call _main` やその他の内部参照オフセットは自動的にテーブルに抽出され、`kexec.c` で正しくパッチされます。

---

## 4. 2026-09-14 セッション引き継ぎ(z80board 実機ブリングアップ、続き)

**このセクションは次のセッション開始時に必ず読むこと**(4.1相当の前回分は
このセッションで解決/上書き済みのため削除した)。

### 4.1 今回やったこと(commit 予定。console.c は前回分を維持)
- `arch/z80board/console.c`: 前回の「ステータス待ち削除」版のまま維持で
  確定(ユーザー確認済み。「だいぶ勘違い」は console.c 自体の話ではなく、
  UART ブリングアップの flush 方式の理解違いだった)。
- `arch/z80board/boot.s`: zasm 方言 → sdasz80 方言へ全面書き換え
  (`equ`→`=`、即値は `#` 必須、`.rept`/`.endm`(ドット必須)、
  `n(ix)` 形式のインデックス、文字列は `.ascii`+`.db`、ファイル全体を
  単一の `.area _HEADER (ABS)` に配置)。`#include "spi.s"/"sdcard.s"` は
  廃止し、カーネルと同じく `spi.rel`/`sdcard.rel` を別リンクする方式に統一
  (z80pack が基準、という方針に合わせた)。UART(FT245RL)ブリングアップ
  フラッシュ(改行40個+ESC画面クリア)を追加。
- `arch/z80board/Makefile`: `boot` ターゲットを修正
  (`sdasz80 --abs` は未知オプションとして無視されるだけで効いていなかった
  のが本当の原因だった。今は `.area (ABS)` 宣言で対応。`spi.rel`/`sdcard.rel`
  を `--code-loc 0x5000` でリンクに追加。出力ファイル名を `monitor.rom` に
  改名 ── 以前はカーネル本体の `$(DISK)=boot.rom` と同名で、`make boot`
  実行時に本物のカーネル ROM を上書きしてしまうバグがあったため分離)。
- `arch/z80board/crt0.s`: `start:` の先頭に boot.s のブリングアップ
  シーケンス(flush→実時間ウェイト付きカウントダウン→画面クリア→
  `BOOTING NOW !`→`SYSTEM STATUS CODE [XX]`→`LOAD MBR...`)をほぼそのまま
  移植。SD read/hex_dump/ブートストラップへのジャンプは kernel に不要なので
  含めていない。

### 4.2 実機での検証結果
- `arch/z80board/boot.s`(単体モニタ、`monitor.rom`): UART 出力・SPI/SD
  読み出し(セクタ0の MBR)ともに実機で成功を確認。
- kernel(`boot.rom`、crt0.s にブリングアップ移植後): 実機で
  `BOOTING NOW !` → `SYSTEM STATUS CODE [1F]` → `LOAD MBR...` →
  `FAT Drive ...... DETECTED` まで到達を確認。**`System Driver .. FAILED`
  で停止**(`src/kexec.c` の `kload_driver()` が `/bin/driver.bin`
  (4096B=8セクタ)の読み込みに失敗して `0xFF` を返している)。
  boot.s で確認できていたのはセクタ0の単発読み出しのみなので、
  **複数セクタにまたがる SD 読み出しでのみ問題が起きている可能性が高い**。
  次のセッションはここから着手。
- エミュレータ(`make ARCH=z80board run`、z80boardsim)では上記の
  kernel ビルドが安定して(10回試行して10回とも)`[/root]#` まで完走する
  ことを確認済み。つまり `System Driver .. FAILED` は実機固有の問題で、
  z80boardsim の仮想 SD では再現しない。

### 4.3 未解決のまま持ち越し(今回着手せず)
- **z80boardsim のコンソール実装が根本的に甘い**: `iosim.c` の
  `p001_in`/`p001_out` が単純な `getchar()`/`putchar()` の直接呼び出しで、
  z80pack 本家(cpmsim)のような非ブロッキング+SIGIO 前提の実装になって
  いない。ブロッキング中は Z80 命令ループごと止まるため、SIGALRM ベースの
  疑似 IM1 割り込みが「呼ばれたら1 tick」式に来ていても反映されない瞬間が
  生じ、非決定的な動作(FAT 検出が回によって成功/失敗する等)につながって
  いる。ユーザー報告の「バックスペースが押しっぱなしに見える」「Ctrl-] で
  終了しようとすると暴走する」もこれが原因の可能性が高いが未着手。
  cpmsim の該当実装を移植するのが筋(z80pack が基準、の方針に沿う)。
- `arch/z80board/console.c` の TXE#/RXF# ステータス待ち削除は据え置き
  (bios.s と同じ無条件 OUT/IN 方式で確定)。

### 4.4 環境メモ
- rocky9 への SSH: `ssh -i <秘密鍵> <ユーザー>@<ビルドホスト>`
  (Windows Git Bash から)。
- `\\rocky9\tk\z80pack\tizix`(Windows から SMB 経由、編集用)と
  `~/z80pack/tizix`(rocky9 上、`make`/実行用)は同一ファイル
  システム。編集は SMB 側、ビルド確認は SSH 側、という使い分けで進めた。
- **`tizix-z80board` は古いフォークで作業対象ではない**。作業は必ず
  `tizix` 側で行うこと。
- ビルドツールチェイン: `export PATH=~/z80pack/sdcc/4.5.0/bin:$PATH`
  が必要(rocky9 のデフォルト PATH には無い)。
- テストは必ず実際に指示されたコマンド(`make ARCH=z80board run` 等)で
  行うこと。手早く済ませるための代替コマンド(バイナリ直接実行等)で
  代用しない。

### 4.5 2026-09-17 `System Driver .. FAILED` 切り分け用デバッグログ追加

- **背景**: 4.2 で判明した「`FAT Drive ...... DETECTED` の後
  `System Driver .. FAILED` で停止する」問題(実機のみ再現、複数セクタに
  またがる SD 読み出しが疑わしい)を切り分けるため、ログを仕込んだ。
- **ROM は 32KB ハード上限で空きが数百B しか無い**(この時点の通常ビルドで
  free 242B)ため、ログは `SDDBG=1` を渡したときだけ `-DSD_DEBUG` が付く
  条件コンパイルにしてある(通常ビルドはバイト単位で無変更)。
  デバッグビルドは以下:
  ```
  make ARCH=z80board SDDBG=1 clean
  make ARCH=z80board SDDBG=1 boot.rom
  make ARCH=z80board SDDBG=1 sdcard.img
  ```
  (free 98B まで消費するので、これ以上ログや機能を同時に足さないこと。
  診断が終わったら通常ビルド `make ARCH=z80board boot.rom`(SDDBG 無し)に
  戻すこと。)
- **仕込んだ場所**:
  - `arch/z80board/sdcard.s` `sd_data_read`: R1 エラー時に定数 `1` で
    潰していたのをやめ、実際の R1 応答バイト(0xFF=無応答タイムアウト、
    それ以外はビットパターン)をそのまま返すよう修正(副作用でコードは
    2B 縮んだ)。
  - `arch/z80board/diskio.c` `disk_read()`: ループの毎回、`RD lba=%u r=%u`
    を出力(r=0 成功、非0 は上記の実 R1 値)。
  - `src/kexec.c` `kload_driver()`(`ARCH_Z80BOARD && SD_DEBUG` 限定):
    `KL open=%u sz=%u`(f_open の結果とファイルサイズ)、
    `KL read br=%u`(f_read が実際に読めたバイト数)。
- **エミュレータでの検証結果(正常系、参考ログ)**:
  ```
  RD lba=0 r=0
  FAT Drive ...... DETECTED
  RD lba=13 r=0
  RD lba=45 r=0
  KL open=0 sz=4096
  RD lba=109 r=0 ... RD lba=116 r=0  (間に RD lba=1 = FAT テーブル参照)
  KL read br=4096
  System Driver .. LOADED
  ```
  8 セクタ(4096B/512B)ぶんの `RD lba=` が連番で見え、`br=4096` で完了。
  これが実機でどこで途切れる/どの `lba` で `r` が非0になるかを見れば、
  「特定セクタで失敗」なのか「複数セクタ連続読み出し特有」なのか
  「CMD17 トランザクション自体が途中から応答しなくなる」のかを切り分け
  られる。
- **次のセッション**: 実機に `SDDBG=1` ビルドを焼いて UART ログを採取し、
  `RD lba=` が途切れる位置と `r=` の値(R1 エラービットか 0xFF タイムアウト
  か)を確認するところから。

### 4.6 2026-09-18 実機ブリングアップ(SD 決着 → 割り込みへ)。**中断時点の状態**

**このセクションが最新。次のセッションはここから読むこと。**

#### 4.6.1 解決したこと(実機で確認済み)

1. **`System Driver .. FAILED` の真因 = SD のアドレス指定方式の取り違え**
   実機ログで `RD lba=0 r=0` は成功するのに `RD lba=13 r=32` で必ず失敗
   (32 = 0x20 = R1 の Address Error ビット)。原因は、このボードの SD が
   **2GB = SDSC(標準容量)でバイトアドレス指定が必要**なのに、ドライバが
   ACMD41 を HCS=1 で送っているだけでカード種別を一度も確認せず、
   ブロック番号をそのまま CMD17 の引数にしていたこと。lba=0 だけは
   「ブロック 0 = バイト 0」で両方式が一致するため偶然成功し、
   それ以外が全滅していた。
   **この前提は元祖の Arduino 参考実装 `~/z80pack/bios/bk/arduino_si_sdcard.txt`
   に `// SDはアドレス番号、SDHCはセクタ位置を指定する。2GB SDなのでアドレス指定`
   と明記されていた**が、`bios/sdcard.s` → tizix への移植過程で失われていた
   (移植版は単発のセクタ 0 読みしか検証していなかったので露見しなかった)。
   → `sdcard.s` に `sd_cmd58`(READ_OCR)を追加し、OCR bit30(CCS)で
   実カード種別を判定。SDSC なら `_disk_raw_rw32` が送信直前にブロック番号を
   ×512(9bit 左シフト)してバイトアドレスへ変換する。
2. **`sd_data_read` のデータトークン待ちが無限ループだった**
   CMD17 の R1 が成功しても、データトークン(0xFE)が来ないと**タイムアウトが
   無いため永久に戻らず、ログすら出ないまま停止**していた(sh.bin ロード中に
   `RD lba=214` で無言停止した症状の正体)。タイムアウトを入れ、0xFD を返す
   ようにした。
3. **`sd_data_read` の R1 エラー値が定数 1 で潰されていた**のをやめ、実際の
   R1 応答バイトをそのまま返すよう変更(上記 1 の切り分けはこれで判明した)。

この 3 点で **SD 経路は完全に通った**。実機ログ:
```
[i1][k1] RD lba=13,45,46 [k2 sz=6765] [k3 blk=2 n=2]
RD lba=201..214 (全て r=0)
[k4 br=6733][k5 sp=49138 pc=40992][k6][i2 n=2][i3 pid=2]
```
sh.bin 6765B が 1 バイト欠けず block2(base=0xA000)へロードされ、偽コンテキスト
(sp=0xBFF2 / pc=0xA020=base+0x20)も正値、PCB 登録まで完了している。

#### 4.6.2 いま詰まっている所: **割り込みが一度も開いていなかった**

上記の通りロードは完璧なのに `[i3 pid=2]` の後 sh が走らない。原因は
**`ei` がどこでも実行されていないこと**:
- `crt0.s` は `start:` で `di` し、以降 `ei` を撃たない
- 唯一の `ei` は**スケジューラが既存プロセスへ復帰する経路の中**にある
  ── そこへ入るには割り込みが要る、という卵と鶏
- `io.c` の `IRQ_ON()` は `kgetchar`/`con_break` の中だけで、init の待ちループ
  `while (pid[n] != 0);` はどちらも呼ばない

→ タイマ割り込みが永久に来ず、スケジューラが起動せず、子(sh)に CPU が
一度も渡らない。**エミュレータ(cpmsim/z80boardsim)は IFF を無視して割り込みを
注入するため露見しない**(ISR プローブで、`IRQ_ON()` 到達前から `*0` が出るのを
確認済み)。**`arch/z80pack` も同じ穴を持っている**(cpmsim が寛容なだけ)。

対処として入れたもの:
- `src/init.c`: sh ロード完了直後に `IRQ_ON()`。SD(bit-bang SPI)経路は実機で
  通った `di` のままの状態を保つため、開けるのは意図的にここまで遅らせている
- `arch/z80board/crt0.s`: `im 1` を明示(リセット直後は IM0 で、データバス
  未駆動の 0xFF = RST 38h がたまたま 0x0038 へ飛んでいただけ)

**その結果、実機は今度は "halt"(完全停止に見える)状態になった。ここで中断。**

#### 4.6.3 次にやること(最優先)

焼くべき ROM は**ビルド済み**(`arch/z80board/boot.rom`、空き 771B)。
ISR に判定用プローブが入っているので、実機に焼いて `[i3 pid=2]` の後の
見え方を見るだけで次の一手が一意に決まる:

| 見え方 | 意味 | 対処 |
|---|---|---|
| `*` が延々流れ続ける | **割り込みストーム**。/INT がクリアされていない | ISR で FT245 を読む(下記) |
| `*0` は出るが `*2` が出ない | スケジューラが block2 を選べていない | pid_tbl / sched_pick を見る |
| `*2` の後に無音 | sh は走り出したが即死 | sh 本体 / DRIVER 側 |
| `*0` すら出ない | 割り込みが物理的に来ていない | タイマ配線 / PIC 側 |

**ストームが本命の仮説**。理由: 実機の `/INT` は FT245 `~RXF` とタイマ
(PIC GP4 TMR_OUT)のダイオード OR で、**`~RXF` は未読バイトがある限り
アサートされ続ける**。カーネルの ISR(`plt_interrupt()`)は ticks++ しか
しておらず**割り込み源を一切クリアしない**。一方、実機で動いていた
ブリングアップ用 `bios/bios.s` の `intr_handler` は `in a,(console)` で
**読んでクリア**していた。この差がそのまま残っている。

ストームだった場合の対処は「ISR で FT245 を読む」だが、**読んだバイトを
捨てると sh が入力を受け取れなくなる**(`kgetchar` はポーリングで読む設計)。
よって小さな RX リングを置き、ISR が積んで `kgetchar` が拾う形に変える必要が
ある(`io.c`、`#ifdef ARCH_Z80BOARD` で囲う)。ROM 空きは 771B あるので容量は足りる。

#### 4.6.4 片付け待ちの一時的な変更(プロンプトが安定したら戻す)

- `arch/z80board/Makefile`: **`SDDBG` を既定 ON にしてある**(素の
  `make ARCH=z80board` でログ無し版を焼いてしまい 1 往復無駄にしたため)。
  安定したら `SDDBG ?= 0` に戻す。
- `src/builtin.c`: プローブ用の ROM 容量を作るため `uptime`/`tree`/`ps`/
  `kill`/`df` を `SD_DEBUG` 時のみ除外している(空き 33B → 802B)。戻すこと。
- `src/init.c` `[i1]`-`[i4]` / `src/kexec.c` `[k1]`-`[k6]` / `diskio.c` の
  `RD lba=` ログ / `crt0.s` の `isr_probe_n` を使った `*` と切り替え先
  ブロック番号のプローブ ── 全て一時物。
- `arch/z80board/spi.s` の「CS High のまま SCK 8 発」追加は、**効果が無いと
  実機で確認済み**(入れる前後で `r=32` が 1 バイトも変わらなかった)。
  真因は上記 4.6.1 の アドレス方式だったので、**revert してよい**(14B 節約)。
- `arch/z80pack` 側の `ei` 欠落は未対応。cpmsim では露見しないが、同じ穴。

### 4.7 2026-09-19 **訂正** + タイマ無しで回す協調切り替え(e0f710c)

**4.6.2 の「`ei` がどこでも実行されていない」は誤診だった。** `src/kernel.c`
の `kernel_init()` が z80 共通で `im 1` + `ei` を実行していた(grep で crt0.s
と io.c しか見ておらず、C の `__asm ei __endasm` を見落とした)。同じく
「エミュレータは IFF を無視」「arch/z80pack にも同じ穴」も誤り
(z80pack は `TIMER=1` + `ei` で正しく動く)。4.6.4 の最後の項目も無効。

**本当の原因: 実機にはタイマ割り込みが無い。** 回路図(`D:\ドキュメント\電子工作\
KiCad 基板エディタ\Z80 BOARD 2025\`)の注記で、TMR_OUT(PIC12F683 GP4)は
「割り込み常時はいるの防止のため実装するまでつながない」と**意図的に未結線**。
残る割り込み源 FT245 `~RXF` はダイオード OR で /INT に入るレベル割り込みで、
ISR が読まないためキーを押すとストームになる。実機ログでも、`*2*0…` は
キーを押すまで出ず、押した後も `S` が一度も出なかった(`reti` 直後に次の
割り込みを受け付け、どちらのブロックも 1 命令も進めない)。

**対策(e0f710c、z80board のみ)**: 割り込みを一度も開けず、待ちループで
`KYIELD()` = `rst 38h`(ISR へソフトで入る)を撃つ協調切り替えにした。撃つ場所は
init の待ち / `con_break`(sh の前景待ち・`krun_wait`)/ `kgetchar` の空待ち /
`getticks`(時間待ちが抜けられるよう TICKS を進める)/ `proc_block`。
エミュレータで協調切り替えのみで `[/root]#` まで到達を確認済み。**実機は未確認。**

**受信判定の修正**: 74HC138 が A4-6 しかデコードしないので、port 0x00 は 0x01
(FT245 のデータ)と同じ。旧コードは 0x00 を「ステータス」として読み、1 バイト
消費していた。RXF# はステータスラッチ U19(74LS573, port 0x10)の bit6。
**LE は `OUT (0x10)` の間だけ開くので「書いてから読む」**。書く値の bit0 は
ROMKILL(1 で ROM が消える)なので必ず 0。`src/io.c` と `user/driver.c` を修正。
回路の解読結果の全体はメモリ `z80board-hw-io-map` にある。

**起動が不安定な件**: 74LS573 にはクリア入力が無く、電源投入時の Q0(ROMKILL)が
不定。`CS_BOARD_ROM# = A15 OR ROMKILL OR MREQ#`(ROMKILL/MREQ# は D8/D9 の
ダイオード OR + R39 プルダウン)なので、Q0 が 1 で立つと ROM が選択されず、
最初の 1 命令も読めない。ソフトでは直せない。当面は **D8 を浮かす**のが手軽
(ROMKILL は現状ソフトで一切使っていない)。将来使うなら 74HC74 に移して
CLR# = Z80_RESET。

**残課題**: 実機で協調切り替え版の確認 → プロンプト → `ls`。
z80boardsim の port 0x10 は RXF を模していない(常に 0 = データあり扱い)ため、
対話 `make run` では `con_break` が getchar でブロックしうる(要エミュ修正)。
タイマ配線後は、ISR で RXF を読んでリングに積む割り込み駆動へ戻すことを検討。

### 4.8 2026-09-19 **実機で動作確認完了**(z80board ブリングアップ一区切り)

自作基板(Z84C00 8MHz + FT245RL + bit-bang SPI の 2GB SD)で、
`BOOTING NOW !` → `SYSTEM STATUS CODE [5F]` → `FAT Drive ...... DETECTED` →
`System Driver .. LOADED` → `tizix` → `[/root]#` → `ls` / `ls /bin` / `pwd` /
`date` / `ps` が動作(コミット 241e84f 時点)。

4.7 以降に入れたもの:
- SD 書き込み後のビジー明け待ち(eb16a0c)。これが無いとヒストリ書き込み直後の
  読み出しが 0xFD タイムアウトで落ち `ls: not found` になっていた
- SD 読み書きエラーを常に物理コンソールへ(`SD RD ERR` / `SD WR ERR`、787a374)。
  リダイレクトを通さないのは FatFs の再入を避けるため
- デバッグプリントは消さずに `#if` / `;` で無効化(5a9838d)。SDDBG は既定 OFF
- ビルトインは「OS の心臓部(ブート必須 + プロセス操作)」だけに。uptime / tree / df
  は無効化して外部コマンド化待ち(task.md #56)。ROM 溢れの主因は FatFs(約 20KB、62%)
- ヒストリの SD 書き込みをコマンド実行の後へ(41c19c6)
- bit-bang SPI の書き直し(241e84f): spi_transfer 約 144 → 73T/bit、512B 受信専用
  spi_rx512 約 55T/bit。実機で体感でも速くなった
- SYSTEM STATUS は `OUT (0x10),0` してから読む(74LS573 の LE は書込時のみ)。
  `5F` = RXF#=1(受信データ無し)、TXE#=0(送信可)で正しい値

運用上の注意:
- **ROM と SD は必ずセットで焼く**。ROM だけ新しいと、コマンドは動くのに直後に
  `not found` が出る(task.md #57 で版ずれ検出を検討)
- rocky9 で SD へ書くときは、カードが本当に見えているか確かめてから
  (`[ -b /dev/sda ]` かつサイズ > 0)。無いまま dd すると `/dev/sda` という
  通常ファイルができ、読み戻しの md5 まで一致してしまう
- ヘッダを触ったら `make clean`(task.md #55)

次の予定: ESP32 変換基板が届いたらネットワーク(at_modem.py のテストベッドが既にある)。
残タスク: #55 ヘッダ依存 / #56 uptime・tree・df の外部化 / #57 版ずれ検出 /
#58 rx が実機で途中停止 / タイマ配線後のプリエンプション復活 / 74LS573 の ROMKILL
電源投入時不定(D8 を浮かす)。

## 5. 2026-09-14 セッション引き継ぎ(arch/z80pack: cpmsim static化 + client socketダイヤル拡張)

**このセクションは後日レビュー/指摘用にまとめたもの。iosim.c 側のみ着手
済みで、tizix guest 側(driver/AT コマンド送出/自作 wget)は未着手。**

### 5.1 z80pack-tizix (`z80pack/z80pack-tizix`、tizix リポジトリ外・
git 非管理) 側で見つかった/直した点
- **iosim.c の FDC オフバイワンバグ**: `if (track > disks[drive].tracks)`
  は誤りで `>=` が正しい(`track` は 0 始まり、`.tracks` はトラック数
  カウント。`sector` 側は 1 始まりなので `>` のままで正しい)。tizix 用
  に修正済み。**upstream(github.com/udo-munk/z80pack master、2026-09-14
  時点で確認)にも同一バグが現存**するが、報告は見送り(tizix は独自
  ビルド済みバイナリを同梱運用のため実害なし)。
- **cpmsim を static link に変更**: `cpmsim/srcsim/GNUmakefile` の
  `LINUX` ブロックに `PLAT_LFLAGS = -static` を追加し、
  `LFLAGS = $(PLAT_LFLAGS)` をリンク行に反映。依存が `libc.so.6` のみ
  (ncurses/pthread なし)だったためサイズ増は 297,664B→1,536,768B
  (+約1.2MB)で軽微。
- **GNUmakefile の VPATH 地雷**: `VPATH = ../../z80core ../../iodevices`
  が原因で、リンク先ターゲット `../cpmsim`(=`cpmsim/cpmsim`)を
  `../../z80core/../cpmsim`(=兄弟ディレクトリ `cpmsim/` 自体)に誤解決
  し「up to date」と誤認してリンクをスキップする潜在バグがある。
  **バイナリが既に存在する間は表面化しない**(ローカルに見つかるので
  VPATH 探索が走らない)が、`rm -f ../cpmsim` 等で消してから素の
  `make` を叩くと再現する。回避策は `make -B`(強制リビルド)。
- 差し替え済みバイナリ: `z80pack-tizix/cpmsim/cpmsim` と
  `tizix/arch/z80pack/cpmsim` の両方。旧動的版は
  `z80pack-tizix/cpmsim/cpmsim.bak.dyn` に保存。

### 5.2 ネットワーク機能の調査結果と実装(iosim.c のみ)
- **`sim.h` で `NETWORKING`・`HAS_CONFIG`・`PIPES` はすべて最初から
  デフォルト有効**(`#ifdef` 切り替えは無し)。つまり client socket #1
  (port 50=ステータス/51=データ)は元から完全実装済みで、
  `arch/z80pack/conf/net_client.conf`(tizix 側、cpmsim の cwd 相対で
  `./conf` が優先される)を置くだけで固定ホストへの接続ができる
  ── **iosim.c の改造は本来不要だった**。
- ただし固定接続先(起動時に conf ファイルから一度だけ解決・接続)
  だと、z80board + ESP-WROOM-02 実装時の AT コマンド的な「動的に
  接続先を選ぶ」使い方ができない。将来 z80board に ESP-WROOM-02 を
  載せる構想があり、そちらは AT コマンド(`AT+CIPSTART` 等)で dial
  するのが定石なので、**cpmsim 側にも同じ発想(コマンド行→データモード
  切替)を先に実装しておき、実機移行時は tizix 側の下位ドライバだけ
  差し替えれば済む設計**にした。
- **実装: client socket #1 の ATD ダイヤル拡張**(iosim.c、port 50/51
  はそのまま流用、新規ポートは追加していない)。
  - `net_client_connect()` を新設(元々 `nets1_in()` 内にインライン
    展開されていた gethostbyname/socket/connect のロジックを関数化)。
    **`gethostbyname()` の戻り値 NULL チェックを追加**(元コードは
    未チェックで NULL 参照即クラッシュだったが、conf ファイル固定
    運用では踏みにくかった潜在バグ。guest から任意ホスト名を打てる
    ようになった以上、trivially踏めるようになるため必須の修正)。
  - 接続失敗時は `cpu_error=IOERROR; cpu_state=STOPPED;` で CPU を
    止める元の挙動をやめ、**ログ警告のみで `cs=0` のまま握り潰す**
    ように変更(タイプミスや unreachable host で仮想マシンごと
    落ちるのは AT ダイヤル用途では論外なため)。
  - `netd1_out()`(port 51 への OUT ハンドラ)を拡張: **`cs == 0`
    (未接続)の間は、書き込まれたバイトをソケットへ送らず ASCII の
    1 行コマンドとして蓄積する**。`\r` は無視、`\n` で行確定。
    行が `"ATD"` で始まっていれば残りを `<host>:<port>` として
    `strrchr(..., ':')` で分割し、`net_client_connect()` を呼んで
    即座に dial する。`cs != 0`(接続済み)になれば従来通りソケット
    へそのまま素通し。
  - 相手が切断すれば(`nets1_in()` の `POLLHUP` 検出で)`cs` は 0 に
    戻るので、再度 `ATD host:port\n` を書けば繋ぎ直せる。
    `net_client.conf` による固定接続の既存経路は無改造でそのまま
    共存(`cs_port` が conf 経由で既に非ゼロなら起動直後に自動接続、
    ATD で上書きしたい場合はそのまま新しい host:port を送ればよい)。
  - ビルド確認済み(`make -B` で warning は既知の
    `gethostbyname` static link 警告のみ)。**tizix guest 側の
    ドライバ・実際の通信テスト(nc/socat 相手)は未実施・未着手**。

### 5.3 次のセッション/guest 側実装者向けメモ
- port 50 (status, IN): bit0=読み込み可能、bit1=書き込み可能。
  `POLLHUP` で相手切断を検出すると自動的に `cs=0` に戻る。
- port 51 (data): 未接続時は OUT した1行が `"ATD<host>:<port>\n"`
  形式ならダイヤルコマンドとして解釈される(大文字 `ATD` 固定、
  `strncmp` で完全一致判定なので小文字は不可)。接続済みなら普通の
  データ IN/OUT。
- 「自作 wget」は、固定 1 サーバ(rocky9 上に立てた簡易 HTTP サーバ
  等)相手であれば ATD 拡張なしでも `net_client.conf` だけで実現
  可能。任意ホストを打てる本物の wget にするなら今回の ATD 拡張が
  前提になる。
- テストはまず `nc -l <port>`(またはローカル HTTP サーバ)を rocky9
  上で立て、tizix 側から `ATD127.0.0.1:<port>\n` を port 51 へ OUT
  してから通常のデータ送受信、という手順で疎通確認すること。

### 5.4 追記(2026-09-15): ATD が一切トリガーされないバグを発見・修正
- guest 側(Gemini 作の `user/telnet.c`)は `NETSTAT & 0x02`(port 50
  bit1 = 送信可能)を待ってから port 51 に1バイトずつ書く、という
  正しい実装だった。
- ところが **`nets1_in()` は bit1 を `cs != 0`(=接続済み)の場合しか
  立てていなかった**。ATD で最初にダイヤルするにはまず未接続の状態で
  port 51 に書き込む必要があるのに、その「書き込み可能」フラグが
  未接続中は常に 0 ── 鶏卵状態でハングしていた
  (`net_send()` はタイムアウトして `val=0` のまま `-1` を返す)。
  5.2 で書いたパース処理(`strncmp` 等)は一度も実行されないまま。
- 修正: `cs == 0` の間は bit1(0x02)を無条件に立てるよう
  `nets1_in()` を変更(`netd1_out` は未接続時も ATD バッファへの
  追記を常に受け付けるので、これで矛盾しない)。ビルド・差し替え
  済み(`make -B`、警告は既知の `gethostbyname` のみ)。
- **ハマった点**: 差し替え時、テスト中の cpmsim プロセスが実行中
  だったため `cp` が `Text file busy` で失敗した。`cp` して
  `mv -f` でアトミック置換に切り替えて解決(実行中バイナリの
  差し替えは `cp` 直書きでなく `mv` を使うこと)。

### 5.5 追記(2026-09-15 続き): 実機テストで見つかったさらに3件のバグ
`python/test_telnet.py`(pty 経由で cpmsim を実際に起動し `telnet` を
叩く既存ハーネス)と `nc -l 8080` で実際に動かして確認したところ、
以下が判明・修正・実機確認済み(`nc` 側に実データ `HELLOTIZIX` が
届くところまで確認)。

1. **`user/telnet.c` のポート番号ミス**: `#define NET_STAT 0x50` /
   `NET_DATA 0x51` は10進80/81を指してしまっており、client socket #1
   の本当のポート(10進50/51 = 0x32/0x33)ではなく未使用ポート
   (`io_trap_in`、常に0xFF)を叩いていた。`RX: ff` の無限ループは
   これが原因。`50`/`51`(10進)に修正。5.4 で直した `nets1_in` の
   鶏卵バグは無関係ではなく正しい修正だが、この版のテストでは
   そもそも到達していなかった。
2. **static link下の `gethostbyname()` 不安定性**: static binary は
   NSS モジュールを `dlopen` できないため、`127.0.0.1` のような数値
   IP に対してさえ `gethostbyname()` が固まる/不安定になりうる
   (リンク時 warning の実害がここで出た)。`net_client_connect()`
   で `inet_aton()` を先に試し、失敗した(=数値IPでない)場合のみ
   `gethostbyname()` にフォールバックするよう変更(`<arpa/inet.h>`
   追加)。
3. **`connect()` の EINTR 未処理**: cpmsim の10msタイマー割り込みが
   loopback への `connect()` すら中断しうる(`EINTR`)。他の箇所
   (`netd1_out` 等)と同じ「EINTR ならリトライ」パターンを
   `connect()` にも追加。

**現状: ATD ダイヤル→接続→実データ送受信まで一通り動作確認済み。**
guest 側のテキスト整形(RXデバッグ表示など)は telnet.c 側の作り込み
次第で、host 側(iosim.c)はこれで完了とみなしてよい。

### 5.6 追記(2026-09-16): net.bin常駐デーモン+netcli(wget/ftpの土台) - 実証優先、精度は後回し
telnet.c は port50/51 を自分で直接ポーリングする一体型だったが、
「カーネルには入らないので常駐コマンドとしてバックグラウンド実行したい」
「厳密な意味のATコマンドやPPPは要らない、外向きだけでよい、host機が
パケットフォワードできればいい」「簡易wgetやftpが実装できる土台が
欲しい」という方針を受けて、中継デーモン + クライアントAPI に分離した。

- `src/kmem.h`: 元々「socket bridge記述子用」と予約コメントされていた
  `0x8C68-0x8DFF` 帯に共有制御ブロック(KW_NETCMD/KW_NETSTATE/
  KW_NETHOST 40B)+ RX/TXリングバッファ(各128B)を配置。
- `user/net.c`: 常駐デーモン本体。`net &` で起動し、port50/51(既存の
  ATD dial-on-demand拡張、無改造)をポーリングして共有リングへ素通しで
  中継するだけ。プロトコル解釈は一切しない。kill後は `net &` を打ち直せば
  起動時に状態を初期化するので、それがそのまま復旧手順になる。
- `user/netcli.h` / `netcli.c`: `net_connect("host:port")` / `net_read` /
  `net_write` / `net_close` の4関数のみ。wget/ftp 等はこれをリンクする
  想定(telnet の string.rel と同じ流儀)。
- rocky9(sdcc 4.5.0)でフルクリーンビルド確認済み。net.bin=492B(1ブロック
  に余裕)、警告は他コマンドと同じ定型のみ。**cpmsim実機での接続テスト
  (nc相手に実際にconnect/relayが動くか)はまだ。**

**既知の精度課題(今回は意図的に後回し)**: `netd1_out` は ATD行の `\n` を
受けた瞬間に `connect()` を同期的に呼ぶが、成否を guest 側へ伝える
ステータスビットが iosim.c 側に無い。そのため `net_connect` は「送信し
終えたら楽観的に connected 扱い」にしており、実際に繋がったかどうかは
呼び出し側(wget/ftp)が応答の有無やタイムアウトで判断するしかない。
恒久対策(iosim.c に成否ビットを足す/netcliのタイムアウト・再試行を
強化する等)は次の課題として残すが、**今は接続判定の精度より
「実際にcpmsim上でconnect→relayが動く」ことの実証を優先する。**

**実証結果(2026-09-16、python/test_nettest.py + `nc -l 8080`)**:
- **送信(tizix→host)は完全に動作確認**。`net &` → `nettest` が
  `127.0.0.1:8080` へ接続し、`net_write()` した `"hello from tizix\n"`
  が nc 側にバイト単位で正確に届いた(ring buffer → net.bin → port51 →
  iosim.c → 実TCPソケット、全経路実証済み)。
- **受信(host→tizix)に本物のバグを発見(精度の話とは別、正しさの問題)**。
  nc が16バイト("PONG-FROM-HOST\n")を送って接続を閉じた直後から、
  tizix側が数百バイトの高エントロピーな(ネットワークとは無関係に見える)
  バイト列を「受信」として読み続けた。
- **原因の見立て**: `nets1_in`(status読み取り)は POLLHUP 検出時に
  `close(cs); cs=0;` するが **`cs_port` をクリアしない**。そのため直後の
  status ポーリング1回で `cs==0 && cs_port!=0` に触れ、
  `net_client_connect()` が即座に自動再ダイヤルされる。cpmsim は
  "CPU speed is unlimited" で動いており、net.bin は1ループで NETSTAT を
  2回(RX確認/TX確認)ポーリングするため、この自動再接続サイクルと
  高頻度に絡み、本来の接続とは無関係な状態を読んでいる可能性が高い。
  netd1_in 側(port51読み取り)は `read(cs,&c,1)!=1` のエラー処理はあるが
  cs==0 のガードが無い点も疑わしい。
- **判断**: tizix 側(net.c/netcli.c)ではなく iosim.c(z80pack-tizix
  フォーク)の接続ライフサイクル管理に起因する可能性が高い。この
  フォークは従来「オフバイワン修正1件のみ」という慎重な改造方針
  ([[z80pack-tizix-fork-status]])だったため、ここで即座に手を入れず
  一旦報告に留める。次にこの基盤を触るときの最初の調査対象。
- python/test_nettest.py と user/nettest.c は使い捨てではなく、**この
  バグの再現手段として残置する**(削除しない)。

### 5.7 追記(2026-09-17): net常駐時クラッシュ(#51)の根本原因を特定・修正
5.6 で報告に留めた「受信直後に無関係なバイト列を読み続ける」バグとは別に、
`net &` 常駐下で `nettest` を実行すると**システムが非決定的にリブート/クラッシュ
する**、より深刻な問題があった(task.md #51)。gemini がレート制限で停止した
セッションを Claude が引き継いだ際に発見・修正した。

- **調査手法**: python/tz80.py(単一命令ステップ Z80 デバッガ)に client
  socket #1(port 50/51)の簡易シミュレーションを新規追加した
  (`python/tz80.py net status|rx|hangup-after|hangup-now|clear-tx`)。
  実 TCP は張らず、ATD dial 検出+常時成功接続+RX 注入バッファのみを
  模す。従来 tz80.py はこのポート帯を一切扱っておらず、
  `net_connect()` の 500 tick タイムアウトを毎回無限に待つだけで
  ネットワーク絡みのバグを再現できなかった。
- これで `net &`→`nettest` を実トレースし、500,000 ステップ刻みの
  二分探索で「SP が健全な値(0xDE70 付近)から突然 0x000D 付近まで
  暴落する」箇所を特定 → 直前に実行された `jp p, 0x03AB`(本来
  `D3AB` へ飛ぶべきところ、`+IY` 補正されず未再配置のカーネル領域
  へ wild jump していた)が真犯人と判明。
- **真因は `user/iy_reg_claude.py`(sdcc 出力を IY 相対 PIC へ変換する
  ビルド時ツール、全 sdcc 外部コマンド共通)自体のバグ**: SDCC が
  符号付き比較で生成する `jp PO,skip`→`xor`→`skip: jp M,target` 型の
  2段分岐を変換する際、変換器自身が挿入する skip 先ラベル
  (`L_skipjp_N`)への `jp <cond>, L_skipjp_N` を素の絶対 jp のまま
  残していた。しかもそのラベル名が「二重変換防止フィルタ」に
  自分自身で引っかかり、ビルド時の検証パス(安全装置)でも見逃されて
  いた。`netcli.c` の `net_read()`/`net_write()` の while ループ条件が
  まさにこのパターンを踏んでおり、`net &` 常駐下で高頻度実行される
  タイミングでのみ暴発していたため「nettest 送信後」「タイミング
  依存」「非決定的」に見えていた。
- **修正**: Z80 の `RET` が `JP` と同じ8条件(PO/PE/P/M 含む)を
  取れることを利用し、`seq_cond_indirect_jp()` という位置独立な
  条件分岐シーケンスへ置換。全コマンドをクリーンリビルドし検証パス
  エラーなし、tz80.py で 5,000,000 ステップのワイルドジャンプなし
  トレース、実機 cpmsim で `net &`→`nettest`→`nettest` を実行し
  1回目は正常完走・2回目は(前接続が IDLE に戻っていないため正しく)
  `connect failed`、クラッシュせずプロセス生存を確認。詳細は
  task.md #51 および memory `iy-reg-claude-jpcond-bug` 参照。
- **残課題(5.7執筆時点)**: 5.6 で報告した「受信直後の無関係なバイト列」は
  上記とは別系統のバグとして task.md #54 に切り出し済み(TX リングバッファ
  経由の中継がコンソール文字列の断片を含むゴミを追加送信する)。

### 5.7.1 追記(2026-09-17続き): #54 も解決 — 5.7 の修正自体に新規バグがあった
ユーザーの実機テストで「nettest は2回目でバグる」との指摘を受けて再着手。
2つの不具合が重なっていた:

1. `user/nettest.c` が `net_close()` を呼んでいなかったため、daemon 側の
   `KW_NETSTATE` が CONNECTED のまま戻らず、2回目の `net_connect()` が
   常に `connect failed` になっていた。末尾に `net_close();` を追加。
2. **本命: 5.7 の `seq_cond_indirect_jp()` 自体に新規バグがあった**。
   `ret <cond>` が不成立だった場合の後始末に `pop af` を使っていたが、
   これはスタックに積んだままの「未使用の実アドレス」の下位ワードを
   そのまま AF レジスタへ pop してしまい、直前の signed 比較が
   セットした本物の判定フラグをゴミで上書きしていた。壊れたフラグが
   後続の第2段階比較まで伝播し、`netcli.c` の `while (n < len)` が
   毎回同じ側に倒れ続けて実質無限ループ化。string リテラルが rodata
   上に連続配置されているため、over-run したコピーが送信データの
   直後にある別の文字列リテラルをそのまま host へ送っていた。
   **修正**: 後始末を `pop af` から `inc sp` ×2 に変更(16bit `INC rr`
   はレジスタ・フラグを一切変更しない、SDCC 自身も同じ目的でこの
   イディオムを使っている)。
3. **ハマった点(ビルド)**: 中間ファイル(`*.asm/*.rel/*.iy.asm`)だけ
   `rm` して `make` しても `nettest.bin` 自体が古いまま再生成されない
   ことがあった。**最終 `.bin` も明示的に消してから `make` し直す**必要
   がある(この構成の Make 依存関係の穴)。
4. **検証**: tz80.py で修正前後を比較(修正前は n=428 でも終了せず、
   TXBUF に nettest 自身の文字列リテラルが混入。修正後は n=17 で正しく
   終了)。実機 cpmsim で `net &`→`nettest`→`nettest` を実行、両方とも
   `connecting...`→`connected (optimistic)`→`done` まで正常完走、
   host 側は両回とも `hello from tizix\n`(17バイトちょうど)のみ受信、
   ゴミなしを確認。副産物として `rx.bin` も同じバグを踏んでいたことが
   判明(修正で 3032B→3150B に増加)。詳細は task.md #51/#54、memory
   `net-tx-garbage-54`・`iy-reg-claude-jpcond-bug` 参照。

### 5.7.2 追記(2026-09-17続き): python/at_modem.py — ESP32 AT コマンドのテストベッド
ユーザーの実機構想(z80board に ESP32 を SPI bit-bang で接続し、AT コマンド
(ESP-AT ファームウェア)を叩いて WiFi 経由の TCP 通信を行う)に向けて、
`python/at_modem.py` を Hayes 風の単純な ATD ダイヤラから、**ESP-AT の
主要コマンドセットへ応答するテストベッド**へ全面的に書き直した。
「SPI 層は z80pack(cpmsim)では省略してよい」との方針を受け、SPI の
電気的/フレーミング詳細はモデル化せず、**プレーンな TCP 越しに AT
コマンドのテキストストリームだけを受け答えする**構成にした。

対応コマンド(1接続のみ、CIPMUX=0相当): `AT` / `ATE0|1` / `AT+RST` /
`AT+GMR` / `AT+CWMODE=` / `AT+CWJAP="ssid","pass"`(WiFi接続は常に即成功
したふり)/ `AT+CIFSR` / `AT+CIPSTATUS` / `AT+CIPSTART="TCP",host,port`
(**実際に TCP connect() する**)/ `AT+CIPSEND=<n>`(**実際に送信**)/
`AT+CIPCLOSE`。相手からの受信は非同期スレッドが監視し、ESP-AT 標準の
`+IPD,<len>:<data>` 形式でクライアント側へ流す(相手が切断すれば
`CLOSED` も通知)。

検証: `AT`→`ATE0`→`AT+CWMODE=1`→`AT+CWJAP=...`→`AT+CIFSR`→
`AT+CIPSTART="TCP","127.0.0.1",9000`→`AT+CIPSEND=17`+生データ、という
一連のコマンド列を python テストクライアントから流し、実際に立てた
テスト用 TCP サーバ(port 9000)へデータが正しく届くこと・相手からの
応答が `+IPD,17:...` として正しく非同期通知されることを確認済み。

**未着手**: z80board 側(または net.c 相当のドライバ)からこのテストベッド
へ実際に AT コマンドを送る配線はまだ無い。現状はスタンドアロンの python
ツールとして、将来の z80board ファームウェア開発時に host 側で立てて
使うテストベッドという位置づけ。

### 5.8 追記(2026-09-17): 外部コマンドを tzcc へ全面移行すべきか(議論のみ、未着手)
ユーザーから「この際もう tzcc に全部寄せるべきか、地雷になるか」と問われた
議論の記録。現状把握:

- `user/Makefile` の `COMMANDS` は `hello a b date_dbg rx cp_dbg test1 t2
  blk wak ptx prx sh telnet net nettest ftp` ── **coreutils(cat/cp/ls/
  echo/head/tail/wc 等 21 本、#26 で tzcc 移行済み)以外は今も sdcc +
  iy_reg_claude.py のまま**。`sh`(シェル本体)もここに含まれる点に
  ユーザーは驚いていた([[tzcc-vs-iy-reg-coverage]])。
- 5.7 の #51 は、まさにこの sdcc + iy_reg_claude.py パイプライン側
  (netcli.c)で踏んだバグであり、tzcc へ移行していれば原理的に
  発生し得なかったクラスの不具合だった(tzcc は IX 相対アドレッシング
  を採用しており、iy_reg のような「ビルド後に絶対番地へ +IY するパッチ」
  を必要としない設計 ─ [[tzcc-cost-model]])。
- **Claude の見解(実装はまだしていない)**: 一気に全部移すのは地雷。
  - 低リスクで進められるもの: `hello`/`a`/`b`/`date_dbg`/`rx`/`cp_dbg`/
    `test1`/`t2`/`blk`/`wak`/`ptx`/`prx` のような小さい開発スクラッチ系、
    および `net`/`nettest`/`tzftp`(coreutils と同程度の複雑度)。
  - 高リスクで後回しにすべきもの: **`sh`**。複数ブロック・ヒストリ・
    行編集・ジョブ制御・リダイレクトを抱えた最も複雑で最も落ちては
    いけないファイル。焦って移すとシェルそのものが動かなくなる
    blast radius になる。
  - 未検証の懸念点: `net.c`/`telnet.c` が使う `__sfr __at`(生 I/O
    ポート直叩き)を tzcc が同じように正しく扱えるか未確認。ここを
    確認せずに移すと、2026-09-16〜17 に gemini が `__sfr` 宣言を
    誤って書き換えて起きた退行([[net-relay-daemon]]、task.md #52)と
    同種の事故を再現しかねない。
  - 推奨する進め方(実施はユーザー判断待ち): 「小物から着手 → net 系 →
    最後に sh」の順で段階的に。
