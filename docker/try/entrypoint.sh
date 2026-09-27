#!/bin/sh
# tizix-try-entry: 引数なしなら sshd を前景で起動(tizix ユーザーで ssh ログインする使い方)。
#   引数があればそれを tizix ユーザーとしてホームで実行する(docker run -it … tizix m68k など)。
set -e
if [ $# -eq 0 ]; then
    # ホスト鍵はイメージに焼かず、コンテナごとに作る
    ssh-keygen -A >/dev/null
    mkdir -p /run/sshd
    exec /usr/sbin/sshd -D -e
fi
cd /home/tizix
exec runuser -u tizix -- env HOME=/home/tizix USER=tizix LOGNAME=tizix SHELL=/bin/bash "$@"
