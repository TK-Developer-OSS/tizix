#ifndef GENERATOR_H
#define GENERATOR_H

#include "parser.h"

void generate_asm(Node *node, FILE *out);
void generate_x86(Node *root, FILE *out);   /* x86-64 バックエンド (gen_x86.c) */
int ia16_is_narrowfunc(const char *n);        /* main.c: callee declared to return a plain char (ia16 only) */
void generate_ia16(Node *root, FILE *out);  /* x86 16bit バックエンド PoC (gen_ia16.c) */

#endif
