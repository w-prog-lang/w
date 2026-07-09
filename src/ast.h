#ifndef WLANG_AST_H
#define WLANG_AST_H

#include "lexer.h"
#include "util.h"

typedef struct {
    const char* name;
    int len;
    int is_array;
    int array_len;
} TypeRef;

typedef enum {
    NODE_PROGRAM,
    NODE_FUNC_DECL,
    NODE_BLOCK,
    NODE_VAR_DECL,
    NODE_ASSIGN,
    NODE_IF,
    NODE_LOOP,
    NODE_RETURN,
    NODE_BINOP,
    NODE_UNARY,
    NODE_CALL,
    NODE_IDENT,
    NODE_NUM,
    NODE_STRING,
    NODE_INDEX,
    NODE_INDEX_ASSIGN,
    NODE_STRUCT_DECL,
    NODE_FIELD,
    NODE_FIELD_ASSIGN,
    NODE_BREAK,
    NODE_CONTINUE,
    NODE_IMPORT,
} NodeKind;

typedef struct Node Node;

typedef struct {
    const char* name;
    int name_len;
    TypeRef type;
} Param;

struct Node {
    NodeKind kind;
    int line;

    union {
        struct {
            PtrList imports;
            PtrList funcs;
            PtrList structs;
        } program;

        struct {
            const char* path;
            int path_len;
            int is_c;  // 1 for a '.h' C header, 0 for a '.wsrc' W library
        } import;

        struct {
            const char* name;
            int name_len;
            TypeRef return_type;
            Param* params;
            int param_count;
            Node* body;
        } func_decl;

        struct {
            PtrList stmts;
        } block;

        struct {
            const char* name;
            int name_len;
            TypeRef type;
            Node* init;
            int is_const;
        } var_decl;

        struct {
            const char* name;
            int name_len;
            Node* value;
        } assign;

        struct {
            Node* cond;
            Node* then_block;
            Node* else_block;  // NULL, a NODE_BLOCK, or a NODE_IF (else-if)
        } if_stmt;

        struct {
            Node* expr;
        } return_stmt;

        struct {
            TokenKind op;
            Node* left;
            Node* right;
        } binop;

        struct {
            TokenKind op;  // TOK_BANG or TOK_MINUS
            Node* operand;
        } unary;

        struct {
            const char* name;
            int name_len;
            Node** args;
            int arg_count;
        } call;

        struct {
            const char* name;
            int len;
        } ident;

        struct {
            const char* text;
            int len;
        } num;

        struct {
            const char* text;
            int len;
        } str;

        struct {
            Node* base;
            Node* index;
        } index;

        struct {
            Node* base;
            Node* index;
            Node* value;
        } index_assign;

        struct {
            const char* name;
            int name_len;
            Param* fields;
            int field_count;
        } struct_decl;

        struct {
            Node* base;
            const char* field;
            int field_len;
        } field;

        struct {
            Node* base;
            const char* field;
            int field_len;
            Node* value;
        } field_assign;

        struct {
            Node* init;  // NULL if no init clause
            Node* cond;  // NULL if infinite loop (no cond at all)
            Node* step;  // NULL if no step clause
            Node* body;
        } loop;
    } as;
};

Node* ast_new(Arena* a, NodeKind kind, int line);
const char* node_kind_name(NodeKind kind);

#endif
