#ifndef TIZIX_H
#define TIZIX_H

/* path のアセンブリを in-place で Tizix 用 IY 相対 PIC へ変換する。成功 0 / 失敗 1。 */
int tizix_iy_transform(const char *path);

#endif
