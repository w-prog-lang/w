#include "sema.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char* name;
    int len;
    int is_const;
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
} FuncInfo;

typedef struct {
    Scope* current;
    FuncInfo* funcs;
    int func_count;
    int func_cap;
    int had_error;
} Sema;

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

// declares in the *current* (innermost) scope only
static void scope_declare(Scope* sc, const char* name, int len, int is_const) {
    if (sc->count == sc->cap) {
        sc->cap = sc->cap == 0 ? 8 : sc->cap * 2;
        sc->symbols = realloc(sc->symbols, sc->cap * sizeof(Symbol));
    }
    sc->symbols[sc->count].name = name;
    sc->symbols[sc->count].len = len;
    sc->symbols[sc->count].is_const = is_const;
    sc->count++;
}

// looks up across all enclosing scopes; returns NULL if not found
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

// only checks the *current* scope, for redeclaration detection
static Symbol* scope_lookup_local(Scope* sc, const char* name, int len) {
    for (int i = 0; i < sc->count; i++) {
        if (sc->symbols[i].len == len &&
            strncmp(sc->symbols[i].name, name, len) == 0) {
            return &sc->symbols[i];
        }
    }
    return NULL;
}

static void register_func(Sema* s, const char* name, int len, int param_count) {
    if (s->func_count == s->func_cap) {
        s->func_cap = s->func_cap == 0 ? 8 : s->func_cap * 2;
        s->funcs = realloc(s->funcs, s->func_cap * sizeof(FuncInfo));
    }
    s->funcs[s->func_count].name = name;
    s->funcs[s->func_count].len = len;
    s->funcs[s->func_count].param_count = param_count;
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

static void check_expr(Sema* s, Node* n) {
    if (!n) return;

    switch (n->kind) {
        case NODE_IDENT: {
            Symbol* sym =
                scope_lookup(s->current, n->as.ident.name, n->as.ident.len);
            if (!sym) {
                error(s, n->line, "use of undeclared identifier",
                      n->as.ident.name, n->as.ident.len);
            }
            break;
        }

        case NODE_NUM:
            break;

        case NODE_BINOP:
            check_expr(s, n->as.binop.left);
            check_expr(s, n->as.binop.right);
            break;

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
                check_expr(s, n->as.call.args[i]);
            }
            break;
        }

        default:
            error(s, n->line, "invalid expression node", "?", 1);
            break;
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

            // ident := expr / ident : type [= expr]  -- must not already
            // exist in the *current* scope (a2 := 41; after a2 already
            // declared is illegal, per language spec)
            if (scope_lookup_local(s->current, name, len)) {
                error(s, n->line, "redefinition of identifier", name, len);
            }

            if (n->as.var_decl.init) {
                check_expr(s, n->as.var_decl.init);
            }

            scope_declare(s->current, name, len, n->as.var_decl.is_const);
            break;
        }

        case NODE_ASSIGN: {
            const char* name = n->as.assign.name;
            int len = n->as.assign.name_len;

            Symbol* sym = scope_lookup(s->current, name, len);
            if (!sym) {
                // a1 = 42; where a1 was never declared -- illegal
                error(s, n->line, "assignment to undeclared identifier", name,
                      len);
            } else if (sym->is_const) {
                error(s, n->line, "assignment to const identifier", name, len);
            }

            check_expr(s, n->as.assign.value);
            break;
        }

        case NODE_IDENT: {
            // bare identifier statement, e.g. "a;" -- illegal per spec
            error(s, n->line, "expression statement has no effect",
                  n->as.ident.name, n->as.ident.len);
            break;
        }

        case NODE_IF:
            check_expr(s, n->as.if_stmt.cond);
            check_block(s, n->as.if_stmt.then_block);
            if (n->as.if_stmt.else_block) {
                check_block(s, n->as.if_stmt.else_block);
            }
            break;

        case NODE_RETURN:
            if (n->as.return_stmt.expr) {
                check_expr(s, n->as.return_stmt.expr);
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
        // params are treated as const bindings within the function body;
        // reassigning a param is disallowed just like any other const
        scope_declare(s->current, param->name, param->name_len, 1);
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

    // pre-register all functions first, so calls can appear before
    // their definitions in source order
    for (int i = 0; i < program->as.program.funcs.count; i++) {
        Node* fn = (Node*)program->as.program.funcs.items[i];
        if (lookup_func(&s, fn->as.func_decl.name, fn->as.func_decl.name_len)) {
            error(&s, fn->line, "redefinition of function",
                  fn->as.func_decl.name, fn->as.func_decl.name_len);
            continue;
        }
        register_func(&s, fn->as.func_decl.name, fn->as.func_decl.name_len,
                      fn->as.func_decl.param_count);
    }

    for (int i = 0; i < program->as.program.funcs.count; i++) {
        check_func(&s, (Node*)program->as.program.funcs.items[i]);
    }

    free(s.funcs);

    SemaResult result;
    result.had_error = s.had_error;
    return result;
}
