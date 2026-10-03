/* src/ffunicode_min.c -- FatFs の文字コード変換の最小版(gcc 系のポート、task.md #114)
 *
 *   長いファイル名(FF_USE_LFN)を有効にすると、FatFs は 3 つの変換関数を要る:
 *     ff_oem2uni / ff_uni2oem  短名(8.3)の OEM コード ⇔ Unicode
 *     ff_wtoupper              名前を大文字小文字を区別せずに比べるための大文字化
 *   本物(FatFs 同梱の ffunicode.c)は日本語の短名(cp932)だと数十 KB の表を持つ。ここは
 *   **ASCII だけ**を扱う最小版(src/ffconf.h の FF_CODE_PAGE 437 で使う):
 *     ・長い名前の中身は UTF-8 のまま(UTF-16 で)保存されるので、日本語の名前も作れて読める
 *     ・短名に ASCII 以外が要るときは FatFs が「変換できない」として '_' などに置き換える
 *     ・大文字小文字を無視して比べるのは ASCII の範囲だけ
 *   z80 は長いファイル名を使わない(ROM に入らない)ので、このファイルはリンクしない。
 */
#include "ff.h"

#if FF_USE_LFN

WCHAR ff_oem2uni(WCHAR oem, WORD cp)
{
    (void)cp;
    return oem < 0x80 ? oem : 0;
}

WCHAR ff_uni2oem(DWORD uni, WORD cp)
{
    (void)cp;
    return uni < 0x80 ? (WCHAR)uni : 0;
}

DWORD ff_wtoupper(DWORD uni)
{
    return (uni >= 'a' && uni <= 'z') ? uni - 0x20 : uni;
}

#endif
