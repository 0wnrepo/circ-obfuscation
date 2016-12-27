#ifndef __ZIM__OBFUSCATOR_H__
#define __ZIM__OBFUSCATOR_H__

#include "../obfuscator.h"

typedef struct zim_obf_params_t {
    size_t npowers;
} zim_obf_params_t;

extern obfuscator_vtable zim_obfuscator_vtable;
extern op_vtable zim_op_vtable;

#endif
