// SPDX-License-Identifier: MIT
#ifndef MISC_H
#define MISC_H

#include <assert.h>

#define TODO_STRINGIFY_IMPL(x) #x
#define TODO_STRINGIFY(x) TODO_STRINGIFY_IMPL(x)

#define TODO(msg) \
    do { \
        assert(0 && (__FILE__ ":" TODO_STRINGIFY(__LINE__) ": " msg)); \
    } while (0)

#endif // MISC_H
