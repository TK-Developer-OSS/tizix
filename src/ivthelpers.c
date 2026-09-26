/*
 * ivthelpers.c  --  tizix 独自ランタイムヘルパ (ABI: --sdcccall 0)
 *
 *  カーネルを全域 --sdcccall 0 に統一すると、SDCC 標準ライブラリ(z80.lib,
 *  sdcccall(1)=レジスタ渡し) の整数 div/mod/mul・memcmp・strcmp と呼出規約
 *  が食い違いリンクで衝突する。ここに sdcccall(0) 規約の実装を自前で置き、
 *  標準ライブラリの該当モジュールを一切引かせない(GPLソース非持込)。
 *
 *  命名: C名 _divuint(下線1) → asm __divuint(下線2)。codegen の call 先に
 *        一致。シグネチャは SDCC 組込みプロトタイプに一致必須(char版は
 *        返り値が int/unsigned int に昇格)。
 *  実装: '/','%','*' と可変長シフトを使わず、シフト減算/シフト加算で構成
 *        (使うと自分自身や別の内部ヘルパを呼ぶため)。char版は int版へ委譲。
 */
typedef unsigned char  u8;
typedef unsigned int   u16;
typedef unsigned long  u32;

/* 16bit unsigned 除算/剰余(復元法,16反復) */
unsigned int _divuint(unsigned int a, unsigned int b){
	u16 rem=0; u8 i=16;
	do { rem=(u16)(rem<<1)|(u16)(a>>15); a=(u16)(a<<1);
	     if(rem>=b){rem=(u16)(rem-b); a|=1;} } while(--i);
	return a;
}
unsigned int _moduint(unsigned int a, unsigned int b){
	u16 rem=0; u8 i=16;
	do { rem=(u16)(rem<<1)|(u16)(a>>15); a=(u16)(a<<1);
	     if(rem>=b){rem=(u16)(rem-b); a|=1;} } while(--i);
	return rem;
}
/* 16bit signed: 符号を外して unsigned へ委譲 */
int _divsint(int a, int b){
	u8 neg=0; u16 ua,ub,uq;
	if(a<0){ua=(u16)(-a);neg^=1;}else ua=(u16)a;
	if(b<0){ub=(u16)(-b);neg^=1;}else ub=(u16)b;
	uq=_divuint(ua,ub);
	return neg?-(int)uq:(int)uq;
}
int _modsint(int a, int b){
	u8 neg=0; u16 ua,ub,ur;
	if(a<0){ua=(u16)(-a);neg=1;}else ua=(u16)a;   /* 剰余符号=被除数 */
	ub=(b<0)?(u16)(-b):(u16)b;
	ur=_moduint(ua,ub);
	return neg?-(int)ur:(int)ur;
}
/* 16bit multiply(シフト加算,低16bitは符号非依存) */
int _mulint(int a, int b){
	u16 ua=(u16)a,ub=(u16)b,r=0;
	while(ub){ if(ub&1)r=(u16)(r+ua); ua=(u16)(ua<<1); ub=(u16)(ub>>1); }
	return (int)r;
}
/* 32bit unsigned 除算/剰余(復元法,32反復) */
unsigned long _divulong(unsigned long a, unsigned long b){
	u32 rem=0; u8 i=32;
	do { rem=(u32)(rem<<1); if(a&0x80000000UL)rem|=1; a=(u32)(a<<1);
	     if(rem>=b){rem=(u32)(rem-b); a|=1;} } while(--i);
	return a;
}
unsigned long _modulong(unsigned long a, unsigned long b){
	u32 rem=0; u8 i=32;
	do { rem=(u32)(rem<<1); if(a&0x80000000UL)rem|=1; a=(u32)(a<<1);
	     if(rem>=b){rem=(u32)(rem-b); a|=1;} } while(--i);
	return rem;
}
/* 32bit multiply(シフト加算) */
long _mullong(long a, long b){
	u32 ua=(u32)a,ub=(u32)b,r=0;
	while(ub){ if(ub&1)r=(u32)(r+ua); ua=(u32)(ua<<1); ub=(u32)(ub>>1); }
	return (long)r;
}
/* char 変種: int 版へ委譲(返り値昇格は SDCC 規約どおり) */
unsigned int _divuchar(unsigned char a, unsigned char b){ return _divuint((u16)a,(u16)b); }
unsigned int _moduchar(unsigned char a, unsigned char b){ return _moduint((u16)a,(u16)b); }
int _divschar(signed char a, signed char b){ return _divsint((int)a,(int)b); }
int _modschar(signed char a, signed char b){ return _modsint((int)a,(int)b); }
unsigned int _muluchar(unsigned char a, unsigned char b){ return (unsigned int)_mulint((int)(u16)a,(int)(u16)b); }
int _mulschar(signed char a, signed char b){ return _mulint((int)a,(int)b); }

/* memcmp / strcmp */
int memcmp(const void *s1, const void *s2, u16 n){
	const u8 *p1=(const u8*)s1,*p2=(const u8*)s2;
	while(n){ if(*p1!=*p2) return (int)*p1-(int)*p2; ++p1;++p2;--n; }
	return 0;
}
int strcmp(const char *s1, const char *s2){
	while(*s1 && (*s1==*s2)){ ++s1; ++s2; }
	return (int)(u8)*s1-(int)(u8)*s2;
}
