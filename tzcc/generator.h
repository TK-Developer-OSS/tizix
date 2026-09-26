#ifndef GENERATOR_H
#define GENERATOR_H

#include "parser.h"

void generate_asm(Node *node, FILE *out);
void generate_x86(Node *root, FILE *out);   /* x86-64 バックエンド (gen_x86.c) */

#endif
