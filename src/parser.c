#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void advance(Parser* p) {
    p->prev = p->cur;
    p->cur = lexer_next(&p->lx);
}

static int check(Parser* p, TokenKind kind) { return p->cur.kind == kind; }

static int match(Parser* p, TokenKind kind) {
    if (!check(p, kind)) return 0;
    advance(p);
    return 1;
}

static void error_at(Parser* p, Token t, const char* msg) {
    fprintf(stderr, "parse error at line %d: %s (got '%.*s')\n", t.line, msg,
            t.len, t.start);
    p->had_error = 1;
}

static void expect(Parser* p, TokenKind kind, const char* msg) {
    if (check(p, kind)) {
        advance(p);
        return;
    }
    error_at(p, p->cur, msg);
}

void parser_init(Parser* p, const char* src, size_t len, Arena* arena) {
    lexer_init(&p->lx, src, len);
    p->arena = arena;
    p->had_error = 0;
    advance(p);  // prime p->cur
}

static Node* parse_expr(Parser* p);
static Node* parse_block(Parser* p);
static Node* parse_stmt(Parser* p);

static int is_valid_type_name(const char* name, int len) {
    static const struct {
        const char* n;
        int l;
    } valid[] = {
        {"int8", 4},   {"int16", 5}, {"int32", 5},
        {"int64", 5},  {"int128", 6}, {"string", 6},
    };
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
        if (valid[i].l == len && strncmp(valid[i].n, name, len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int parse_uint_literal(const char* text, int len) {
    int value = 0;
    for (int i = 0; i < len; i++) {
        if (text[i] < '0' || text[i] > '9') break;
        value = value * 10 + (text[i] - '0');
    }
    return value;
}

// type := IDENT [ '[' NUM ']' ]
static TypeRef parse_type(Parser* p) {
    TypeRef t;
    t.is_array = 0;
    t.array_len = 0;

    if (!check(p, TOK_IDENT)) {
        error_at(p, p->cur, "expected type name");
        t.name = NULL;
        t.len = 0;
        return t;
    }

    t.name = p->cur.start;
    t.len = p->cur.len;

    if (!is_valid_type_name(t.name, t.len)) {
        error_at(p, p->cur, "unknown type name");
    }

    advance(p);

    if (match(p, TOK_LBRACKET)) {
        if (!check(p, TOK_NUM)) {
            error_at(p, p->cur, "expected array size");
        } else {
            t.array_len = parse_uint_literal(p->cur.start, p->cur.len);
            advance(p);
        }
        expect(p, TOK_RBRACKET, "expected ']' after array size");
        t.is_array = 1;
    }

    return t;
}

// primary := IDENT | NUM | STRING | '(' expr ')'
//          | IDENT '(' args ')' | IDENT '[' expr ']'
static Node* parse_primary(Parser* p) {
    int line = p->cur.line;

    if (match(p, TOK_LPAREN)) {
        Node* e = parse_expr(p);
        expect(p, TOK_RPAREN, "expected ')'");
        return e;
    }

    if (check(p, TOK_NUM)) {
        Node* n = ast_new(p->arena, NODE_NUM, line);
        n->as.num.text = p->cur.start;
        n->as.num.len = p->cur.len;
        advance(p);
        return n;
    }

    if (check(p, TOK_STRING)) {
        Node* n = ast_new(p->arena, NODE_STRING, line);
        n->as.str.text = p->cur.start;
        n->as.str.len = p->cur.len;
        advance(p);
        return n;
    }

    if (check(p, TOK_IDENT)) {
        const char* name = p->cur.start;
        int name_len = p->cur.len;
        advance(p);

        if (match(p, TOK_LPAREN)) {
            Node* n = ast_new(p->arena, NODE_CALL, line);
            n->as.call.name = name;
            n->as.call.name_len = name_len;

            PtrList args;
            ptrlist_init(&args);
            if (!check(p, TOK_RPAREN)) {
                do {
                    ptrlist_push(&args, parse_expr(p));
                } while (match(p, TOK_COMMA));
            }
            expect(p, TOK_RPAREN, "expected ')' after call args");

            n->as.call.args = (Node**)args.items;
            n->as.call.arg_count = args.count;
            return n;
        }

        if (match(p, TOK_LBRACKET)) {
            Node* n = ast_new(p->arena, NODE_INDEX, line);
            n->as.index.name = name;
            n->as.index.name_len = name_len;
            n->as.index.index = parse_expr(p);
            expect(p, TOK_RBRACKET, "expected ']' after array index");
            return n;
        }

        Node* n = ast_new(p->arena, NODE_IDENT, line);
        n->as.ident.name = name;
        n->as.ident.len = name_len;
        return n;
    }

    error_at(p, p->cur, "expected expression");
    advance(p);
    return ast_new(p->arena, NODE_NUM, line);
}

// unary := ('!' | '-') unary | primary
static Node* parse_unary(Parser* p) {
    if (check(p, TOK_BANG) || check(p, TOK_MINUS)) {
        TokenKind op = p->cur.kind;
        int line = p->cur.line;
        advance(p);
        Node* n = ast_new(p->arena, NODE_UNARY, line);
        n->as.unary.op = op;
        n->as.unary.operand = parse_unary(p);
        return n;
    }
    return parse_primary(p);
}

// term := unary (('*' | '/') unary)*
static Node* parse_term(Parser* p) {
    Node* left = parse_unary(p);
    while (check(p, TOK_STAR) || check(p, TOK_SLASH)) {
        TokenKind op = p->cur.kind;
        int line = p->cur.line;
        advance(p);
        Node* right = parse_unary(p);
        Node* n = ast_new(p->arena, NODE_BINOP, line);
        n->as.binop.op = op;
        n->as.binop.left = left;
        n->as.binop.right = right;
        left = n;
    }
    return left;
}

// additive := term (('+' | '-') term)*
static Node* parse_additive(Parser* p) {
    Node* left = parse_term(p);
    while (check(p, TOK_PLUS) || check(p, TOK_MINUS)) {
        TokenKind op = p->cur.kind;
        int line = p->cur.line;
        advance(p);
        Node* right = parse_term(p);
        Node* n = ast_new(p->arena, NODE_BINOP, line);
        n->as.binop.op = op;
        n->as.binop.left = left;
        n->as.binop.right = right;
        left = n;
    }
    return left;
}

// comparison := additive (('==' | '!=' | '<' | '>' | '<=' | '>=') additive)*
static Node* parse_comparison(Parser* p) {
    Node* left = parse_additive(p);
    while (check(p, TOK_EQ) || check(p, TOK_NEQ) || check(p, TOK_LT) ||
           check(p, TOK_GT) || check(p, TOK_LE) || check(p, TOK_GE)) {
        TokenKind op = p->cur.kind;
        int line = p->cur.line;
        advance(p);
        Node* right = parse_additive(p);
        Node* n = ast_new(p->arena, NODE_BINOP, line);
        n->as.binop.op = op;
        n->as.binop.left = left;
        n->as.binop.right = right;
        left = n;
    }
    return left;
}

// logical_and := comparison ('&&' comparison)*
static Node* parse_logical_and(Parser* p) {
    Node* left = parse_comparison(p);
    while (check(p, TOK_AND_AND)) {
        int line = p->cur.line;
        advance(p);
        Node* right = parse_comparison(p);
        Node* n = ast_new(p->arena, NODE_BINOP, line);
        n->as.binop.op = TOK_AND_AND;
        n->as.binop.left = left;
        n->as.binop.right = right;
        left = n;
    }
    return left;
}

// logical_or := logical_and ('||' logical_and)*
static Node* parse_logical_or(Parser* p) {
    Node* left = parse_logical_and(p);
    while (check(p, TOK_OR_OR)) {
        int line = p->cur.line;
        advance(p);
        Node* right = parse_logical_and(p);
        Node* n = ast_new(p->arena, NODE_BINOP, line);
        n->as.binop.op = TOK_OR_OR;
        n->as.binop.left = left;
        n->as.binop.right = right;
        left = n;
    }
    return left;
}

static Node* parse_expr(Parser* p) { return parse_logical_or(p); }

// stmt := var_decl | assign_or_expr | if_stmt | return_stmt
//
// var_decl forms:
//   IDENT ':=' expr ';'              -> const, inferred type
//   'var' IDENT ':=' expr ';'        -> var, inferred type
//   IDENT ':' type '=' expr ';'      -> const, explicit type
//   'var' IDENT ':' type ';'         -> var, explicit type, no init
//   'var' IDENT ':' type '=' expr ';'-> var, explicit type, with init
//
// assign form:
//   IDENT '=' expr ';'               -> reassign existing var
static Node* parse_var_decl(Parser* p, int is_var_kw) {
    int line = p->cur.line;
    const char* name = p->cur.start;
    int name_len = p->cur.len;
    expect(p, TOK_IDENT, "expected identifier");

    Node* n = ast_new(p->arena, NODE_VAR_DECL, line);
    n->as.var_decl.name = name;
    n->as.var_decl.name_len = name_len;
    n->as.var_decl.is_const = !is_var_kw;
    n->as.var_decl.init = NULL;
    n->as.var_decl.type.name = NULL;
    n->as.var_decl.type.len = 0;
    n->as.var_decl.type.is_array = 0;
    n->as.var_decl.type.array_len = 0;

    if (match(p, TOK_DEFINE)) {
        // ident := expr   (type inferred)
        n->as.var_decl.init = parse_expr(p);
    } else if (match(p, TOK_COLON)) {
        // ident : type [= expr]
        n->as.var_decl.type = parse_type(p);
        if (match(p, TOK_ASSIGN)) {
            n->as.var_decl.init = parse_expr(p);
        }
    } else {
        error_at(p, p->cur, "expected ':=' or ':' in declaration");
    }

    expect(p, TOK_SEMI, "expected ';' after declaration");
    return n;
}

static Node* build_compound_assign(Parser* p, const char* name, int name_len,
                                   int line, TokenKind op) {
    Node* rhs = parse_expr(p);

    Node* ident = ast_new(p->arena, NODE_IDENT, line);
    ident->as.ident.name = name;
    ident->as.ident.len = name_len;

    Node* binop = ast_new(p->arena, NODE_BINOP, line);
    binop->as.binop.op = op;  // TOK_PLUS or TOK_MINUS
    binop->as.binop.left = ident;
    binop->as.binop.right = rhs;

    Node* assign = ast_new(p->arena, NODE_ASSIGN, line);
    assign->as.assign.name = name;
    assign->as.assign.name_len = name_len;
    assign->as.assign.value = binop;
    return assign;
}

static Node* parse_assign_or_expr(Parser* p) {
    int line = p->cur.line;
    const char* name = p->cur.start;
    int name_len = p->cur.len;
    advance(p);

    if (match(p, TOK_LBRACKET)) {
        Node* index_expr = parse_expr(p);
        expect(p, TOK_RBRACKET, "expected ']' after array index");
        expect(p, TOK_ASSIGN, "expected '=' after array index");

        Node* n = ast_new(p->arena, NODE_INDEX_ASSIGN, line);
        n->as.index_assign.name = name;
        n->as.index_assign.name_len = name_len;
        n->as.index_assign.index = index_expr;
        n->as.index_assign.value = parse_expr(p);
        expect(p, TOK_SEMI, "expected ';' after assignment");
        return n;
    }

    if (match(p, TOK_ASSIGN)) {
        Node* n = ast_new(p->arena, NODE_ASSIGN, line);
        n->as.assign.name = name;
        n->as.assign.name_len = name_len;
        n->as.assign.value = parse_expr(p);
        expect(p, TOK_SEMI, "expected ';' after assignment");
        return n;
    }

    if (match(p, TOK_PLUS_ASSIGN)) {
        Node* n = build_compound_assign(p, name, name_len, line, TOK_PLUS);
        expect(p, TOK_SEMI, "expected ';' after assignment");
        return n;
    }

    if (match(p, TOK_MINUS_ASSIGN)) {
        Node* n = build_compound_assign(p, name, name_len, line, TOK_MINUS);
        expect(p, TOK_SEMI, "expected ';' after assignment");
        return n;
    }

    if (match(p, TOK_STAR_ASSIGN)) {
        Node* n = build_compound_assign(p, name, name_len, line, TOK_STAR);
        expect(p, TOK_SEMI, "expected ';' after assignment");
        return n;
    }

    if (match(p, TOK_SLASH_ASSIGN)) {
        Node* n = build_compound_assign(p, name, name_len, line, TOK_SLASH);
        expect(p, TOK_SEMI, "expected ';' after assignment");
        return n;
    }

    Node* n = ast_new(p->arena, NODE_IDENT, line);
    n->as.ident.name = name;
    n->as.ident.len = name_len;
    expect(p, TOK_SEMI, "expected ';' after expression statement");
    return n;
}

static Node* parse_if(Parser* p) {
    int line = p->cur.line;
    advance(p);  // consume 'if'
    expect(p, TOK_LPAREN, "expected '(' after if");
    Node* cond = parse_expr(p);
    expect(p, TOK_RPAREN, "expected ')' after if condition");

    Node* n = ast_new(p->arena, NODE_IF, line);
    n->as.if_stmt.cond = cond;
    n->as.if_stmt.then_block = parse_block(p);
    n->as.if_stmt.else_block = NULL;

    if (match(p, TOK_KW_ELSE)) {
        n->as.if_stmt.else_block = parse_block(p);
    }

    return n;
}

static Node* parse_return(Parser* p) {
    int line = p->cur.line;
    advance(p);  // consume 'return'
    Node* n = ast_new(p->arena, NODE_RETURN, line);
    if (!check(p, TOK_SEMI)) {
        n->as.return_stmt.expr = parse_expr(p);
    } else {
        n->as.return_stmt.expr = NULL;
    }
    expect(p, TOK_SEMI, "expected ';' after return");
    return n;
}

// scans ahead (without consuming) to determine whether the loop header
// contains a top-level ';' before its closing ')' -- if so, it's the
// three-clause for-style form; otherwise it's the while-style single-cond
// form. must be called right after consuming the loop's opening '('.
static int loop_is_three_clause(Parser* p) {
    Lexer saved_lx = p->lx;
    Token saved_cur = p->cur;
    Token saved_prev = p->prev;

    int depth = 0;
    int result = 0;

    for (;;) {
        if (check(p, TOK_EOF)) break;
        if (check(p, TOK_LPAREN)) {
            depth++;
        } else if (check(p, TOK_RPAREN)) {
            if (depth == 0) break;  // reached loop header's closing ')'
            depth--;
        } else if (check(p, TOK_SEMI) && depth == 0) {
            result = 1;
            break;
        }
        advance(p);
    }

    p->lx = saved_lx;
    p->cur = saved_cur;
    p->prev = saved_prev;
    return result;
}

// init clause: 'var' IDENT ':=' expr  |  IDENT ':=' expr  |  IDENT '=' expr
// (reuses parse_var_decl / parse_assign_or_expr, which both consume the
// trailing ';' -- exactly what's needed between the init and cond clauses)
static Node* parse_loop_init(Parser* p) {
    if (check(p, TOK_KW_VAR)) {
        advance(p);
        return parse_var_decl(p, 1);
    }

    if (check(p, TOK_IDENT)) {
        Lexer saved_lx = p->lx;
        Token saved_cur = p->cur;
        Token saved_prev = p->prev;

        advance(p);
        if (check(p, TOK_DEFINE) || check(p, TOK_COLON)) {
            p->lx = saved_lx;
            p->cur = saved_cur;
            p->prev = saved_prev;
            return parse_var_decl(p, 0);
        }

        p->lx = saved_lx;
        p->cur = saved_cur;
        p->prev = saved_prev;
        return parse_assign_or_expr(p);
    }

    error_at(p, p->cur, "expected loop init clause");
    return NULL;
}

// step clause: IDENT '=' expr | IDENT '+=' expr | IDENT '-=' expr
//            | IDENT '*=' expr | IDENT '/=' expr
// (does NOT consume a trailing ';' -- it's immediately followed by ')')
static Node* parse_loop_step(Parser* p) {
    if (!check(p, TOK_IDENT)) {
        error_at(p, p->cur, "expected loop step statement");
        return NULL;
    }

    int line = p->cur.line;
    const char* name = p->cur.start;
    int name_len = p->cur.len;
    advance(p);

    if (match(p, TOK_ASSIGN)) {
        Node* n = ast_new(p->arena, NODE_ASSIGN, line);
        n->as.assign.name = name;
        n->as.assign.name_len = name_len;
        n->as.assign.value = parse_expr(p);
        return n;
    }
    if (match(p, TOK_PLUS_ASSIGN)) {
        return build_compound_assign(p, name, name_len, line, TOK_PLUS);
    }
    if (match(p, TOK_MINUS_ASSIGN)) {
        return build_compound_assign(p, name, name_len, line, TOK_MINUS);
    }
    if (match(p, TOK_STAR_ASSIGN)) {
        return build_compound_assign(p, name, name_len, line, TOK_STAR);
    }
    if (match(p, TOK_SLASH_ASSIGN)) {
        return build_compound_assign(p, name, name_len, line, TOK_SLASH);
    }

    error_at(p, p->cur, "expected assignment in loop step");
    return NULL;
}

// loop := 'loop' '{' block '}'                                (infinite)
//       | 'loop' '(' expr ')' '{' block '}'                   (while-style)
//       | 'loop' '(' init ';' expr ';' step ')' '{' block '}' (for-style)
static Node* parse_loop(Parser* p) {
    int line = p->cur.line;
    advance(p);  // consume 'loop'

    Node* n = ast_new(p->arena, NODE_LOOP, line);
    n->as.loop.init = NULL;
    n->as.loop.cond = NULL;
    n->as.loop.step = NULL;

    if (match(p, TOK_LPAREN)) {
        if (loop_is_three_clause(p)) {
            n->as.loop.init = parse_loop_init(p);
            n->as.loop.cond = parse_expr(p);
            expect(p, TOK_SEMI, "expected ';' after loop condition");
            n->as.loop.step = parse_loop_step(p);
            expect(p, TOK_RPAREN, "expected ')' after loop step");
        } else {
            n->as.loop.cond = parse_expr(p);
            expect(p, TOK_RPAREN, "expected ')' after loop condition");
        }
    }
    // no '(' at all -> infinite loop, nothing more to parse before '{'

    n->as.loop.body = parse_block(p);
    return n;
}

static Node* parse_stmt(Parser* p) {
    if (check(p, TOK_KW_VAR)) {
        advance(p);  // consume 'var'
        return parse_var_decl(p, 1);
    }

    if (check(p, TOK_KW_IF)) {
        return parse_if(p);
    }

    if (check(p, TOK_KW_LOOP)) {
        return parse_loop(p);
    }

    if (check(p, TOK_KW_RETURN)) {
        return parse_return(p);
    }

    if (check(p, TOK_KW_BREAK)) {
        int line = p->cur.line;
        advance(p);
        expect(p, TOK_SEMI, "expected ';' after break");
        return ast_new(p->arena, NODE_BREAK, line);
    }

    if (check(p, TOK_KW_CONTINUE)) {
        int line = p->cur.line;
        advance(p);
        expect(p, TOK_SEMI, "expected ';' after continue");
        return ast_new(p->arena, NODE_CONTINUE, line);
    }

    if (check(p, TOK_IDENT)) {
        // lookahead: is this "ident := ..." or "ident : type ..." (decl),
        // or "ident = ..." (assign), or bare "ident;" (illegal expr stmt)?
        Lexer saved_lx = p->lx;
        Token saved_cur = p->cur;
        Token saved_prev = p->prev;

        advance(p);  // consume ident tentatively
        if (check(p, TOK_DEFINE) || check(p, TOK_COLON)) {
            // rewind and parse as a declaration
            p->lx = saved_lx;
            p->cur = saved_cur;
            p->prev = saved_prev;
            return parse_var_decl(p, 0);
        }

        // rewind and parse as assign-or-bare-expr
        p->lx = saved_lx;
        p->cur = saved_cur;
        p->prev = saved_prev;
        return parse_assign_or_expr(p);
    }

    error_at(p, p->cur, "expected statement");
    advance(p);
    return ast_new(p->arena, NODE_NUM, p->cur.line);
}

static Node* parse_block(Parser* p) {
    int line = p->cur.line;
    expect(p, TOK_LBRACE, "expected '{'");

    Node* n = ast_new(p->arena, NODE_BLOCK, line);
    ptrlist_init(&n->as.block.stmts);

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        ptrlist_push(&n->as.block.stmts, parse_stmt(p));
    }

    expect(p, TOK_RBRACE, "expected '}'");
    return n;
}

// func_decl := 'fn' IDENT ':' type '<-' '(' params ')' block
static Node* parse_func_decl(Parser* p) {
    int line = p->cur.line;
    advance(p);  // consume 'fn'

    const char* name = p->cur.start;
    int name_len = p->cur.len;
    expect(p, TOK_IDENT, "expected function name");

    expect(p, TOK_COLON, "expected ':' after function name");
    TypeRef ret_type = parse_type(p);
    expect(p, TOK_ARROW, "expected '<-' before parameter list");
    expect(p, TOK_LPAREN, "expected '(' after '<-'");

    PtrList params;
    ptrlist_init(&params);

    if (!check(p, TOK_RPAREN)) {
        do {
            Param* param = arena_alloc(p->arena, sizeof(Param));
            param->name = p->cur.start;
            param->name_len = p->cur.len;
            expect(p, TOK_IDENT, "expected parameter name");
            expect(p, TOK_COLON, "expected ':' after parameter name");
            param->type = parse_type(p);
            ptrlist_push(&params, param);
        } while (match(p, TOK_COMMA));
    }
    expect(p, TOK_RPAREN, "expected ')' after parameters");

    Node* n = ast_new(p->arena, NODE_FUNC_DECL, line);
    n->as.func_decl.name = name;
    n->as.func_decl.name_len = name_len;
    n->as.func_decl.return_type = ret_type;

    // flatten PtrList<Param*> into a contiguous Param array
    Param* param_arr = arena_alloc(
        p->arena, sizeof(Param) * (params.count > 0 ? params.count : 1));
    for (int i = 0; i < params.count; i++) {
        param_arr[i] = *(Param*)params.items[i];
    }
    n->as.func_decl.params = param_arr;
    n->as.func_decl.param_count = params.count;

    n->as.func_decl.body = parse_block(p);
    return n;
}

Node* parser_parse_program(Parser* p) {
    Node* n = ast_new(p->arena, NODE_PROGRAM, p->cur.line);
    ptrlist_init(&n->as.program.funcs);

    while (!check(p, TOK_EOF)) {
        if (check(p, TOK_KW_FN)) {
            ptrlist_push(&n->as.program.funcs, parse_func_decl(p));
        } else {
            error_at(p, p->cur, "expected 'fn'");
            advance(p);
        }
    }

    return n;
}
