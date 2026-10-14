; ipl.s - PC-8801 の IPL(トラック 0 セクタ 1 の 256B)
;
;   ROM が 0xC000 へ読んで jp してくる(DE=0xC100)。ここからカーネルを
;   サブ CPU(FDD 側 Z80)に頼んで 0x0100 へ読み込み、64KB RAM モードへ
;   切り替えて 0x0100 へ飛ぶ。
;
;   サブ CPU との握手は ports 0xFC-0xFF(8255)。手順は cisc 氏の擬似 BIOS
;   (bios.asm の DiSendCommand / DiSendData / DiRecvData、disk.asm の
;   READDISK)から読み取ったもの。実機 ROM では未確認。
;     read  = cmd 2 + {枚数, ドライブ, トラック, セクタ} → cmd 3 で 256B x 枚数
;   トラック番号は「シリンダ*2+面」の通し番号、セクタは 1 起点(2D は 16 枚/面)。
;
;   ディスク配置(make_d88.py の入力 raw): 先頭 256B = この IPL、
;   次の 256B からカーネル(0x0100 起点、NSEC*128 B = 127 セクタ)。
;
;   ROM が 0x0000-0x7FFF を覆っていても書き込みは下の RAM へ届くので、
;   ROM モードのまま 0x0100 へ直接読み込める。
        .module ipl
        .area   _IPL (ABS)
        .org    0xC000

TVRAM   = 0xF3C8 + 120*3        ; 4 行目の先頭(1 行 = 文字 80 + 属性 40)
KERNEL  = 0x0100
NSECT   = 127                   ; カーネル 256B セクタ数(= NSEC*128/256)

start:
        di
        ld      sp, #0xE000
        ld      hl, #msg
        ld      de, #TVRAM
bnr:    ld      a, (hl)
        or      a
        jr      z, bnr_end
        ld      (de), a
        inc     hl
        inc     de
        jr      bnr
bnr_end:
        ld      hl, #KERNEL
        ld      b, #0           ; トラック
        ld      c, #2           ; セクタ(1 は IPL 自身)
        ld      d, #NSECT       ; 残り枚数
nexttrk:
        ld      a, #17          ; このトラックで読める枚数 = 17 - セクタ
        sub     c
        cp      d
        jr      c, n_ok
        ld      a, d            ; 残りの方が少なければ残り
n_ok:
        ld      e, a            ; e = 今回の枚数
        ld      a, #2           ; READ DISK
        call    sendcmd
        ld      a, e
        call    senddata
        xor     a               ; ドライブ 0
        call    senddata
        ld      a, b
        call    senddata
        ld      a, c
        call    senddata
        ld      a, #3           ; SEND BUFFER
        call    sendcmd
        push    bc
        push    de
rxsec:
        ld      b, #0
rxbyte:
        call    recv
        ld      (hl), a
        inc     hl
        djnz    rxbyte
        dec     e
        jr      nz, rxsec
        pop     de
        pop     bc
        ld      a, d
        sub     e
        ld      d, a
        jr      z, done
        inc     b               ; 次のトラック
        ld      c, #1
        jr      nexttrk
done:
        ld      a, #0x1A        ; 64KB RAM モード(bit1)+ pbios の初期値 0x18
        out     (0x31), a
        jp      KERNEL

; 8255 握手(破壊するのは A のみ)
sendcmd:
        push    af
        ld      a, #0x0F        ; ATN=1
        out     (0xFF), a
        pop     af
senddata:
        push    af
sd1:    in      a, (0xFE)
        and     #0x02           ; RFD=1 待ち
        jr      z, sd1
        ld      a, #0x0E        ; ATN=0
        out     (0xFF), a
        pop     af
        out     (0xFD), a
        push    af
        ld      a, #0x09        ; DAV=1
        out     (0xFF), a
sd2:    in      a, (0xFE)
        and     #0x04           ; DAC=1 待ち
        jr      z, sd2
        ld      a, #0x08        ; DAV=0
        out     (0xFF), a
sd3:    in      a, (0xFE)
        and     #0x04           ; DAC=0 待ち
        jr      nz, sd3
        pop     af
        ret
recv:
        ld      a, #0x0B        ; RFD=1
        out     (0xFF), a
rv1:    in      a, (0xFE)
        and     #0x01           ; DAV=1 待ち
        jr      z, rv1
        ld      a, #0x0A        ; RFD=0
        out     (0xFF), a
        in      a, (0xFC)
        push    af
        ld      a, #0x0D        ; DAC=1
        out     (0xFF), a
rv2:    in      a, (0xFE)
        and     #0x01           ; DAV=0 待ち
        jr      nz, rv2
        ld      a, #0x0C        ; DAC=0
        out     (0xFF), a
        pop     af
        ret

msg:    .asciz  "TIZIX IPL"
