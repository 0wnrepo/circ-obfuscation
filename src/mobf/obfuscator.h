#pragma once

#include "../obfuscator.h"

typedef struct {
    size_t npowers;
    size_t symlen;
    bool sigma;
} mobf_obf_params_t;

extern obfuscator_vtable mobf_obfuscator_vtable;
extern op_vtable mobf_op_vtable;
