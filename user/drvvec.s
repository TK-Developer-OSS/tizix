;===================================================================
; drvvec.s - DRIVER.BIN 先頭ジャンプベクタ表
;
;   string.h / stdlib.h の純粋関数(旧 drv_tbl[19..37])は user/string.c,
;   user/stdlib.c へ分離し、コマンドが string.rel / stdlib.rel をリンクする方式に
;   変更した(driver 実体 4KB 超過の恒久対策)。よってこの表は stdio.h 分
;   (drv_tbl[0..18])のみを保持する。
;===================================================================
        .module drvvec
        .area _CODE

        ; stdio.h (0..18)
        .globl _drv_putc, _drv_getc, _drv_printf
        .globl _drv_fopen, _drv_fwrite, _drv_fclose
        .globl _drv_fread, _drv_fputs, _drv_fgets
        .globl _drv_kbhit, _drv_getc_timeout
        .globl _drv_fseek, _drv_ftell, _drv_feof
        .globl _drv_ferror, _drv_fflush, _drv_fgetc
        .globl _drv_fputc, _drv_puts
        ; sched (19..20) / dir 反復 (21..23) / FS 書込み (24..26)。カーネル本体の
        ; 関数を直接指す(アドレスは Makefile の FS_SYMS が kernel.map から -g で
        ; 渡す。DRIVER にコード無し)
        .globl _proc_block, _proc_wake
        .globl _kdir_open, _kdir_read, _kdir_close
        .globl _kfs_mkdir, _kfs_unlink, _kfs_rename
        .globl _kdir_size
        ; 外部 sh(user/sh.c → /bin/sh.bin)が使うカーネル入口 (27..37)。同上。
        .globl _kexec_argv
        .globl _builtin_try, _builtin_is
        .globl _redir_begin, _redir_end, _in_begin, _in_end
        .globl _pipe_setup, _pipe_attach_writer, _pipe_teardown, _pipe_note_exit
        .globl _pipe_ovf, _pipe_tail, _krun_pipe   ; #27: 4KB ブロックパイプ
        .globl _kchdir, _kgetcwd                   ; #28: カーネル cwd (cd/pwd)
        .globl _con_break                          ; #33: Ctrl+C 検出(取りこぼさない)
        .globl _krun_wait                          ; #35: 子を起動して待つ(同期 exec)
        .globl _klog_write                         ; rsyslog: /var/log/message へ 1 行追記
        .globl _kfs_df                             ; #56: /bin/df 用の容量(総/空き KB)
        .globl _drv_conraw                         ; 端末の生モード(rx の xmodem 受信。driver.c)

; 関数ポインタテーブル（先頭アドレス 0x9000）
_drv_tbl::
        .dw _drv_putc          ; 0x9000: drv_tbl[0]  putchar
        .dw _drv_getc          ; 0x9002: drv_tbl[1]  getchar
        .dw _drv_printf        ; 0x9004: drv_tbl[2]  printf
        .dw _drv_fopen         ; 0x9006: drv_tbl[3]  fopen
        .dw _drv_fwrite        ; 0x9008: drv_tbl[4]  fwrite
        .dw _drv_fclose        ; 0x900A: drv_tbl[5]  fclose
        .dw _drv_fread         ; 0x900C: drv_tbl[6]  fread
        .dw _drv_fputs         ; 0x900E: drv_tbl[7]  fputs
        .dw _drv_fgets         ; 0x9010: drv_tbl[8]  fgets
        .dw _drv_kbhit         ; 0x9012: drv_tbl[9]  kbhit
        .dw _drv_getc_timeout  ; 0x9014: drv_tbl[10] getc_timeout
        .dw _drv_fseek         ; 0x9016: drv_tbl[11] fseek
        .dw _drv_ftell         ; 0x9018: drv_tbl[12] ftell
        .dw _drv_feof          ; 0x901A: drv_tbl[13] feof
        .dw _drv_ferror        ; 0x901C: drv_tbl[14] ferror
        .dw _drv_fflush        ; 0x901E: drv_tbl[15] fflush
        .dw _drv_fgetc         ; 0x9020: drv_tbl[16] fgetc
        .dw _drv_fputc         ; 0x9022: drv_tbl[17] fputc
        .dw _drv_puts          ; 0x9024: drv_tbl[18] puts
        .dw _proc_block        ; 0x9026: drv_tbl[19] proc_block  (カーネル本体を直接)
        .dw _proc_wake         ; 0x9028: drv_tbl[20] proc_wake   (カーネル本体を直接)
        .dw _kdir_open         ; 0x902A: drv_tbl[21] opendir
        .dw _kdir_read         ; 0x902C: drv_tbl[22] readdir
        .dw _kdir_close        ; 0x902E: drv_tbl[23] closedir
        .dw _kfs_mkdir         ; 0x9030: drv_tbl[24] mkdir  (カーネル本体を直接)
        .dw _kfs_unlink        ; 0x9032: drv_tbl[25] unlink (rm)
        .dw _kfs_rename        ; 0x9034: drv_tbl[26] rename (mv)
        .dw _kexec_argv        ; 0x9036: drv_tbl[27] kexec_argv(fname, argpack, argc)
        .dw _builtin_try       ; 0x9038: drv_tbl[28] builtin_try(cmd, arg)
        .dw _builtin_is        ; 0x903A: drv_tbl[29] builtin_is(cmd)
        .dw _redir_begin       ; 0x903C: drv_tbl[30] redir_begin(fname)   > file
        .dw _redir_end         ; 0x903E: drv_tbl[31] redir_end()
        .dw _in_begin          ; 0x9040: drv_tbl[32] in_begin(fname)      < file
        .dw _in_end            ; 0x9042: drv_tbl[33] in_end()
        .dw _pipe_setup        ; 0x9044: drv_tbl[34] pipe_setup(rblk)
        .dw _pipe_attach_writer ; 0x9046: drv_tbl[35] pipe_attach_writer(wblk)
        .dw _pipe_teardown     ; 0x9048: drv_tbl[36] pipe_teardown()
        .dw _pipe_note_exit    ; 0x904A: drv_tbl[37] pipe_note_exit(blk)
        .dw _kdir_size         ; 0x904C: drv_tbl[38] kdir_size() 直前 readdir のサイズ
        .dw _pipe_ovf          ; 0x904E: drv_tbl[39] pipe_ovf()  #27 4KB 超で 1
        .dw _pipe_tail         ; 0x9050: drv_tbl[40] pipe_tail(want) #27 パイプ後段 tail
        .dw _krun_pipe         ; 0x9052: drv_tbl[41] krun_pipe(ln,lp,lc,rn,rp,rc) #27
        .dw _kchdir            ; 0x9054: drv_tbl[42] kchdir(path)  #28 カーネル cwd
        .dw _kgetcwd           ; 0x9056: drv_tbl[43] kgetcwd(out)  #28 カーネル cwd
        .dw _con_break         ; 0x9058: drv_tbl[44] con_break()   #33 Ctrl+C 検出
        .dw _krun_wait         ; 0x905A: drv_tbl[45] krun_wait(f,pack,argc) #35 同期 exec
        .dw _klog_write        ; 0x905C: drv_tbl[46] klog_write(msg) rsyslog: /var/log/message 追記
        .dw _kfs_df            ; 0x905E: drv_tbl[47] kfs_df(sel) #56 /bin/df(0=総 1=空き KB)
        .dw _drv_conraw        ; 0x9060: drv_tbl[48] drv_conraw(on) 端末の生モード(rx)
