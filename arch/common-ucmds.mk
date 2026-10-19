# arch/common-ucmds.mk -- 外部コマンド(共有 user/<cmd>.c)の一覧。全 gcc 系アーキ(common-gcc.mk)と
# x86-ia16 の Makefile が読む。コマンドを足すときはここに 1 つ足す。
UCMDS_BASE = ls echo cat head tail rm touch cp wc hello grep sed uniq tee whoami uname \
             date du mkdir mv rmdir sleep id a b dd vi rsyslog df uptime free history \
             ptx prx sh rx mbtest tzsh
