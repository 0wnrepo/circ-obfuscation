#include "obf_params.h"
#include "../mmap.h"
#include "../util.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>

/* TODO: set num_symbolic_inputs */

static obf_params_t *
_op_new(acirc *const circ, int flags)
{
    obf_params_t *p = calloc(1, sizeof(obf_params_t));
    p->rachel_input = (flags & LIN_FLAG_RACHEL_INPUT) > 0;
    p->m = circ->nconsts;
    p->gamma = circ->noutputs;
    p->types = my_calloc(p->gamma, sizeof(size_t *));

    int num_symbolic_inputs = 1;

    p->ell = ceil((double) circ->ninputs / (double) num_symbolic_inputs); // length of symbols
    if (p->rachel_input)
        p->q = p->ell;
    else
        p->q = pow((double) 2, (double) p->ell); // 2^ell
    p->c = num_symbolic_inputs;

    p->M = 0;
    for (size_t o = 0; o < p->gamma; o++) {
        p->types[o] = my_calloc(p->c+1, sizeof(size_t));
        type_degree(p->types[o], circ->outrefs[o], circ, p->c, chunker_in_order);

        for (size_t k = 0; k < p->c+1; k++) {
            if (p->types[o][k] > p->M) {
                p->M = p->types[o][k];
            }
        }
    }
    p->d = acirc_max_degree(circ);
    p->D = p->d + num_symbolic_inputs + 1;

    p->chunker  = chunker_in_order;
    p->rchunker = rchunker_in_order;
    p->circ = circ;

    return p;
}

static void
_op_free(obf_params_t *p)
{
    for (size_t i = 0; i < p->gamma; i++) {
        free(p->types[i]);
    }
    free(p->types);
    free(p);
}

static int
_op_fwrite(const obf_params_t *const params, FILE *const fp)
{
    (void) params; (void) fp;
    return ERR;
}

static int
_op_fread(obf_params_t *const params, FILE *const fp)
{
    (void) params; (void) fp;
    return ERR;
}

op_vtable lin_op_vtable =
{
    .new = _op_new,
    .free = _op_free,
    .fwrite = _op_fwrite,
    .fread = _op_fread,
};