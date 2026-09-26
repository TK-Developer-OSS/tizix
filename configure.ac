dnl configure.ac - tizix のビルド環境検査(#73)
dnl
dnl   生成物は config.mk 1 本だけ。Makefile は生成しない(開発ツリーは
dnl   config.mk 無しでも今までどおり `make ARCH=…` で動く)。
dnl   configure 本体はリリース版ツリーにだけ入れる(利用者は autoconf 不要)。
dnl   開発ツリーで試すなら: autoconf && ./configure
dnl
dnl   方針: 足りないものは **全部並べてから** 止める(1 個直すと次が出る、を避ける)。
dnl   SDCC は版ごとに後方互換が無いので 4.5.0 以外は通さない。同梱もしない。

AC_PREREQ([2.69])
AC_INIT([tizix], [0.1])
AC_CONFIG_SRCDIR([src/kernel.c])

tz_missing=""
tz_warn=""
tz_nl='
'

dnl ------------------------------------------------------------------
dnl SDCC 4.5.0(z80 系 2 アーキ: z80pack / z80board)
dnl ------------------------------------------------------------------
AC_ARG_WITH([sdcc],
  [AS_HELP_STRING([--with-sdcc=DIR], [SDCC 4.5.0 の bin ディレクトリ(既定: PATH から探す)])],
  [SDCC_BIN=$withval], [SDCC_BIN=])

tz_sdcc_path=$PATH
test -n "$SDCC_BIN" && tz_sdcc_path=$SDCC_BIN
AC_PATH_PROG([SDCC], [sdcc], [], [$tz_sdcc_path])

tz_sdcc_hint="    入手: https://sourceforge.net/projects/sdcc/files/ の 4.5.0 ソースを取り、z80 以外の
    ポートを外してビルド(例: ./configure --disable-pic14-port --disable-pic16-port
    --disable-ucsim && make && make install)。別の場所に入れたら --with-sdcc=DIR/bin"
if test -z "$SDCC"; then
  tz_missing="$tz_missing$tz_nl  - sdcc 4.5.0 が見つからない$tz_nl$tz_sdcc_hint"
else
  AC_MSG_CHECKING([sdcc の版])
  tz_sdcc_ver=`"$SDCC" -v 2>/dev/null | sed -n '1s/.* \([[0-9]][[0-9]]*\.[[0-9]][[0-9]]*\.[[0-9]][[0-9]]*\) .*/\1/p'`
  AC_MSG_RESULT([$tz_sdcc_ver])
  if test "$tz_sdcc_ver" != "4.5.0"; then
    tz_missing="$tz_missing$tz_nl  - sdcc が $tz_sdcc_ver($SDCC)。tizix は 4.5.0 でしか確認していない$tz_nl$tz_sdcc_hint"
  fi
  test -z "$SDCC_BIN" && SDCC_BIN=`dirname "$SDCC"`
  for tz_p in sdasz80 sdldz80 sdar makebin; do
    AC_MSG_CHECKING([$tz_p])
    if test -x "$SDCC_BIN/$tz_p"; then
      AC_MSG_RESULT([$SDCC_BIN/$tz_p])
    else
      AC_MSG_RESULT([no])
      tz_missing="$tz_missing$tz_nl  - $tz_p が $SDCC_BIN に無い(SDCC 4.5.0 に同梱のもの)"
    fi
  done
fi

dnl ------------------------------------------------------------------
dnl ホストのツール(tzcc のビルド、ディスク像の作成、テスト)
dnl ------------------------------------------------------------------
AC_PATH_PROG([HOSTGCC], [gcc])
test -z "$HOSTGCC" && tz_missing="$tz_missing$tz_nl  - gcc(tzcc をビルドする)   dnf install gcc make / apt install build-essential"

AC_PATH_PROGS([PYTHON3], [python3.11 python3])
if test -z "$PYTHON3"; then
  tz_missing="$tz_missing$tz_nl  - python3(3.6 以上)   dnf install python3 / apt install python3"
else
  AC_MSG_CHECKING([python3 が 3.6 以上か])
  if "$PYTHON3" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 6) else 1)' 2>/dev/null; then
    AC_MSG_RESULT([yes])
  else
    AC_MSG_RESULT([no])
    tz_missing="$tz_missing$tz_nl  - $PYTHON3 が古い(3.6 以上が要る)"
  fi
fi

for tz_p in mcopy mmd mdir; do
  AC_PATH_PROG([TZ_MTOOL], [$tz_p])
  if test -z "$TZ_MTOOL"; then
    tz_missing="$tz_missing$tz_nl  - $tz_p(mtools)   dnf install mtools / apt install mtools"
  fi
  unset TZ_MTOOL ac_cv_path_TZ_MTOOL
done

AC_PATH_PROG([MKFS_FAT], [mkfs.fat], [], [$PATH:/usr/sbin:/sbin])
test -z "$MKFS_FAT" && tz_missing="$tz_missing$tz_nl  - mkfs.fat   dnf install dosfstools / apt install dosfstools"

