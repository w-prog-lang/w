#include <stdio.h>
#include <stdlib.h>

#include "ast.h"
#include "codegen.h"
#include "lexer.h"
#include "parser.h"
#include "sema.h"
#include "util.h"

static char* read_file(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open file: %s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* buf = malloc(size + 1);
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);

    *out_len = (size_t)size;
    return buf;
}

static void indent(int depth) {
    for (int i = 0; i < depth; i++) printf("  ");
}

static const char* op_str(TokenKind op) {
    switch (op) {
        case TOK_PLUS:
            return "+";
        case TOK_MINUS:
            return "-";
        case TOK_STAR:
            return "*";
        case TOK_SLASH:
            return "/";
        case TOK_EQ:
            return "==";
        case TOK_NEQ:
            return "!=";
        case TOK_LT:
            return "<";
        case TOK_GT:
            return ">";
        case TOK_LE:
            return "<=";
        case TOK_GE:
            return ">=";
        default:
            return "?";
    }
}

static void print_type(TypeRef t) {
    if (t.name == NULL) {
        printf("<inferred>");
    } else {
        printf("%.*s", t.len, t.name);
    }
}

static void print_node(Node* n, int depth) {
    if (!n) {
        indent(depth);
        printf("(null)\n");
        return;
    }

    indent(depth);

    switch (n->kind) {
        case NODE_PROGRAM:
            printf("Program\n");
            for (int i = 0; i < n->as.program.funcs.count; i++) {
                print_node((Node*)n->as.program.funcs.items[i], depth + 1);
            }
            break;

        case NODE_FUNC_DECL:
            printf("FuncDecl %.*s -> ", n->as.func_decl.name_len,
                   n->as.func_decl.name);
            print_type(n->as.func_decl.return_type);
            printf("\n");
            indent(depth + 1);
            printf("Params:\n");
            for (int i = 0; i < n->as.func_decl.param_count; i++) {
                Param* param = &n->as.func_decl.params[i];
                indent(depth + 2);
                printf("%.*s: ", param->name_len, param->name);
                print_type(param->type);
                printf("\n");
            }
            indent(depth + 1);
            printf("Body:\n");
            print_node(n->as.func_decl.body, depth + 2);
            break;

        case NODE_BLOCK:
            printf("Block\n");
            for (int i = 0; i < n->as.block.stmts.count; i++) {
                print_node((Node*)n->as.block.stmts.items[i], depth + 1);
            }
            break;

        case NODE_VAR_DECL:
            printf("VarDecl %.*s [%s] type=", n->as.var_decl.name_len,
                   n->as.var_decl.name,
                   n->as.var_decl.is_const ? "const" : "var");
            print_type(n->as.var_decl.type);
            printf("\n");
            if (n->as.var_decl.init) {
                indent(depth + 1);
                printf("Init:\n");
                print_node(n->as.var_decl.init, depth + 2);
            }
            break;

        case NODE_ASSIGN:
            printf("Assign %.*s\n", n->as.assign.name_len, n->as.assign.name);
            print_node(n->as.assign.value, depth + 1);
            break;

        case NODE_IF:
            printf("If\n");
            indent(depth + 1);
            printf("Cond:\n");
            print_node(n->as.if_stmt.cond, depth + 2);
            indent(depth + 1);
            printf("Then:\n");
            print_node(n->as.if_stmt.then_block, depth + 2);
            if (n->as.if_stmt.else_block) {
                indent(depth + 1);
                printf("Else:\n");
                print_node(n->as.if_stmt.else_block, depth + 2);
            }
            break;

        case NODE_RETURN:
            printf("Return\n");
            if (n->as.return_stmt.expr) {
                print_node(n->as.return_stmt.expr, depth + 1);
            }
            break;

        case NODE_BINOP:
            printf("BinOp %s\n", op_str(n->as.binop.op));
            print_node(n->as.binop.left, depth + 1);
            print_node(n->as.binop.right, depth + 1);
            break;

        case NODE_CALL:
            printf("Call %.*s\n", n->as.call.name_len, n->as.call.name);
            for (int i = 0; i < n->as.call.arg_count; i++) {
                print_node(n->as.call.args[i], depth + 1);
            }
            break;

        case NODE_IDENT:
            printf("Ident %.*s\n", n->as.ident.len, n->as.ident.name);
            break;

        case NODE_NUM:
            printf("Num %.*s\n", n->as.num.len, n->as.num.text);
            break;

        default:
            printf("Unknown(%s)\n", node_kind_name(n->kind));
            break;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file>\n", argv[0]);
        return 1;
    }

    size_t len;
    char* src = read_file(argv[1], &len);

    Arena arena;
    arena_init(&arena);

    Parser p;
    parser_init(&p, src, len, &arena);

    Node* program = parser_parse_program(&p);

    if (p.had_error) {
        fprintf(stderr, "parsing failed with errors\n");
        arena_free(&arena);
        free(src);
        return 1;
    }

    print_node(program, 0);

    printf("\n--- sema check ---\n");
    SemaResult sr = sema_check(program);
    if (sr.had_error) {
        fprintf(stderr, "semantic analysis failed\n");
        arena_free(&arena);
        free(src);
        return 1;
    }
    printf("sema check passed\n");

    if (argc >= 3) {
        FILE* out = fopen(argv[2], "w");
        if (!out) {
            fprintf(stderr, "cannot open output file: %s\n", argv[2]);
            arena_free(&arena);
            free(src);
            return 1;
        }
        codegen_emit(program, out);
        fclose(out);
        printf("wrote %s\n", argv[2]);
    } else {
        codegen_emit(program, stdout);
    }

    arena_free(&arena);
    free(src);
    return 0;
}
