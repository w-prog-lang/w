#include "sema.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char* name;
    int len;
    int is_const;
    TypeRef type;
} Symbol;

typedef struct Scope {
    Symbol* symbols;
    int count;
    int cap;
    struct Scope* parent;
} Scope;

typedef struct {
    const char* name;
    int len;
    int param_count;
    Param* params;  // borrowed from the func_decl AST node
    TypeRef return_type;
} FuncInfo;

typedef struct {
    const char* name;
    int len;
    Param* fields;
    int field_count;
} StructInfo;

typedef struct {
    Scope* current;
    FuncInfo* funcs;
    int func_count;
    int func_cap;
    StructInfo* structs;
    int struct_count;
    int struct_cap;
    int loop_depth;
    int had_error;
} Sema;

// synthetic TypeRef constants used for inferred literal types
static const char TY_INT8[] = "int8";
static const char TY_INT16[] = "int16";
static const char TY_INT32[] = "int32";
static const char TY_INT64[] = "int64";
static const char TY_INT128[] = "int128";
static const char TY_STRING[] = "string";

static TypeRef type_int8(void) {
    TypeRef t = {TY_INT8, 4, 0, 0};
    return t;
}
static TypeRef type_int16(void) {
    TypeRef t = {TY_INT16, 5, 0, 0};
    return t;
}
static TypeRef type_int32(void) {
    TypeRef t = {TY_INT32, 5, 0, 0};
    return t;
}
static TypeRef type_int64(void) {
    TypeRef t = {TY_INT64, 5, 0, 0};
    return t;
}
static TypeRef type_int128(void) {
    TypeRef t = {TY_INT128, 6, 0, 0};
    return t;
}
static TypeRef type_string(void) {
    TypeRef t = {TY_STRING, 6, 0, 0};
    return t;
}

static int is_string_type(TypeRef t) {
    return t.name != NULL && t.len == 6 && strncmp(t.name, "string", 6) == 0;
}

// type yielded by indexing a symbol of type `t` with '[' expr ']'
static TypeRef element_type_of(TypeRef t) {
    if (t.is_array) {
        TypeRef elem = t;
        elem.is_array = 0;
        elem.array_len = 0;
        return elem;
    }
    if (is_string_type(t)) return type_int8();  // string indexing yields a byte
    return type_int64();
}

static int type_rank(TypeRef t) {
    if (t.name == NULL) return 3;  // unknown -> treat as int64
    if (t.len == 4 && strncmp(t.name, "int8", 4) == 0) return 0;
    if (t.len == 5 && strncmp(t.name, "int16", 5) == 0) return 1;
    if (t.len == 5 && strncmp(t.name, "int32", 5) == 0) return 2;
    if (t.len == 5 && strncmp(t.name, "int64", 5) == 0) return 3;
    if (t.len == 6 && strncmp(t.name, "int128", 6) == 0) return 4;
    return 3;  // unrecognized type name -> fall back to int64
}

static TypeRef type_from_rank(int rank) {
    switch (rank) {
        case 0:
            return type_int8();
        case 1:
            return type_int16();
        case 2:
            return type_int32();
        case 4:
            return type_int128();
        default:
            return type_int64();
    }
}

static TypeRef widen(TypeRef a, TypeRef b) {
    int ra = type_rank(a);
    int rb = type_rank(b);
    return type_from_rank(ra > rb ? ra : rb);
}

// smallest signed integer type that fits `value`
static TypeRef smallest_type_for_value(long long value) {
    if (value >= -128 && value <= 127) return type_int8();
    if (value >= -32768 && value <= 32767) return type_int16();
    if (value >= -2147483648LL && value <= 2147483647LL) return type_int32();
    return type_int64();
    // NOTE: literals beyond int64 range (needing int128) are not handled
    // yet -- parse_int_literal below uses long long, so extremely large
    // literals will silently overflow. Flagging as a known gap.
}

static long long parse_int_literal(const char* text, int len) {
    long long value = 0;
    int neg = 0;
    int i = 0;
    if (len > 0 && text[0] == '-') {
        neg = 1;
        i = 1;
    }
    for (; i < len; i++) {
        if (text[i] < '0' || text[i] > '9') break;  // stop at '.' etc.
        value = value * 10 + (text[i] - '0');
    }
    return neg ? -value : value;
}

