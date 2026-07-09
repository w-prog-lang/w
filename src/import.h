#ifndef WLANG_IMPORT_H
#define WLANG_IMPORT_H

#include "ast.h"
#include "util.h"

// resolves every #import in `program` (which was parsed from `main_path`):
// each '.wlang' import is read, parsed, and its functions/structs merged into
// `program`; each '.h' import is collected (deduplicated) back onto the
// program's import list for codegen to emit as a C #include. the source
// buffer of every imported file is pushed onto `buffers` -- the AST points
// into them, so the caller must keep them alive until codegen has run.
// returns 0 on success, nonzero if any import failed to resolve or parse.
int imports_resolve(Node* program, const char* main_path, Arena* arena,
                    PtrList* buffers);

#endif