dnl ------------------------------------------------------------------
dnl m68k-mega(任意。無ければ ARCH=m68k-mega だけ作れない)
dnl ------------------------------------------------------------------
AC_ARG_WITH([m68k],
  [AS_HELP_STRING([--with-m68k=DIR], [m68k-elf-gcc の bin ディレクトリ(既定: PATH から探す)])],
  [M68K_BIN=$withval], [M68K_BIN=])
tz_m68k_path=$PATH
test -n "$M68K_BIN" && tz_m68k_path=$M68K_BIN
AC_PATH_PROG([M68K_GCC], [m68k-elf-gcc], [], [$tz_m68k_path])
if test -z "$M68K_GCC"; then
  tz_warn="$tz_warn$tz_nl  - m68k-elf-gcc が無い → ARCH=m68k-mega は作れない(z80 の 2 アーキには不要)"
else
  test -z "$M68K_BIN" && M68K_BIN=`dirname "$M68K_GCC"`
fi

dnl ------------------------------------------------------------------
dnl シミュレータ(任意。無ければビルドは可、run / 回帰は不可)
dnl   cpmsim / z80boardsim は z80pack-tizix フォークから作る(`make sims`)。
dnl   m68ksim はこのツリーの中で make ARCH=m68k-mega が作る。
dnl ------------------------------------------------------------------
AC_ARG_WITH([z80pack],
  [AS_HELP_STRING([--with-z80pack=DIR], [z80pack-tizix フォークの場所(既定: ../z80pack-tizix)])],
  [Z80PACK_DIR=$withval], [Z80PACK_DIR=])
if test -z "$Z80PACK_DIR" && test -d "$srcdir/../z80pack-tizix/z80boardsim"; then
  Z80PACK_DIR=`cd "$srcdir/../z80pack-tizix" && pwd`
fi
AC_MSG_CHECKING([z80pack-tizix フォーク])
if test -n "$Z80PACK_DIR" && test -d "$Z80PACK_DIR/cpmsim/srcsim" && test -d "$Z80PACK_DIR/z80boardsim/srcsim"; then
  AC_MSG_RESULT([$Z80PACK_DIR])
else
  AC_MSG_RESULT([no])
  test -n "$Z80PACK_DIR" && tz_warn="$tz_warn$tz_nl  - $Z80PACK_DIR に cpmsim/srcsim・z80boardsim/srcsim が無い"
  Z80PACK_DIR=
fi
dnl cpmsim は起動時に `receive`(フォークの cpmsim/srctools)を ./receive か PATH から
dnl exec し、無いとプロセスグループごと SIGQUIT で落ちる。srctools を PATH に足す。
TZ_SIMTOOLS=
test -n "$Z80PACK_DIR" && TZ_SIMTOOLS=$Z80PACK_DIR/cpmsim/srctools
AC_PATH_PROG([TZ_RECEIVE], [receive], [], [$PATH:$TZ_SIMTOOLS])
if test -z "$TZ_RECEIVE"; then
  if test -n "$TZ_SIMTOOLS"; then
    tz_warn="$tz_warn$tz_nl  - receive(cpmsim が起動時に使う)がまだ無い → make sims で作れる"
  else
    tz_warn="$tz_warn$tz_nl  - receive(z80pack の cpmsim/srctools)が PATH に無い → cpmsim が起動直後に落ちる"
  fi
fi
for tz_s in z80pack/cpmsim z80board/z80boardsim; do
  if test ! -x "$srcdir/arch/$tz_s"; then
    if test -n "$Z80PACK_DIR"; then
      tz_warn="$tz_warn$tz_nl  - arch/$tz_s が無い → configure の後に make sims で作れる"
    else
      tz_warn="$tz_warn$tz_nl  - arch/$tz_s が無い → ビルドは可、run と回帰は不可(--with-z80pack=DIR を指定すれば make sims で作れる)"
    fi
  fi
done

dnl ------------------------------------------------------------------
dnl 結果
dnl ------------------------------------------------------------------
TZ_EXTRA_PATH=
for tz_d in "$SDCC_BIN" "$M68K_BIN" "$TZ_SIMTOOLS"; do
  test -n "$tz_d" || continue
  if test -z "$TZ_EXTRA_PATH"; then TZ_EXTRA_PATH=$tz_d; else TZ_EXTRA_PATH=$TZ_EXTRA_PATH:$tz_d; fi
done

if test -n "$tz_warn"; then
  AC_MSG_WARN([次は無くても続行する:$tz_warn])
fi
if test -n "$tz_missing"; then
  AC_MSG_ERROR([足りないもの(全部):$tz_missing
入れてから ./configure をやり直す。])
fi

AC_SUBST([SDCC_BIN])
AC_SUBST([M68K_BIN])
AC_SUBST([Z80PACK_DIR])
AC_SUBST([TZ_EXTRA_PATH])
AC_CONFIG_FILES([config.mk])
AC_OUTPUT

echo
echo "config.mk を書いた。次は:"
echo "    make ARCH=z80pack        (既定。z80board / m68k-mega も同じ形)"
test -n "$Z80PACK_DIR" && echo "    make sims                (cpmsim / z80boardsim をフォークから作って arch/ へ置く)"
echo "    sh python/run_regress.sh --build"