static void error(Sema* s, int line, const char* msg, const char* name,
                  int len) {
    fprintf(stderr, "sema error at line %d: %s: '%.*s'\n", line, msg, len,
            name);
    s->had_error = 1;
}

static Scope* scope_push(Scope* parent) {
    Scope* sc = malloc(sizeof(Scope));
    sc->symbols = NULL;
    sc->count = 0;
    sc->cap = 0;
    sc->parent = parent;
    return sc;
}

static Scope* scope_pop(Scope* sc) {
    Scope* parent = sc->parent;
    free(sc->symbols);
    free(sc);
    return parent;
}

static void scope_declare(Scope* sc, const char* name, int len, int is_const,
                          TypeRef type) {
    if (sc->count == sc->cap) {
        sc->cap = sc->cap == 0 ? 8 : sc->cap * 2;
        sc->symbols = realloc(sc->symbols, sc->cap * sizeof(Symbol));
    }
    sc->symbols[sc->count].name = name;
    sc->symbols[sc->count].len = len;
    sc->symbols[sc->count].is_const = is_const;
    sc->symbols[sc->count].type = type;
    sc->count++;
}

static Symbol* scope_lookup(Scope* sc, const char* name, int len) {
    while (sc) {
        for (int i = 0; i < sc->count; i++) {
            if (sc->symbols[i].len == len &&
                strncmp(sc->symbols[i].name, name, len) == 0) {
                return &sc->symbols[i];
            }
        }
        sc = sc->parent;
    }
    return NULL;
}

static Symbol* scope_lookup_local(Scope* sc, const char* name, int len) {
    for (int i = 0; i < sc->count; i++) {
        if (sc->symbols[i].len == len &&
            strncmp(sc->symbols[i].name, name, len) == 0) {
            return &sc->symbols[i];
        }
    }
    return NULL;
}

static void register_func(Sema* s, const char* name, int len, int param_count,
                          Param* params, TypeRef return_type) {
    if (s->func_count == s->func_cap) {
        s->func_cap = s->func_cap == 0 ? 8 : s->func_cap * 2;
        s->funcs = realloc(s->funcs, s->func_cap * sizeof(FuncInfo));
    }
    s->funcs[s->func_count].name = name;
    s->funcs[s->func_count].len = len;
    s->funcs[s->func_count].param_count = param_count;
    s->funcs[s->func_count].params = params;
    s->funcs[s->func_count].return_type = return_type;
    s->func_count++;
}

static FuncInfo* lookup_func(Sema* s, const char* name, int len) {
    for (int i = 0; i < s->func_count; i++) {
        if (s->funcs[i].len == len &&
            strncmp(s->funcs[i].name, name, len) == 0) {
            return &s->funcs[i];
        }
    }
    return NULL;
}

static void register_struct(Sema* s, const char* name, int len, Param* fields,
                            int field_count) {
    if (s->struct_count == s->struct_cap) {
        s->struct_cap = s->struct_cap == 0 ? 8 : s->struct_cap * 2;
        s->structs = realloc(s->structs, s->struct_cap * sizeof(StructInfo));
    }
    s->structs[s->struct_count].name = name;
    s->structs[s->struct_count].len = len;
    s->structs[s->struct_count].fields = fields;
    s->structs[s->struct_count].field_count = field_count;
    s->struct_count++;
}

static StructInfo* lookup_struct(Sema* s, const char* name, int len) {
    if (!name) return NULL;
    for (int i = 0; i < s->struct_count; i++) {
        if (s->structs[i].len == len &&
            strncmp(s->structs[i].name, name, len) == 0) {
            return &s->structs[i];
        }
    }
    return NULL;
}

// is `t` a builtin scalar type, "string", or a declared struct name?
static int is_known_type_name(Sema* s, TypeRef t) {
    if (t.name == NULL) return 1;  // inferred; nothing to validate
    if (t.len == 4 && strncmp(t.name, "int8", 4) == 0) return 1;
    if (t.len == 5 && strncmp(t.name, "int16", 5) == 0) return 1;
    if (t.len == 5 && strncmp(t.name, "int32", 5) == 0) return 1;
    if (t.len == 5 && strncmp(t.name, "int64", 5) == 0) return 1;
    if (t.len == 6 && strncmp(t.name, "int128", 6) == 0) return 1;
    if (is_string_type(t)) return 1;
    return lookup_struct(s, t.name, t.len) != NULL;
}

