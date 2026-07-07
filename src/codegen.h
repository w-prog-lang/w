#ifndef WLANG_CODEGEN_H
#define WLANG_CODEGEN_H

#include <stdio.h>

#include "ast.h"

void codegen_emit(Node* program, FILE* out);

#endif
