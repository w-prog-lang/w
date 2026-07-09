#include "import.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"

typedef struct {
    Node* root;
    Arena* arena;
    PtrList* buffers;  // owned source buffers of imported files
    PtrList visited;   // resolved paths of '.wlang' files already merged
    int had_error;
} Importer;

static void import_error(Importer* im, int line, const char* msg,
                         const char* path, int len) {
    fprintf(stderr, "import error at line %d: %s: '%.*s'\n", line, msg, len,
            path);
    im->had_error = 1;
}

static char* read_file_or_null(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
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

// directory prefix of `path` including the trailing '/', or "" if none --
// import paths are resolved relative to the file that names them
static const char* dir_of(Importer* im, const char* path) {
    const char* slash = strrchr(path, '/');
    if (!slash) return "";
    size_t n = (size_t)(slash - path) + 1;
    char* dir = arena_alloc(im->arena, n + 1);
    memcpy(dir, path, n);
    dir[n] = '\0';
    return dir;
}

static const char* join_path(Importer* im, const char* dir, const char* path,
                             int len) {
    size_t dlen = strlen(dir);
    char* full = arena_alloc(im->arena, dlen + (size_t)len + 1);
    memcpy(full, dir, dlen);
    memcpy(full + dlen, path, (size_t)len);
    full[dlen + len] = '\0';
    return full;
}

static int already_visited(Importer* im, const char* full) {
    for (int i = 0; i < im->visited.count; i++) {
        if (strcmp((const char*)im->visited.items[i], full) == 0) return 1;
    }
    return 0;
}

static void add_c_include(PtrList* c_includes, Node* imp) {
    for (int i = 0; i < c_includes->count; i++) {
        Node* prev = (Node*)c_includes->items[i];
        if (prev->as.import.path_len == imp->as.import.path_len &&
            strncmp(prev->as.import.path, imp->as.import.path,
                    imp->as.import.path_len) == 0) {
            return;  // this header is already going to be included
        }
    }
    ptrlist_push(c_includes, imp);
}

static void process_program(Importer* im, Node* file_prog,
                            const char* file_dir, PtrList* c_includes) {
    for (int i = 0; i < file_prog->as.program.imports.count; i++) {
        Node* imp = (Node*)file_prog->as.program.imports.items[i];

        if (imp->as.import.is_c) {
            add_c_include(c_includes, imp);
            continue;
        }

        const char* full = join_path(im, file_dir, imp->as.import.path,
                                     imp->as.import.path_len);
        if (already_visited(im, full)) continue;  // merge each file once
        ptrlist_push(&im->visited, (void*)full);

        size_t len;
        char* src = read_file_or_null(full, &len);
        if (!src) {
            import_error(im, imp->line, "cannot open imported file", full,
                         (int)strlen(full));
            continue;
        }
        ptrlist_push(im->buffers, src);

        Parser p;
        parser_init(&p, src, len, im->arena);
        Node* lib = parser_parse_program(&p);
        if (p.had_error) {
            import_error(im, imp->line, "errors in imported file", full,
                         (int)strlen(full));
            continue;
        }

        // depth-first: resolve the library's own imports first, then merge
        // its declarations into the root program
        process_program(im, lib, dir_of(im, full), c_includes);

        for (int j = 0; j < lib->as.program.structs.count; j++) {
            ptrlist_push(&im->root->as.program.structs,
                         lib->as.program.structs.items[j]);
        }
        for (int j = 0; j < lib->as.program.funcs.count; j++) {
            ptrlist_push(&im->root->as.program.funcs,
                         lib->as.program.funcs.items[j]);
        }
    }
}

int imports_resolve(Node* program, const char* main_path, Arena* arena,
                    PtrList* buffers) {
    Importer im;
    im.root = program;
    im.arena = arena;
    im.buffers = buffers;
    ptrlist_init(&im.visited);
    im.had_error = 0;

    // the root file counts as visited, so an import cycle back to it is cut
    ptrlist_push(&im.visited, (void*)main_path);

    PtrList c_includes;
    ptrlist_init(&c_includes);

    process_program(&im, program, dir_of(&im, main_path), &c_includes);

    // after resolution the import list holds only the C headers codegen must
    // emit -- every '.wlang' import has been merged away
    free(program->as.program.imports.items);
    program->as.program.imports = c_includes;

    free(im.visited.items);
    return im.had_error;
}
