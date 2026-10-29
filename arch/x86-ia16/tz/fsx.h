/* fsx.h -- the tzcc kernel reaches the shared FatFs stack (ia16-gcc code, see fsif.c) only through the trampolines
 * T_<name> of glue.S (fs_tramp): they switch to the file system stack (SS = DS) and stop preemption for the call.
 * Include after kw.h, before any prototype of these functions. */
#define vfs_resolve     T_vfs_resolve
#define vfs_init        T_vfs_init
#define fat_init        T_fat_init
#define dev_open        T_dev_open
#define dev_read        T_dev_read
#define dev_write       T_dev_write
#define fsb_open        T_fsb_open
#define fsb_close       T_fsb_close
#define fsb_read        T_fsb_read
#define fsb_write       T_fsb_write
#define fsb_lseek       T_fsb_lseek
#define fsb_seek_hi     T_fsb_seek_hi
#define fsb_unlink      T_fsb_unlink
#define fsb_mkdir       T_fsb_mkdir
#define fsb_rename      T_fsb_rename   /* fsif_rename: the mv semantics */
#define kfs_df          T_kfs_df
#define kdir_open_abs   T_kdir_open_abs
#define kdir_read       T_kdir_read
#define kdir_size       T_kdir_size
#define kdir_close      T_kdir_close
#define kpath           T_kpath
#define kchdir          T_kchdir
#define kgetcwd         T_kgetcwd
#define klog_write      T_klog_write
#define redir_begin     T_redir_begin
#define redir_end       T_redir_end
#define redir_sink      T_redir_sink
#define in_begin        T_in_begin
#define in_end          T_in_end
#define in_src          T_in_src
#define f_open          T_f_open
#define f_read          T_f_read
#define f_close         T_f_close
