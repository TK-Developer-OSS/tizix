/* flashfs.S - 内蔵 Flash の FAT イメージ(ビルドで作る flashfs.img)をカーネルの像に抱かせる。
 *   SD が無いときのドライブ 0(diskio.c)。4KB 境界に置く(Flash の消去単位)。
 *   FS として書き換えられるのは、この領域だけ。 */
    .section .rodata.flashfs, "a"
    .balign 4096
    .global flashfs_img
    .global flashfs_end
flashfs_img:
    .incbin "flashfs.img"
flashfs_end:
    .balign 4096