// resolves the type of `field` on a value of type `base_type`, reporting
// sema errors for a non-struct base or an unknown field
static TypeRef lookup_field_type(Sema* s, TypeRef base_type,
                                 const char* field, int field_len, int line) {
    StructInfo* si = lookup_struct(s, base_type.name, base_type.len);
    if (!si) {
        error(s, line, "field access on non-struct type", field, field_len);
        return type_int64();
    }
    for (int i = 0; i < si->field_count; i++) {
        if (si->fields[i].name_len == field_len &&
            strncmp(si->fields[i].name, field, field_len) == 0) {
            return si->fields[i].type;
        }
    }
    error(s, line, "no such field", field, field_len);
    return type_int64();
}

// walks a '.'/'[]' access chain down to the identifier that anchors it, for
// const-checking assignment targets -- parse_assign_or_expr only ever builds
// these chains starting from a plain identifier, so this always terminates
// on a NODE_IDENT
static Node* find_root_ident(Node* n) {
    while (n->kind == NODE_FIELD || n->kind == NODE_INDEX) {
        n = (n->kind == NODE_FIELD) ? n->as.field.base : n->as.index.base;
    }
    return n;
}

static TypeRef infer_expr(Sema* s, Node* n);

// validates a printf() builtin call: the first argument must be a string
// literal, and its %d/%s directives must line up one-to-one with the types
// of the remaining arguments ('%%' is a literal percent)
static void check_printf(Sema* s, Node* n) {
    if (n->as.call.arg_count < 1) {
        fprintf(stderr,
                "sema error at line %d: 'printf' expects a format string\n",
                n->line);
        s->had_error = 1;
        return;
    }

    Node* fmt = n->as.call.args[0];
    if (fmt->kind != NODE_STRING) {
        error(s, n->line, "printf format must be a string literal", "printf",
              6);
        for (int i = 1; i < n->as.call.arg_count; i++) {
            infer_expr(s, n->as.call.args[i]);
        }
        return;
    }

    const char* text = fmt->as.str.text;
    int len = fmt->as.str.len;
    int next_arg = 1;

    for (int i = 0; i < len; i++) {
        if (text[i] != '%') continue;
        if (i + 1 >= len) {
            error(s, n->line, "incomplete format directive at end of string",
                  "%", 1);
            break;
        }
        char d = text[++i];
        if (d == '%') continue;
        if (d != 'd' && d != 's') {
            error(s, n->line, "unsupported format directive", &text[i - 1], 2);
            continue;
        }
        if (next_arg < n->as.call.arg_count) {
            TypeRef arg_type = infer_expr(s, n->as.call.args[next_arg]);
            if ((d == 's') != is_string_type(arg_type)) {
                error(s, n->line,
                      "format directive does not match argument type",
                      &text[i - 1], 2);
            }
        }
        next_arg++;
    }

    if (next_arg != n->as.call.arg_count) {
        fprintf(stderr,
                "sema error at line %d: format string expects %d value args, "
                "got %d\n",
                n->line, next_arg - 1, n->as.call.arg_count - 1);
        s->had_error = 1;
        // still check any surplus args for undeclared identifiers
        for (int i = next_arg; i < n->as.call.arg_count; i++) {
            infer_expr(s, n->as.call.args[i]);
        }
    }
}

