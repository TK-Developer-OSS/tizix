#include <stdio.h>
#include <stdlib.h>
#include "parser.h"

Node* new_node(NodeType type, char *value) {
    Node *node = malloc(sizeof(Node));
    node->type = type;
    node->value = value;
    node->array_size = 0;
    node->base_type = 0;
    node->is_long = 0;
    node->esz = 0;
    node->left = NULL;    /* tzcc 自身は連鎖代入 a=b=c を未サポート */
    node->right = NULL;
    node->third = NULL;
    node->fourth = NULL;
    node->next = NULL;
    return node;
}

void add_child(Node *parent, Node *child) {
    if (!parent->left) parent->left = child;
    else {
        Node *n = parent->left;
        while (n->next) n = n->next;
        n->next = child;
    }
}

void print_ast(Node *node, int depth) {
    if (!node) return;
    for (int i = 0; i < depth; i++) printf("  ");
    const char *type_name = "UNKNOWN";
    switch(node->type) {
        case NODE_ROOT: type_name = "ROOT"; break;
        case NODE_INCLUDE: type_name = "INCLUDE"; break;
        case NODE_FUNC: type_name = "FUNC"; break;
        case NODE_BLOCK: type_name = "BLOCK"; break;
        case NODE_EXPR_STMT: type_name = "EXPR_STMT"; break;
        case NODE_RETURN: type_name = "RETURN"; break;
        case NODE_CALL: type_name = "CALL"; break;
        case NODE_VAR_DECL: type_name = "VAR_DECL"; break;
        case NODE_ARRAY_DECL: type_name = "ARRAY_DECL"; break;
        case NODE_PTR_DECL: type_name = "PTR_DECL"; break;
        case NODE_ASSIGN: type_name = "ASSIGN"; break;
        case NODE_IDENTIFIER: type_name = "IDENTIFIER"; break;
        case NODE_NUMBER: type_name = "NUMBER"; break;
        case NODE_STRING: type_name = "STRING"; break;
        case NODE_ARG: type_name = "ARG"; break;
        case NODE_ADD: type_name = "ADD"; break;
        case NODE_SUB: type_name = "SUB"; break;
        case NODE_MUL: type_name = "MUL"; break;
        case NODE_DIV: type_name = "DIV"; break;
        case NODE_EQ: type_name = "EQ"; break;
        case NODE_NE: type_name = "NE"; break;
        case NODE_LT: type_name = "LT"; break;
        case NODE_GT: type_name = "GT"; break;
        case NODE_LE: type_name = "LE"; break;
        case NODE_GE: type_name = "GE"; break;
        case NODE_NOT: type_name = "NOT"; break;
        case NODE_NEG: type_name = "NEG"; break;
        case NODE_IF: type_name = "IF"; break;
        case NODE_WHILE: type_name = "WHILE"; break;
        case NODE_FOR: type_name = "FOR"; break;
        case NODE_INDEX: type_name = "INDEX"; break;
        case NODE_STORE_INDEX: type_name = "STORE_INDEX"; break;
        case NODE_DEREF: type_name = "DEREF"; break;
        case NODE_STORE_DEREF: type_name = "STORE_DEREF"; break;
        case NODE_ADDR: type_name = "ADDR"; break;
        case NODE_PREINC: type_name = "PREINC"; break;
        case NODE_PREDEC: type_name = "PREDEC"; break;
        case NODE_POSTINC: type_name = "POSTINC"; break;
        case NODE_POSTDEC: type_name = "POSTDEC"; break;
        case NODE_AND: type_name = "AND"; break;
        case NODE_OR: type_name = "OR"; break;
        case NODE_BITAND: type_name = "BITAND"; break;
        case NODE_BITOR: type_name = "BITOR"; break;
        case NODE_BITXOR: type_name = "BITXOR"; break;
        case NODE_BITNOT: type_name = "BITNOT"; break;
        case NODE_SHL: type_name = "SHL"; break;
        case NODE_SHR: type_name = "SHR"; break;
        case NODE_MEMBER: type_name = "MEMBER"; break;
        case NODE_STORE_MEMBER: type_name = "STORE_MEMBER"; break;
        case NODE_TERNARY: type_name = "TERNARY"; break;
        case NODE_CVT: type_name = "CVT"; break;
        case NODE_INITLIST: type_name = "INITLIST"; break;
        case NODE_BREAK: type_name = "BREAK"; break;
        case NODE_CONTINUE: type_name = "CONTINUE"; break;
        case NODE_SWITCH: type_name = "SWITCH"; break;
        case NODE_CASE: type_name = "CASE"; break;
        case NODE_DEFAULT: type_name = "DEFAULT"; break;
    }
    printf("Type: %s, Value: %s\n", type_name, node->value ? node->value : "NULL");
    print_ast(node->left, depth + 1);
    print_ast(node->right, depth + 1);
    print_ast(node->third, depth + 1);
    print_ast(node->fourth, depth + 1);
    print_ast(node->next, depth);
}
