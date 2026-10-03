.module crt0

;===================================================================
; arch/z80pack crt0.s(cpmsim。boot.s が 0x0100 へロードして jp してくる)
;
;   このファイルにあるのは z80pack に固有の部分だけ:
;     ・0x0100 の入口トランポリン
;     ・低位ベクタ(0x0038〜0x006C)を **実行時に書き込む**処理
;       (RAM 上で動くのでできる。z80board は ROM なので .org で焼く)
;     ・ISR の入口(要因はタイマ 1 つだけなので、AF を積んですぐ共通部分へ)
;   ボードに依らない部分(宣言と定数、RAM とスケジューラ表の初期化、スケジューラ
;   本体と _kexit、エリア順と gsinit)は ../common-z80/ にあり、z80board と共有する。
;   .include した位置にそのままコードが出る。
;===================================================================

        .include "../common-z80/crt0-defs.inc"

; ---- 0x0100 : 最小トランポリン (boot が jp してくる固定入口) --------
; start 本体は _CODE に置く。_HEADER を 3B に固定し、boot コードが
; 何バイト伸びても --code-loc 0x0120 の _CODE と衝突しないようにする。
		.area   _HEADER (ABS)
        .org    0x0100
        jp      start

        .area   _CODE
start:
        di
        ; SP = 0x9000(block0 後端 +1)から下へ。カーネルスタックは block0 内。
        ; block1 は DRIVER 常駐(飛び地)、block7 はプロセス枠。どちらもカーネル
        ; スタックには使わない(スタックは block0 内で完結)。
        ;
        ; スタック枠は 512B(0x8E00-0x8FFF)を運用上の約束とする(kmem.h 参照)。
        ; 0x8419-0x8DFF は VFS/socket/将来枝データに開放済み。SP 初期値自体は
        ; 0x9000 のまま(512B 枠の頂上)なのでここに変更は無い。実測上カーネル
        ; 文脈の最深は FatFs(FF_FS_TINY=1: FIL=34B/DIR=40B, cp で FIL×2=68B)+
        ; ff.c 呼出深で 1KB 未満。512B を大きく超えて潜ると 0x8DFF から下の
        ; データを破壊するので、深い再帰・ISR 多重ネストには注意。溢れたら
        ; SP 起点を上げる(枠拡大)かデータ配置を下げること。
        ld      sp, #0x9000
        ld      a, #0xC3
        ld      (0x0038), a
        ld      hl, #isr
        ld      (0x0039), hl

        ; --- tizix ソフトウェア割り込みベクタ (ISR 直後 0x003B〜に集約) ---
        ;   下位 0x0000-0x0037 は将来デバイス用に完全解放。カーネルへの
        ;   全インポート入口をここへ集める。番地は固定(kputchar が .map に
        ;   依存しないよう、ISR 直後の空き地に体系的に確保)。
        ;   起点 0x003B = ISR(0x0038-0x003A, jp isr)の直後。
        ;
        ;     0x003B : jp _kexit     (コマンド終了)
        ;     0x003E : jp _kputchar  (物理層直結。1バイト出力。ドライバ/
        ;                             外部コマンドが叩く固定番地)
        ;     0x0041 : jp _kgetchar  (物理層直結。1バイト入力)
        ;     0x0044 : jp _getticks  (100Hz tick、delay 用)
        ;     0x0047 : jp _kprintf   (書式変換。putchar を使う上位)
        ;
        ;   コマンド/ドライバは -g _kputchar=0x003E ... でリンク。reloc は
        ;   絶対のまま(base を足さない)。
        ld      a, #0xC3               ; JP
        ld      (0x003B), a
        ld      hl, #_kexit
        ld      (0x003C), hl
        ld      a, #0xC3
        ld      (0x003E), a
        ld      hl, #_kputchar
        ld      (0x003F), hl
        ld      a, #0xC3
        ld      (0x0041), a
        ld      hl, #_kgetchar
        ld      (0x0042), hl
        ld      a, #0xC3
        ld      (0x0044), a
        ld      hl, #_getticks
        ld      (0x0045), hl
        ld      a, #0xC3
        ld      (0x0047), a
        ld      hl, #_kprintf
        ld      (0x0048), hl

        ; --- 時刻ベクタ(0x004A〜。epoch は 100Hz 積算、外部 DATE.BIN が叩く) ---
;     0x004A : jp _time_get  (現在 Unix 秒 32bit を返す。戻り DEHL)
;     0x004D : jp _time_set  (Unix 秒 32bit を設定。引数 DEHL)
;   カーネルは epoch を刻んで渡すだけ。変換(除算)は DATE.BIN 側。
ld      a, #0xC3
ld      (0x004A), a
ld      hl, #_time_get
ld      (0x004B), hl
ld      a, #0xC3
ld      (0x004D), a
ld      hl, #_time_set
ld      (0x004E), hl

; --- SDCC 4.x helper (0x0050: jp (hl)) ---
ld      a, #0xE9
ld      (0x0050), a
ld      a, #0xC9
ld      (0x0051), a

        ; --- ivthelpers ランタイム IVT (0x0052〜0x006C) ---
        ;   ABI統一(--sdcccall 0)で標準ライブラリと衝突する除算/乗算/剰余/
        ;   memcmp/strcmp を自前供給し、固定番地で共有する。番地は kmem.h/
        ;   外部コマンドの -g とここだけで管理(体系的に 3B 刻み)。
        ;     0x0052 __divuint  0x0055 __moduint   (16bit unsigned / %)
        ;     0x0058 __divsint  0x005B __modsint   (16bit signed   / %)
        ;     0x005E __divulong 0x0061 __mullong   (32bit unsigned / *)
        ;     0x0064 __mulint                      (16bit *)
        ;     0x0067 _memcmp    0x006A _strcmp
        ld      a, #0xC3
        ld      (0x0052), a
        ld      hl, #__divuint
        ld      (0x0053), hl
        ld      a, #0xC3
        ld      (0x0055), a
        ld      hl, #__moduint
        ld      (0x0056), hl
        ld      a, #0xC3
        ld      (0x0058), a
        ld      hl, #__divsint
        ld      (0x0059), hl
        ld      a, #0xC3
        ld      (0x005B), a
        ld      hl, #__modsint
        ld      (0x005C), hl
        ld      a, #0xC3
        ld      (0x005E), a
        ld      hl, #__divulong
        ld      (0x005F), hl
        ld      a, #0xC3
        ld      (0x0061), a
        ld      hl, #__mullong
        ld      (0x0062), hl
        ld      a, #0xC3
        ld      (0x0064), a
        ld      hl, #__mulint
        ld      (0x0065), hl
        ld      a, #0xC3
        ld      (0x0067), a
        ld      hl, #_memcmp
        ld      (0x0068), hl
        ld      a, #0xC3
        ld      (0x006A), a
        ld      hl, #_strcmp
        ld      (0x006B), hl

        ; --- ここから共通: _DATA のゼロ埋め / gsinit / PCB 初期化 / メモリチェック ---
        .include "../common-z80/crt0-init.inc"

        call    _init
        halt

; ---- 0x0038 timer ISR : ticks++ の後に save/pick/restore ----
;   cpmsim の割込み要因は 100Hz タイマだけなので、AF を積んですぐ共通部分へ入る。
isr:
        push    af
        .include "../common-z80/crt0-sched.inc"

        .include "../common-z80/crt0-areas.inc"
