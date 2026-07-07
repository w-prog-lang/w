#ifndef WLANG_SEMA_H
#define WLANG_SEMA_H

#include "ast.h"

typedef struct {
    int had_error;
} SemaResult;

SemaResult sema_check(Node* program);

#endif
