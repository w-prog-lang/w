#include "codegen.h"

#include <string.h>

static void emit_indent(FILE* out, int depth) {
    for (int i = 0; i < depth; i++) fprintf(out, "    ");
}

static void emit_c_type(FILE* out, TypeRef t) {
    if (t.len == 4 && strncmp(t.name, "int8", 4) == 0) {
        fprintf(out, "int8_t");
    } else if (t.len == 5 && strncmp(t.name, "int16", 5) == 0) {
        fprintf(out, "int16_t");
    } else if (t.len == 5 && strncmp(t.name, "int32", 5) == 0) {
        fprintf(out, "int32_t");
    } else if (t.len == 5 && strncmp(t.name, "int64", 5) == 0) {
        fprintf(out, "int64_t");
    } else if (t.len == 6 && strncmp(t.name, "int128", 6) == 0) {
        fprintf(out, "__int128");
    } else if (t.len == 6 && strncmp(t.name, "string", 6) == 0) {
        fprintf(out, "const char*");
    } else {
        // unknown type name; emit as-is and let the C compiler complain
        fprintf(out, "%.*s", t.len, t.name);
    }
}

static const char* binop_str(TokenKind op) {
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
        case TOK_AND_AND:
            return "&&";
        case TOK_OR_OR:
            return "||";
        default:
            return "?";
    }
}

static void emit_expr(FILE* out, Node* n) {
    switch (n->kind) {
        case NODE_NUM:
            fprintf(out, "%.*s", n->as.num.len, n->as.num.text);
            break;

        case NODE_IDENT:
            fprintf(out, "%.*s", n->as.ident.len, n->as.ident.name);
            break;

        case NODE_BINOP:
            fprintf(out, "(");
            emit_expr(out, n->as.binop.left);
            fprintf(out, " %s ", binop_str(n->as.binop.op));
            emit_expr(out, n->as.binop.right);
            fprintf(out, ")");
            break;

        case NODE_UNARY:
            fprintf(out, "(%s", n->as.unary.op == TOK_BANG ? "!" : "-");
            emit_expr(out, n->as.unary.operand);
            fprintf(out, ")");
            break;

        case NODE_CALL:
            fprintf(out, "%.*s(", n->as.call.name_len, n->as.call.name);
            for (int i = 0; i < n->as.call.arg_count; i++) {
                if (i > 0) fprintf(out, ", ");
                emit_expr(out, n->as.call.args[i]);
            }
            fprintf(out, ")");
            break;

        case NODE_STRING:
            fprintf(out, "\"%.*s\"", n->as.str.len, n->as.str.text);
            break;

        case NODE_INDEX:
            fprintf(out, "%.*s[", n->as.index.name_len, n->as.index.name);
            emit_expr(out, n->as.index.index);
            fprintf(out, "]");
            break;

        default:
            fprintf(out, "/* unsupported expr node: %s */",
                    node_kind_name(n->kind));
            break;
    }
}

static void emit_stmt(FILE* out, Node* n, int depth);

// emits a loop init/step clause for use inside a C `for (...)` header:
// no leading indent, no trailing ';', no newline
static void emit_for_clause(FILE* out, Node* n) {
    switch (n->kind) {
        case NODE_VAR_DECL:
            if (n->as.var_decl.is_const) fprintf(out, "const ");
            if (n->as.var_decl.type.name) {
                emit_c_type(out, n->as.var_decl.type);
            } else {
                fprintf(out, "int64_t");
            }
            fprintf(out, " %.*s", n->as.var_decl.name_len, n->as.var_decl.name);
            if (n->as.var_decl.type.is_array) {
                fprintf(out, "[%d]", n->as.var_decl.type.array_len);
            }
            if (n->as.var_decl.init) {
                fprintf(out, " = ");
                emit_expr(out, n->as.var_decl.init);
            }
            break;

        case NODE_ASSIGN:
            fprintf(out, "%.*s = ", n->as.assign.name_len, n->as.assign.name);
            emit_expr(out, n->as.assign.value);
            break;

        default:
            fprintf(out, "/* unsupported for-clause node: %s */",
                    node_kind_name(n->kind));
            break;
    }
}

static void emit_block(FILE* out, Node* block, int depth) {
    emit_indent(out, depth);
    fprintf(out, "{\n");
    for (int i = 0; i < block->as.block.stmts.count; i++) {
        emit_stmt(out, (Node*)block->as.block.stmts.items[i], depth + 1);
    }
    emit_indent(out, depth);
    fprintf(out, "}\n");
}

