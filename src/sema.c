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
    TypeRef return_type;
} FuncInfo;

typedef struct {
    Scope* current;
    FuncInfo* funcs;
    int func_count;
    int func_cap;
    int had_error;
} Sema;

// synthetic TypeRef constants used for inferred literal types
static const char TY_INT8[] = "int8";
static const char TY_INT16[] = "int16";
static const char TY_INT32[] = "int32";
static const char TY_INT64[] = "int64";
static const char TY_INT128[] = "int128";

static TypeRef type_int8(void) {
    TypeRef t = {TY_INT8, 4};
    return t;
}
static TypeRef type_int16(void) {
    TypeRef t = {TY_INT16, 5};
    return t;
}
static TypeRef type_int32(void) {
    TypeRef t = {TY_INT32, 5};
    return t;
}
static TypeRef type_int64(void) {
    TypeRef t = {TY_INT64, 5};
    return t;
}
static TypeRef type_int128(void) {
    TypeRef t = {TY_INT128, 6};
    return t;
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
                          TypeRef return_type) {
    if (s->func_count == s->func_cap) {
        s->func_cap = s->func_cap == 0 ? 8 : s->func_cap * 2;
        s->funcs = realloc(s->funcs, s->func_cap * sizeof(FuncInfo));
    }
    s->funcs[s->func_count].name = name;
    s->funcs[s->func_count].len = len;
    s->funcs[s->func_count].param_count = param_count;
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

        case NODE_CALL: {
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
                infer_expr(s, n->as.call.args[i]);
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
                // explicit type given; still walk init for undeclared-name
                // checks
                decl_type = n->as.var_decl.type;
                if (n->as.var_decl.init) {
                    infer_expr(s, n->as.var_decl.init);
                }
            } else {
                // ':=' form; infer type from the init expression and write
                // it back into the AST node so codegen can use it directly
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

            infer_expr(s, n->as.assign.value);
            break;
        }

        case NODE_IDENT:
            error(s, n->line, "expression statement has no effect",
                  n->as.ident.name, n->as.ident.len);
            break;

        case NODE_IF:
            infer_expr(s, n->as.if_stmt.cond);
            check_block(s, n->as.if_stmt.then_block);
            if (n->as.if_stmt.else_block) {
                check_block(s, n->as.if_stmt.else_block);
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
    s.had_error = 0;

    for (int i = 0; i < program->as.program.funcs.count; i++) {
        Node* fn = (Node*)program->as.program.funcs.items[i];
        if (lookup_func(&s, fn->as.func_decl.name, fn->as.func_decl.name_len)) {
            error(&s, fn->line, "redefinition of function",
                  fn->as.func_decl.name, fn->as.func_decl.name_len);
            continue;
        }
        register_func(&s, fn->as.func_decl.name, fn->as.func_decl.name_len,
                      fn->as.func_decl.param_count,
                      fn->as.func_decl.return_type);
    }

    for (int i = 0; i < program->as.program.funcs.count; i++) {
        check_func(&s, (Node*)program->as.program.funcs.items[i]);
    }

    free(s.funcs);

    SemaResult result;
    result.had_error = s.had_error;
    return result;
}
