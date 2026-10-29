/* arch/x86-ia16/fat12min.h -- 最小の FAT12 実装(fat12min.c)の FatFs 互換の型と API。
 *   src/ff.h が FAT12_MIN のとき、これだけを読む(FatFs 本体 ff.c は載せない)。使う範囲は src/ の kexec / fatcmd /
 *   fsbackend_fat が呼ぶ FatFs の部分集合: f_mount / f_open / f_close / f_read / f_write / f_lseek / f_sync /
 *   f_opendir / f_readdir / f_closedir / f_unlink / f_mkdir / f_rename / f_getfree。8.3 名(長い名前なし)、
 *   FAT12 のみ、1 ボリューム、パーティションなし(スーパーフロッピー)。値は FatFs と同じにしてある(FRESULT は
 *   fatcmd が数値のまま返すので 0..19 を揃える)。 */
#ifndef FAT12MIN_H
#define FAT12MIN_H

typedef unsigned char  BYTE;
typedef unsigned int   WORD;      /* ia16: int = 16 bit */
typedef unsigned int   UINT;
typedef unsigned long  DWORD;
typedef DWORD          LBA_t;
typedef DWORD          FSIZE_t;
typedef char           TCHAR;

typedef enum {
	FR_OK = 0, FR_DISK_ERR, FR_INT_ERR, FR_NOT_READY, FR_NO_FILE, FR_NO_PATH, FR_INVALID_NAME, FR_DENIED,
	FR_EXIST, FR_INVALID_OBJECT, FR_WRITE_PROTECTED, FR_INVALID_DRIVE, FR_NOT_ENABLED, FR_NO_FILESYSTEM,
	FR_MKFS_ABORTED, FR_TIMEOUT, FR_LOCKED, FR_NOT_ENOUGH_CORE, FR_TOO_MANY_OPEN_FILES, FR_INVALID_PARAMETER
} FRESULT;

#define FA_READ           0x01
#define FA_WRITE          0x02
#define FA_OPEN_EXISTING  0x00
#define FA_CREATE_NEW     0x04
#define FA_CREATE_ALWAYS  0x08
#define FA_OPEN_ALWAYS    0x10
#define FA_OPEN_APPEND    0x30

#define AM_RDO 0x01
#define AM_HID 0x02
#define AM_SYS 0x04
#define AM_DIR 0x10
#define AM_ARC 0x20

typedef struct {
	BYTE  csize;          /* 1 クラスタのセクタ数(2 のべき乗) */
	BYTE  cshift;         /* log2(csize) */
	BYTE  nfats;
	WORD  n_rootdir;      /* ルートディレクトリのエントリ数 */
	DWORD n_fatent;       /* クラスタ数 + 2 */
	DWORD fatsz;          /* 1 つの FAT のセクタ数 */
	DWORD fatbase, dirbase, database;
	DWORD hint;           /* 空きクラスタ探索の開始位置 */
} FATFS;

typedef struct {
	DWORD fptr;           /* 読み書き位置 */
	struct { DWORD objsize; } obj;
	DWORD sclust;         /* 先頭クラスタ(0 = 空) */
	DWORD clust;          /* fptr を含む(または直前の)クラスタ */
	DWORD cidx;           /* clust が先頭から何番目か */
	DWORD dcl;            /* ディレクトリエントリの置き場所: そのディレクトリの先頭クラスタ(0 = ルート)と */
	WORD  didx;           /*   エントリ番号 */
	BYTE  flag;           /* FA_READ / FA_WRITE と 0x40 = 変更あり */
	BYTE  err;
} FIL;

typedef struct {
	DWORD sclust;         /* 0 = ルート */
	WORD  idx;
} DIR;

typedef struct {
	DWORD fsize;
	WORD  fdate, ftime;
	BYTE  fattrib;
	TCHAR fname[13];
} FILINFO;

FRESULT f_mount (FATFS *fs, const TCHAR *path, BYTE opt);
FRESULT f_open (FIL *fp, const TCHAR *path, BYTE mode);
FRESULT f_close (FIL *fp);
FRESULT f_read (FIL *fp, void *buff, UINT btr, UINT *br);
FRESULT f_write (FIL *fp, const void *buff, UINT btw, UINT *bw);
FRESULT f_lseek (FIL *fp, FSIZE_t ofs);
FRESULT f_sync (FIL *fp);
FRESULT f_opendir (DIR *dp, const TCHAR *path);
FRESULT f_closedir (DIR *dp);
FRESULT f_readdir (DIR *dp, FILINFO *fno);
FRESULT f_unlink (const TCHAR *path);
FRESULT f_mkdir (const TCHAR *path);
FRESULT f_rename (const TCHAR *path_old, const TCHAR *path_new);
FRESULT f_getfree (const TCHAR *path, DWORD *nclst, FATFS **fatfs);

#define f_eof(fp)   ((int)((fp)->fptr == (fp)->obj.objsize))
#define f_error(fp) ((fp)->err)
#define f_tell(fp)  ((fp)->fptr)
#define f_size(fp)  ((fp)->obj.objsize)
#define f_rewind(fp) f_lseek((fp), 0)

#endif