static void emit_stmt(FILE* out, Node* n, int depth) {
    switch (n->kind) {
        case NODE_VAR_DECL: {
            emit_indent(out, depth);
            if (n->as.var_decl.is_const) {
                fprintf(out, "const ");
            }
            if (n->as.var_decl.type.name) {
                emit_c_type(out, n->as.var_decl.type);
            } else {
                // no explicit type; inferred decls default to int64_t for now
                // (real type inference belongs in a later type-checking pass)
                fprintf(out, "int64_t");
            }
            fprintf(out, " %.*s", n->as.var_decl.name_len, n->as.var_decl.name);
            if (n->as.var_decl.type.is_array) {
                fprintf(out, "[%d]", n->as.var_decl.type.array_len);
            }
            if (n->as.var_decl.init) {
                fprintf(out, " = ");
                emit_expr(out, n->as.var_decl.init);
            }
            fprintf(out, ";\n");
            break;
        }

        case NODE_ASSIGN:
            emit_indent(out, depth);
            fprintf(out, "%.*s = ", n->as.assign.name_len, n->as.assign.name);
            emit_expr(out, n->as.assign.value);
            fprintf(out, ";\n");
            break;

        case NODE_INDEX_ASSIGN:
            emit_indent(out, depth);
            fprintf(out, "%.*s[", n->as.index_assign.name_len,
                    n->as.index_assign.name);
            emit_expr(out, n->as.index_assign.index);
            fprintf(out, "] = ");
            emit_expr(out, n->as.index_assign.value);
            fprintf(out, ";\n");
            break;

        case NODE_IF:
            emit_indent(out, depth);
            fprintf(out, "if (");
            emit_expr(out, n->as.if_stmt.cond);
            fprintf(out, ")\n");
            emit_block(out, n->as.if_stmt.then_block, depth);
            if (n->as.if_stmt.else_block) {
                emit_indent(out, depth);
                fprintf(out, "else\n");
                emit_block(out, n->as.if_stmt.else_block, depth);
            }
            break;

        case NODE_LOOP: {
            if (n->as.loop.init || n->as.loop.step) {
                // for-style: translate directly to C's `for` so that
                // `continue` correctly runs the step clause
                emit_indent(out, depth);
                fprintf(out, "for (");
                if (n->as.loop.init) {
                    emit_for_clause(out, n->as.loop.init);
                }
                fprintf(out, "; ");
                if (n->as.loop.cond) {
                    emit_expr(out, n->as.loop.cond);
                } else {
                    fprintf(out, "1");
                }
                fprintf(out, "; ");
                if (n->as.loop.step) {
                    emit_for_clause(out, n->as.loop.step);
                }
                fprintf(out, ")\n");
                emit_block(out, n->as.loop.body, depth);
            } else {
                emit_indent(out, depth);
                fprintf(out, "while (");
                if (n->as.loop.cond) {
                    emit_expr(out, n->as.loop.cond);
                } else {
                    fprintf(out, "1");
                }
                fprintf(out, ")\n");
                emit_block(out, n->as.loop.body, depth);
            }
            break;
        }

        case NODE_BREAK:
            emit_indent(out, depth);
            fprintf(out, "break;\n");
            break;

        case NODE_CONTINUE:
            emit_indent(out, depth);
            fprintf(out, "continue;\n");
            break;

        case NODE_RETURN:
            emit_indent(out, depth);
            fprintf(out, "return");
            if (n->as.return_stmt.expr) {
                fprintf(out, " ");
                emit_expr(out, n->as.return_stmt.expr);
            }
            fprintf(out, ";\n");
            break;

        default:
            emit_indent(out, depth);
            fprintf(out, "/* unsupported stmt node: %s */\n",
                    node_kind_name(n->kind));
            break;
    }
}

static void emit_func(FILE* out, Node* fn) {
    emit_c_type(out, fn->as.func_decl.return_type);
    fprintf(out, " %.*s(", fn->as.func_decl.name_len, fn->as.func_decl.name);

    if (fn->as.func_decl.param_count == 0) {
        fprintf(out, "void");
    }
    for (int i = 0; i < fn->as.func_decl.param_count; i++) {
        if (i > 0) fprintf(out, ", ");
        Param* p = &fn->as.func_decl.params[i];
        emit_c_type(out, p->type);
        fprintf(out, " %.*s", p->name_len, p->name);
    }
    fprintf(out, ")\n");

    emit_block(out, fn->as.func_decl.body, 0);
    fprintf(out, "\n");
}

void codegen_emit(Node* program, FILE* out) {
    fprintf(out, "#include <stdint.h>\n\n");

    for (int i = 0; i < program->as.program.funcs.count; i++) {
        emit_func(out, (Node*)program->as.program.funcs.items[i]);
    }
}