// evaluates an expression: performs existing checks (undeclared name/call,
// arg count) AND returns its inferred type
static TypeRef infer_expr(Sema* s, Node* n) {
    if (!n) return type_int64();

    switch (n->kind) {
        case NODE_NUM:
            return smallest_type_for_value(
                parse_int_literal(n->as.num.text, n->as.num.len));

        case NODE_IDENT: {
            Symbol* sym =
                scope_lookup(s->current, n->as.ident.name, n->as.ident.len);
            if (!sym) {
                error(s, n->line, "use of undeclared identifier",
                      n->as.ident.name, n->as.ident.len);
                return type_int64();
            }
            return sym->type;
        }

        case NODE_BINOP: {
            TypeRef lt = infer_expr(s, n->as.binop.left);
            TypeRef rt = infer_expr(s, n->as.binop.right);
            return widen(lt, rt);
        }

        case NODE_UNARY:
            return infer_expr(s, n->as.unary.operand);

        case NODE_STRING:
            return type_string();

        case NODE_INDEX: {
            TypeRef base_type = infer_expr(s, n->as.index.base);
            if (!base_type.is_array && !is_string_type(base_type)) {
                error(s, n->line, "indexing a non-array, non-string value",
                      "[]", 2);
            }
            infer_expr(s, n->as.index.index);
            return element_type_of(base_type);
        }

        case NODE_FIELD: {
            TypeRef base_type = infer_expr(s, n->as.field.base);
            return lookup_field_type(s, base_type, n->as.field.field,
                                     n->as.field.field_len, n->line);
        }

        case NODE_CALL: {
            // "print" and "printf" are builtins, not user-declarable
            // functions; their result (if used) is a placeholder int64 since
            // the language has no void type. print takes one or more
            // arguments of any printable type and writes them back to back,
            // followed by a newline
            if (n->as.call.name_len == 5 &&
                strncmp(n->as.call.name, "print", 5) == 0) {
                if (n->as.call.arg_count < 1) {
                    fprintf(stderr,
                            "sema error at line %d: 'print' expects at least "
                            "1 arg\n",
                            n->line);
                    s->had_error = 1;
                }
                for (int i = 0; i < n->as.call.arg_count; i++) {
                    infer_expr(s, n->as.call.args[i]);
                }
                return type_int64();
            }

            if (n->as.call.name_len == 6 &&
                strncmp(n->as.call.name, "printf", 6) == 0) {
                check_printf(s, n);
                return type_int64();
            }

            FuncInfo* fi = lookup_func(s, n->as.call.name, n->as.call.name_len);
            if (!fi) {
                error(s, n->line, "call to undeclared function",
                      n->as.call.name, n->as.call.name_len);
            } else if (fi->param_count != n->as.call.arg_count) {
                fprintf(
                    stderr,
                    "sema error at line %d: '%.*s' expects %d args, got %d\n",
                    n->line, n->as.call.name_len, n->as.call.name,
                    fi->param_count, n->as.call.arg_count);
                s->had_error = 1;
            }
            for (int i = 0; i < n->as.call.arg_count; i++) {
                TypeRef arg_type = infer_expr(s, n->as.call.args[i]);
                if (!fi || i >= fi->param_count) continue;

                TypeRef param_type = fi->params[i].type;
                if (is_string_type(param_type) != is_string_type(arg_type)) {
                    error(s, n->line, "type mismatch in call argument",
                          n->as.call.name, n->as.call.name_len);
                } else if (!is_string_type(param_type) &&
                           type_rank(arg_type) > type_rank(param_type)) {
                    error(s, n->line, "argument type too wide for parameter",
                          n->as.call.name, n->as.call.name_len);
                }
            }
            return fi ? fi->return_type : type_int64();
        }

        default:
            error(s, n->line, "invalid expression node", "?", 1);
            return type_int64();
    }
}

static void check_stmt(Sema* s, Node* n);

static void check_block(Sema* s, Node* block) {
    s->current = scope_push(s->current);
    for (int i = 0; i < block->as.block.stmts.count; i++) {
        check_stmt(s, (Node*)block->as.block.stmts.items[i]);
    }
    s->current = scope_pop(s->current);
}

