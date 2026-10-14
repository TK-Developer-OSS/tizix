.module boot
        .area   _BOOT (ABS)
        .org    0x0000

CONDAT  = 1
FDCD    = 10
FDCT    = 11
FDCS    = 12
FDCOP   = 13
FDCST   = 14
DMAL    = 15
DMAH    = 16

KERNEL  = 0x0100
NSEC    = 254            ; kernel image sectors (128B ea)。VFS 一本化 5a+5b(wait/wake
                        ; + カーネルパイプ)でカーネルがピークに達し 0x7F00 台へ。
                        ; 254 = ハード上限(限界 0x8000 = DATA_LOC)。arch.mk と一致必須。
                        ; Step 7 以降のコマンド外部化で fat_* 等が抜けて縮む(その後 NSEC
                        ; を戻す余地あり)。B レジ上限 255。

start:
        di
        ld      sp, #0x0100
        xor     a
        out     (FDCD), a
        ld      d, #0           ; track
        ld      e, #2           ; sector (1 はこのブートセクタ)
        ld      hl, #KERNEL
        ld      b, #NSEC
rdloop:
        ld      a, d
        out     (FDCT), a
        ld      a, e
        out     (FDCS), a
        ld      a, l
        out     (DMAL), a
        ld      a, h
        out     (DMAH), a
        xor     a
        out     (FDCOP), a
        in      a, (FDCST)
        or      a
        jr      NZ, err
        ld      a, l
        add     a, #128
        ld      l, a
        jr      NC, nohi
        inc     h
nohi:
        inc     e
        ld      a, e
        cp      #27
        jr      C, next
        ld      e, #1
        inc     d
next:
        djnz    rdloop

        jp      KERNEL

err:
        ld      a, #0x45        ; 'E'
        out     (CONDAT), a
        halt