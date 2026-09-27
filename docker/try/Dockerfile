# tizix お試しイメージ。ビルド済みのカーネル・ディスク像・シミュレータだけを入れ、
# clone もビルドも要らずに docker コマンドだけで遊べるようにする。
#
#   作り方(tizix のルートで): sh docker/try/build.sh
#     ビルド用イメージ tizix(docker/Dockerfile)の上で 3 アーキを make し、
#     成果物だけを軽い Ubuntu に移す。コンパイラ類はこちらには入らない。
#
#   使い方:
#     docker run -it --rm tizix-try tizix            … z80pack を起動(tizix m68k / tizix z80board も)
#     docker run -d --name tizix -p 10022:22 tizix-try … 常駐させて ssh で入る(tizix / tizix)

# ---------------------------------------------------------------- build
FROM tizix AS build
COPY src.tar /tmp/
# ビルド用イメージの entrypoint と同じく、シミュレータと rocket68 をツリーへ置いてから make
RUN mkdir -p /work && tar xf /tmp/src.tar --no-same-owner -C /work && rm /tmp/src.tar && \
    cd /work && F=/opt/z80pack-tizix && \
    cp "$F/cpmsim/cpmsim" "$F/cpmsim/srctools/receive" arch/z80pack/ && \
    cp "$F/z80boardsim/z80boardsim" arch/z80board/ && \
    mkdir -p arch/m68k-mega/rocket68 && cp -R /opt/rocket68/. arch/m68k-mega/rocket68/ && \
    make && make ARCH=z80board && make ARCH=m68k-mega && \
    ls -l arch/z80pack/disks/drivea.dsk arch/z80pack/disks/driveb.dsk arch/z80board/sdcard.img \
          build/arch/z80board/obj/kernel.ihx build/arch/m68k-mega/obj/kernel.bin \
          build/arch/m68k-mega/obj/disk.img arch/m68k-mega/m68ksim

# ---------------------------------------------------------------- runtime
FROM docker.io/library/ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive

# シミュレータは libc だけで動く。要るのは ssh ログイン用の sshd くらい。
RUN apt-get update && apt-get install -y --no-install-recommends openssh-server && \
    rm -rf /var/lib/apt/lists/* && rm -f /etc/ssh/ssh_host_* && \
    useradd -m -s /bin/bash tizix && echo tizix:tizix | chpasswd

COPY --from=build --chown=tizix:tizix /work /home/tizix/tizix
COPY tizix /usr/local/bin/tizix
COPY entrypoint.sh /usr/local/bin/tizix-try-entry
COPY README.txt /home/tizix/README.txt
# 配布時のディスク像を .pristine に控える(tizix reset がここから戻す)。
# README は ssh ログイン時の motd にも出す。
RUN chmod +x /usr/local/bin/tizix /usr/local/bin/tizix-try-entry && \
    cd /home/tizix/tizix && \
    for f in arch/z80pack/disks/drivea.dsk arch/z80pack/disks/driveb.dsk \
             arch/z80board/sdcard.img build/arch/m68k-mega/obj/disk.img; do \
        mkdir -p ".pristine/$(dirname "$f")" && cp "$f" ".pristine/$f"; \
    done && \
    chown -R tizix:tizix /home/tizix && \
    cp /home/tizix/README.txt /etc/motd

EXPOSE 22
ENTRYPOINT ["/usr/local/bin/tizix-try-entry"]
