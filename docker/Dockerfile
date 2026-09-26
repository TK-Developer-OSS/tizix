# tizix ビルド・回帰用イメージ(#74)。ベースは Ubuntu 24.04 LTS(TK 2026-09-26)。
#
#   作り方(tizix のルートで): sh docker/build.sh
#   使い方: docker run --rm -v "$PWD":/tizix tizix                 … 3 アーキをビルドして回帰
#           docker run --rm -v "$PWD":/tizix tizix make ARCH=z80board
#   (podman / SELinux 環境では -v "$PWD":/tizix:Z)
#
#   イメージに入るのはビルド環境一式(ツールとシミュレータ)だけ。tizix のソースは入れず、
#   実行時に /tizix へマウントする(手元のツリーをそのままビルドする)。
#
#   固定しているもの(版ずれで苦しんだので、全部ハッシュで止める):
#     SDCC 4.5.0(公式バイナリ配布物)/ binutils 2.30 + gcc 8.3.0(m68k-elf)/
#     z80pack-tizix フォーク(build.sh が固定コミットを git archive で渡す)/ rocket68(同梱コピー)

# ---------------------------------------------------------------- builder
FROM docker.io/library/ubuntu:24.04 AS builder
ENV DEBIAN_FRONTEND=noninteractive

# gcc 8.3.0 / binutils 2.30 は古いソースなので、24.04 標準の gcc 13 ではなく gcc-11 で作る
# (開発機 Rocky 9 と同じ系統のホストコンパイラ。実績のある組み合わせ)。
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential gcc-11 g++-11 bison flex texinfo \
        libgmp-dev libmpfr-dev libmpc-dev \
        curl ca-certificates bzip2 xz-utils file && \
    rm -rf /var/lib/apt/lists/*

ARG SDCC_SHA256=3395722e3f8a31ce6a14c75fc419f586a3573b939f85efee5c88cdd35820a240
ARG BINUTILS_SHA256=8c3850195d1c093d290a716e20ebcaa72eda32abf5e3d8611154b39cff79e9ea
ARG GCC_SHA256=ea71adc1c3d86330874b8df19611424b143308f0d6612d542472600532c96d2d

WORKDIR /build

# ---- SDCC 4.5.0: 公式の Linux amd64 バイナリ配布物(開発機 rocky9 の ~/z80pack/sdcc/4.5.0 と同一)。
#   ソースからは作らない: 4.5.0 のソースは boost 1.79 未満を拒否する(SDCC bug #3772)など、
#   ホストに左右される。バイナリ配布物なら開発機とバイト単位で同じものになる。
RUN curl -sSL -o sdcc.tar.bz2 https://sourceforge.net/projects/sdcc/files/sdcc-linux-amd64/4.5.0/sdcc-4.5.0-amd64-unknown-linux2.5.tar.bz2/download && \
    echo "$SDCC_SHA256  sdcc.tar.bz2" | sha256sum -c - && \
    mkdir -p /opt/sdcc && tar xjf sdcc.tar.bz2 -C /opt/sdcc --strip-components=1 --no-same-owner && \
    rm sdcc.tar.bz2 && /opt/sdcc/bin/sdcc -v | head -1

# ---- m68k-elf: binutils 2.30 + gcc 8.3.0(開発機 rocky9 の ~/m68k と同じ configure)
RUN curl -sSL -o binutils.tar.gz https://ftp.gnu.org/gnu/binutils/binutils-2.30.tar.gz && \
    echo "$BINUTILS_SHA256  binutils.tar.gz" | sha256sum -c - && \
    tar xzf binutils.tar.gz && mkdir build-binutils && cd build-binutils && \
    CC=gcc-11 CXX=g++-11 ../binutils-2.30/configure --target=m68k-elf --prefix=/opt/m68k \
        --disable-nls --disable-werror && \
    make -j"$(nproc)" && make install && \
    cd .. && rm -rf binutils-2.30 build-binutils binutils.tar.gz
ENV PATH=/opt/m68k/bin:$PATH
RUN curl -sSL -o gcc.tar.gz https://ftp.gnu.org/gnu/gcc/gcc-8.3.0/gcc-8.3.0.tar.gz && \
    echo "$GCC_SHA256  gcc.tar.gz" | sha256sum -c - && \
    tar xzf gcc.tar.gz && mkdir build-gcc && cd build-gcc && \
    CC=gcc-11 CXX=g++-11 ../gcc-8.3.0/configure --target=m68k-elf --prefix=/opt/m68k \
        --disable-nls --disable-threads --disable-shared --disable-libssp \
        --enable-languages=c --without-headers --with-newlib --disable-werror && \
    make -j"$(nproc)" all-gcc all-target-libgcc && make install-gcc install-target-libgcc && \
    cd .. && rm -rf gcc-8.3.0 build-gcc gcc.tar.gz

# ---- z80pack-tizix フォーク(cpmsim / z80boardsim / receive)。cpmsim は -static でリンクする
#   (Ubuntu は libc6-dev に libc.a が入っている)。
COPY z80pack-tizix.tar /build/
RUN tar xf z80pack-tizix.tar --no-same-owner -C /opt && rm z80pack-tizix.tar && \
    make -C /opt/z80pack-tizix/cpmsim/srcsim && \
    rm -f /opt/z80pack-tizix/cpmsim/srctools/receive && \
    make -C /opt/z80pack-tizix/cpmsim/srctools receive && \
    make -C /opt/z80pack-tizix/z80boardsim/srcsim && \
    ls -l /opt/z80pack-tizix/cpmsim/cpmsim /opt/z80pack-tizix/z80boardsim/z80boardsim \
          /opt/z80pack-tizix/cpmsim/srctools/receive && \
    test -x /opt/z80pack-tizix/cpmsim/srctools/receive
# ↑ receive はフォークにビルド済みバイナリが実行権なし(644)でコミットされていて、make が
#   「最新」と見て作り直さず、cpmsim が exec できなかった。消してから作る。

# ---- rocket68(m68ksim の CPU コア。MIT、github.com/habedi/rocket68 の手元コピー)
COPY rocket68.tar /build/
RUN mkdir -p /opt/rocket68 && tar xf rocket68.tar --no-same-owner -C /opt/rocket68 && rm rocket68.tar && \
    make -C /opt/rocket68 static CC=gcc && ls -l /opt/rocket68/lib/librocket68.a

# ---- 削る。上の gcc の段を作り直さないよう最後に別の RUN で。
#   m68k: gcc / binutils はデバッグ情報付きでできる(cc1 だけで 200MB 超)→ strip。文書と plugin ヘッダも不要。
#   SDCC: 使うのは z80 だけ。non-free(pic 用 152MB)・他ポートのライブラリ・文書を外す。
#   rocket68: m68ksim のビルドに要るのは src / include / Makefile / lib と LICENSE。
RUN find /opt/m68k/bin /opt/m68k/libexec /opt/m68k/m68k-elf/bin -type f -perm -u+x \
        -exec sh -c 'file -b "$1" | grep -q "^ELF" && strip --strip-unneeded "$1"' _ {} \; && \
    rm -rf /opt/m68k/share /opt/m68k/lib/gcc/m68k-elf/8.3.0/plugin && \
    cd /opt/sdcc/share/sdcc && rm -rf non-free doc && \
    find lib -mindepth 1 -maxdepth 1 ! -name z80 -exec rm -rf {} + && \
    cd /opt/rocket68 && rm -rf docs external benches tests obj .github && \
    du -sh /opt/*

# ---------------------------------------------------------------- runtime
FROM docker.io/library/ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive

# tzcc(ホスト gcc)・ディスク像(mtools / mkfs.fat)・回帰(python3)に要るものだけ。
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc libc6-dev make python3 mtools dosfstools file procps bzip2 && \
    rm -rf /var/lib/apt/lists/*

COPY --from=builder /opt /opt
COPY entrypoint.sh /usr/local/bin/tizix-entry
RUN chmod +x /usr/local/bin/tizix-entry

# cpmsim は起動時に receive を PATH から exec する(無いと落ちる)
ENV PATH=/opt/sdcc/bin:/opt/m68k/bin:/opt/z80pack-tizix/cpmsim/srctools:$PATH \
    MTOOLS_SKIP_CHECK=1

ARG FORK_REV=unknown
LABEL org.opencontainers.image.title="tizix build/regress" \
      tizix.z80pack-tizix.rev="$FORK_REV"

WORKDIR /tizix
ENTRYPOINT ["/usr/local/bin/tizix-entry"]
CMD ["sh", "python/run_regress.sh", "--build"]