static void check_stmt(Sema* s, Node* n) {
    switch (n->kind) {
        case NODE_VAR_DECL: {
            const char* name = n->as.var_decl.name;
            int len = n->as.var_decl.name_len;

            if (scope_lookup_local(s->current, name, len)) {
                error(s, n->line, "redefinition of identifier", name, len);
            }

            TypeRef decl_type;
            if (n->as.var_decl.type.name != NULL) {
                // explicit type given; check init expr's type fits within it
                decl_type = n->as.var_decl.type;
                if (!is_known_type_name(s, decl_type)) {
                    error(s, n->line, "unknown type name", name, len);
                }
                if (n->as.var_decl.init) {
                    TypeRef init_type = infer_expr(s, n->as.var_decl.init);
                    if (is_string_type(decl_type) != is_string_type(init_type)) {
                        error(s, n->line,
                              "type mismatch between declared type and "
                              "initializer",
                              name, len);
                    } else if (!is_string_type(decl_type) &&
                               type_rank(init_type) > type_rank(decl_type)) {
                        error(s, n->line,
                              "initializer type too wide for declared type",
                              name, len);
                    }
                }
            } else {
                decl_type = infer_expr(s, n->as.var_decl.init);
                n->as.var_decl.type = decl_type;
            }

            scope_declare(s->current, name, len, n->as.var_decl.is_const,
                          decl_type);
            break;
        }

        case NODE_ASSIGN: {
            const char* name = n->as.assign.name;
            int len = n->as.assign.name_len;

            Symbol* sym = scope_lookup(s->current, name, len);
            if (!sym) {
                error(s, n->line, "assignment to undeclared identifier", name,
                      len);
            } else if (sym->is_const) {
                error(s, n->line, "assignment to const identifier", name, len);
            }

            TypeRef value_type = infer_expr(s, n->as.assign.value);

            if (sym && is_string_type(sym->type) != is_string_type(value_type)) {
                error(s, n->line, "type mismatch in assignment", name, len);
            } else if (sym && type_rank(value_type) > type_rank(sym->type)) {
                error(s, n->line, "assigned value type too wide for variable",
                      name, len);
            }

            break;
        }

        case NODE_INDEX_ASSIGN: {
            Node* root = find_root_ident(n->as.index_assign.base);
            Symbol* sym = scope_lookup(s->current, root->as.ident.name,
                                       root->as.ident.len);
            if (sym && sym->is_const) {
                error(s, n->line, "assignment to const identifier",
                      root->as.ident.name, root->as.ident.len);
            }

            // infer_expr(base) reports its own "undeclared identifier" error
            // if sym is NULL, so nothing further to do in that case here
            TypeRef base_type = infer_expr(s, n->as.index_assign.base);
            if (!base_type.is_array && !is_string_type(base_type)) {
                error(s, n->line, "indexing a non-array, non-string value",
                      root->as.ident.name, root->as.ident.len);
            }

            infer_expr(s, n->as.index_assign.index);
            TypeRef value_type = infer_expr(s, n->as.index_assign.value);
            TypeRef elem_type = element_type_of(base_type);

            if (is_string_type(elem_type) != is_string_type(value_type)) {
                error(s, n->line, "type mismatch in assignment",
                      root->as.ident.name, root->as.ident.len);
            } else if (!is_string_type(elem_type) &&
                       type_rank(value_type) > type_rank(elem_type)) {
                error(s, n->line, "assigned value type too wide for element",
                      root->as.ident.name, root->as.ident.len);
            }

            break;
        }

        case NODE_FIELD_ASSIGN: {
            Node* root = find_root_ident(n->as.field_assign.base);
            Symbol* sym = scope_lookup(s->current, root->as.ident.name,
                                       root->as.ident.len);
            if (sym && sym->is_const) {
                error(s, n->line, "assignment to const identifier",
                      root->as.ident.name, root->as.ident.len);
            }

            TypeRef base_type = infer_expr(s, n->as.field_assign.base);
            TypeRef field_type =
                lookup_field_type(s, base_type, n->as.field_assign.field,
                                  n->as.field_assign.field_len, n->line);
            TypeRef value_type = infer_expr(s, n->as.field_assign.value);

            if (is_string_type(field_type) != is_string_type(value_type)) {
                error(s, n->line, "type mismatch in assignment",
                      n->as.field_assign.field, n->as.field_assign.field_len);
            } else if (!is_string_type(field_type) &&
                       type_rank(value_type) > type_rank(field_type)) {
                error(s, n->line, "assigned value type too wide for field",
                      n->as.field_assign.field, n->as.field_assign.field_len);
            }

            break;
        }

        case NODE_CALL:
            infer_expr(s, n);
            break;

        case NODE_IDENT:
            error(s, n->line, "expression statement has no effect",
                  n->as.ident.name, n->as.ident.len);
            break;

        case NODE_IF:
            infer_expr(s, n->as.if_stmt.cond);
            check_block(s, n->as.if_stmt.then_block);
            if (n->as.if_stmt.else_block) {
                // else-if chains to another NODE_IF; a plain 'else' has a
                // NODE_BLOCK, which needs its own scope via check_block
                if (n->as.if_stmt.else_block->kind == NODE_IF) {
                    check_stmt(s, n->as.if_stmt.else_block);
                } else {
                    check_block(s, n->as.if_stmt.else_block);
                }
            }
            break;

        case NODE_LOOP: {
            s->current = scope_push(s->current);
            s->loop_depth++;

            if (n->as.loop.init) {
                check_stmt(s, n->as.loop.init);
            }
            if (n->as.loop.cond) {
                infer_expr(s, n->as.loop.cond);
            }

            // body gets its own nested scope (via check_block), but can
            // still see the loop-scope variable (e.g. 'i') via the parent
            // chain in scope_lookup
            check_block(s, n->as.loop.body);

            if (n->as.loop.step) {
                check_stmt(s, n->as.loop.step);
            }

            s->loop_depth--;
            s->current = scope_pop(s->current);
            break;
        }

        case NODE_BREAK:
            if (s->loop_depth == 0) {
                error(s, n->line, "'break' used outside of a loop", "break",
                      5);
            }
            break;

        case NODE_CONTINUE:
            if (s->loop_depth == 0) {
                error(s, n->line, "'continue' used outside of a loop",
                      "continue", 8);
            }
            break;

        case NODE_RETURN:
            if (n->as.return_stmt.expr) {
                infer_expr(s, n->as.return_stmt.expr);
            }
            break;

        default:
            error(s, n->line, "invalid statement node", "?", 1);
            break;
    }
}

