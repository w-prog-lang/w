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
        case TOK_PERCENT:
            return "%";
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

        case NODE_CALL: {
            // "print" is a builtin: each argument goes through the
            // w_print_val() dispatch macro (see codegen_emit's preamble),
            // then w_print_nl() ends the line; the whole call is one comma
            // expression so print stays usable in expression position
            if (n->as.call.name_len == 5 &&
                strncmp(n->as.call.name, "print", 5) == 0) {
                fprintf(out, "(");
                for (int i = 0; i < n->as.call.arg_count; i++) {
                    fprintf(out, "w_print_val(");
                    emit_expr(out, n->as.call.args[i]);
                    fprintf(out, "), ");
                }
                fprintf(out, "w_print_nl())");
                break;
            }
            fprintf(out, "%.*s(", n->as.call.name_len, n->as.call.name);
            for (int i = 0; i < n->as.call.arg_count; i++) {
                if (i > 0) fprintf(out, ", ");
                emit_expr(out, n->as.call.args[i]);
            }
            fprintf(out, ")");
            break;
        }

        case NODE_STRING:
            fprintf(out, "\"%.*s\"", n->as.str.len, n->as.str.text);
            break;

        case NODE_INDEX:
            emit_expr(out, n->as.index.base);
            fprintf(out, "[");
            emit_expr(out, n->as.index.index);
            fprintf(out, "]");
            break;

        case NODE_FIELD:
            emit_expr(out, n->as.field.base);
            fprintf(out, ".%.*s", n->as.field.field_len, n->as.field.field);
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

// emits "if (cond) { ... } [else ...]" without a leading indent, so an
// 'else if' can chain onto the previous 'else ' with no newline between them
static void emit_if_chain(FILE* out, Node* n, int depth) {
    fprintf(out, "if (");
    emit_expr(out, n->as.if_stmt.cond);
    fprintf(out, ")\n");
    emit_block(out, n->as.if_stmt.then_block, depth);
    if (n->as.if_stmt.else_block) {
        emit_indent(out, depth);
        if (n->as.if_stmt.else_block->kind == NODE_IF) {
            fprintf(out, "else ");
            emit_if_chain(out, n->as.if_stmt.else_block, depth);
        } else {
            fprintf(out, "else\n");
            emit_block(out, n->as.if_stmt.else_block, depth);
        }
    }
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
            emit_expr(out, n->as.index_assign.base);
            fprintf(out, "[");
            emit_expr(out, n->as.index_assign.index);
            fprintf(out, "] = ");
            emit_expr(out, n->as.index_assign.value);
            fprintf(out, ";\n");
            break;

        case NODE_FIELD_ASSIGN:
            emit_indent(out, depth);
            emit_expr(out, n->as.field_assign.base);
            fprintf(out, ".%.*s = ", n->as.field_assign.field_len,
                    n->as.field_assign.field);
            emit_expr(out, n->as.field_assign.value);
            fprintf(out, ";\n");
            break;

        case NODE_CALL:
            emit_indent(out, depth);
            emit_expr(out, n);
            fprintf(out, ";\n");
            break;

        case NODE_IF:
            emit_indent(out, depth);
            emit_if_chain(out, n, depth);
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

static void emit_struct(FILE* out, Node* sd) {
    fprintf(out, "typedef struct {\n");
    for (int i = 0; i < sd->as.struct_decl.field_count; i++) {
        Param* f = &sd->as.struct_decl.fields[i];
        fprintf(out, "    ");
        emit_c_type(out, f->type);
        fprintf(out, " %.*s", f->name_len, f->name);
        if (f->type.is_array) fprintf(out, "[%d]", f->type.array_len);
        fprintf(out, ";\n");
    }
    fprintf(out, "} %.*s;\n\n", sd->as.struct_decl.name_len,
            sd->as.struct_decl.name);
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
        if (p->type.is_array) {
            fprintf(out, "[%d]", p->type.array_len);
        }
    }
    fprintf(out, ")\n");

    emit_block(out, fn->as.func_decl.body, 0);
    fprintf(out, "\n");
}

// print() has no single C type -- it accepts any integer type or a string,
// so each argument dispatches on its C type via _Generic rather than
// tracking static types through codegen (which has no type oracle of its
// own); a print(a, b, ...) call becomes one w_print_val() per argument
// followed by w_print_nl()
static void emit_print_preamble(FILE* out) {
    fprintf(out, "#include <stdio.h>\n\n");
    fprintf(out,
            "static void w_print_i64(int64_t v) { "
            "printf(\"%%lld\", (long long)v); }\n");
    fprintf(out,
            "static void w_print_str(const char* v) { "
            "printf(\"%%s\", v); }\n");
    fprintf(out, "static void w_print_nl(void) { printf(\"\\n\"); }\n");
    fprintf(out,
            "#define w_print_val(x) _Generic((x), "
            "char*: w_print_str, const char*: w_print_str, "
            "default: w_print_i64)(x)\n\n");
}

void codegen_emit(Node* program, FILE* out) {
    fprintf(out, "#include <stdint.h>\n\n");
    emit_print_preamble(out);

    for (int i = 0; i < program->as.program.structs.count; i++) {
        emit_struct(out, (Node*)program->as.program.structs.items[i]);
    }

    for (int i = 0; i < program->as.program.funcs.count; i++) {
        emit_func(out, (Node*)program->as.program.funcs.items[i]);
    }
}