static void check_func(Sema* s, Node* fn) {
    s->current = scope_push(NULL);

    for (int i = 0; i < fn->as.func_decl.param_count; i++) {
        Param* param = &fn->as.func_decl.params[i];
        scope_declare(s->current, param->name, param->name_len, 1, param->type);
    }

    for (int i = 0; i < fn->as.func_decl.body->as.block.stmts.count; i++) {
        check_stmt(s, (Node*)fn->as.func_decl.body->as.block.stmts.items[i]);
    }

    s->current = scope_pop(s->current);
}

SemaResult sema_check(Node* program) {
    Sema s;
    s.current = NULL;
    s.funcs = NULL;
    s.func_count = 0;
    s.func_cap = 0;
    s.structs = NULL;
    s.struct_count = 0;
    s.struct_cap = 0;
    s.loop_depth = 0;
    s.had_error = 0;

    for (int i = 0; i < program->as.program.structs.count; i++) {
        Node* sd = (Node*)program->as.program.structs.items[i];
        if (lookup_struct(&s, sd->as.struct_decl.name,
                          sd->as.struct_decl.name_len)) {
            error(&s, sd->line, "redefinition of struct",
                  sd->as.struct_decl.name, sd->as.struct_decl.name_len);
            continue;
        }
        register_struct(&s, sd->as.struct_decl.name, sd->as.struct_decl.name_len,
                        sd->as.struct_decl.fields, sd->as.struct_decl.field_count);
    }

    for (int i = 0; i < program->as.program.funcs.count; i++) {
        Node* fn = (Node*)program->as.program.funcs.items[i];
        if (lookup_func(&s, fn->as.func_decl.name, fn->as.func_decl.name_len)) {
            error(&s, fn->line, "redefinition of function",
                  fn->as.func_decl.name, fn->as.func_decl.name_len);
            continue;
        }
        register_func(&s, fn->as.func_decl.name, fn->as.func_decl.name_len,
                      fn->as.func_decl.param_count, fn->as.func_decl.params,
                      fn->as.func_decl.return_type);
    }

    for (int i = 0; i < program->as.program.funcs.count; i++) {
        check_func(&s, (Node*)program->as.program.funcs.items[i]);
    }

    free(s.funcs);
    free(s.structs);

    SemaResult result;
    result.had_error = s.had_error;
    return result;
}
