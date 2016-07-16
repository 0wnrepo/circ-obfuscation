#include "obfuscate.h"

#include "util.h"
#include <clt13.h>
#include <gmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

////////////////////////////////////////////////////////////////////////////////
// boilerplate

void obfuscation_init (obfuscation *obf, secret_params *p)/*{{{*/
{
    obf->op = p->op;
    obf_params op = *(obf->op);

    obf->Zstar = lin_malloc(sizeof(encoding));
    encoding_init(obf->Zstar, p->op);

    obf->Rks = lin_malloc(op.c * sizeof(encoding**));
    for (int k = 0; k < op.c; k++) {
        obf->Rks[k] = lin_malloc(op.q * sizeof(encoding*));
        for (int s = 0; s < op.q; s++) {
            obf->Rks[k][s] = lin_malloc(sizeof(encoding));
            encoding_init(obf->Rks[k][s], p->op);
        }
    }

    obf->Zksj = lin_malloc(op.c * sizeof(encoding***));
    for (int k = 0; k < op.c; k++) {
        obf->Zksj[k] = lin_malloc(op.q * sizeof(encoding**));
        for (int s = 0; s < op.q; s++) {
            obf->Zksj[k][s] = lin_malloc(op.ell * sizeof(encoding*));
            for (int j = 0; j < op.ell; j++) {
                obf->Zksj[k][s][j] = lin_malloc(sizeof(encoding));
                encoding_init(obf->Zksj[k][s][j], p->op);
            }
        }
    }

    obf->Rc = lin_malloc(sizeof(encoding));
    encoding_init(obf->Rc, p->op);
    obf->Zcj = lin_malloc(op.m * sizeof(encoding*));
    for (int j = 0; j < op.m; j++) {
        obf->Zcj[j] = lin_malloc(sizeof(encoding));
        encoding_init(obf->Zcj[j], p->op);
    }

    obf->Rhatkso = lin_malloc(op.c * sizeof(encoding***));
    obf->Zhatkso = lin_malloc(op.c * sizeof(encoding***));
    for (int k = 0; k < op.c; k++) {
        obf->Rhatkso[k] = lin_malloc(op.q * sizeof(encoding**));
        obf->Zhatkso[k] = lin_malloc(op.q * sizeof(encoding**));
        for (int s = 0; s < op.q; s++) {
            obf->Rhatkso[k][s] = lin_malloc(op.gamma * sizeof(encoding*));
            obf->Zhatkso[k][s] = lin_malloc(op.gamma * sizeof(encoding*));
            for (int o = 0; o < op.gamma; o++) {
                obf->Rhatkso[k][s][o] = lin_malloc(sizeof(encoding));
                obf->Zhatkso[k][s][o] = lin_malloc(sizeof(encoding));
                encoding_init(obf->Rhatkso[k][s][o], p->op);
                encoding_init(obf->Zhatkso[k][s][o], p->op);
            }
        }
    }

    obf->Rhato = lin_malloc(op.gamma * sizeof(encoding*));
    obf->Zhato = lin_malloc(op.gamma * sizeof(encoding*));
    for (int o = 0; o < op.gamma; o++) {
        obf->Rhato[o] = lin_malloc(sizeof(encoding));
        obf->Zhato[o] = lin_malloc(sizeof(encoding));
        encoding_init(obf->Rhato[o], p->op);
        encoding_init(obf->Zhato[o], p->op);
    }

    obf->Rbaro = lin_malloc(op.gamma * sizeof(encoding*));
    obf->Zbaro = lin_malloc(op.gamma * sizeof(encoding*));
    for (int o = 0; o < op.gamma; o++) {
        obf->Rbaro[o] = lin_malloc(sizeof(encoding));
        obf->Zbaro[o] = lin_malloc(sizeof(encoding));
        encoding_init(obf->Rbaro[o], p->op);
        encoding_init(obf->Zbaro[o], p->op);
    }
}
/*}}}*/
void obfuscation_clear (obfuscation *obf)/*{{{*/
{
    obf_params op = *(obf->op);

    encoding_clear(obf->Zstar);
    free(obf->Zstar);

    for (int k = 0; k < op.c; k++) {
        for (int s = 0; s < op.q; s++) {
            encoding_clear(obf->Rks[k][s]);
            free(obf->Rks[k][s]);
        }
        free(obf->Rks[k]);
    }
    free(obf->Rks);

    for (int k = 0; k < op.c; k++) {
        for (int s = 0; s < op.q; s++) {
            for (int j = 0; j < op.ell; j++) {
                encoding_clear(obf->Zksj[k][s][j]);
                free(obf->Zksj[k][s][j]);
            }
            free(obf->Zksj[k][s]);
        }
        free(obf->Zksj[k]);
    }
    free(obf->Zksj);

    encoding_clear(obf->Rc);
    free(obf->Rc);

    for (int j = 0; j < op.m; j++) {
        encoding_clear(obf->Zcj[j]);
        free(obf->Zcj[j]);
    }
    free(obf->Zcj);

    for (int k = 0; k < op.c; k++) {
        for (int s = 0; s < op.q; s++) {
            for (int o = 0; o < op.gamma; o++) {
                encoding_clear(obf->Rhatkso[k][s][o]);
                encoding_clear(obf->Zhatkso[k][s][o]);
                free(obf->Rhatkso[k][s][o]);
                free(obf->Zhatkso[k][s][o]);
            }
            free(obf->Rhatkso[k][s]);
            free(obf->Zhatkso[k][s]);
        }
        free(obf->Rhatkso[k]);
        free(obf->Zhatkso[k]);
    }
    free(obf->Rhatkso);
    free(obf->Zhatkso);

    for (int o = 0; o < op.gamma; o++) {
        encoding_clear(obf->Rhato[o]);
        encoding_clear(obf->Zhato[o]);
        free(obf->Rhato[o]);
        free(obf->Zhato[o]);
    }
    free(obf->Rhato);
    free(obf->Zhato);

    for (int o = 0; o < op.gamma; o++) {
        encoding_clear(obf->Rbaro[o]);
        encoding_clear(obf->Zbaro[o]);
        free(obf->Rbaro[o]);
        free(obf->Zbaro[o]);
    }
    free(obf->Rbaro);
    free(obf->Zbaro);
}
/*}}}*/

////////////////////////////////////////////////////////////////////////////////
// obfuscator

void obfuscate (obfuscation *obf, secret_params *p, aes_randstate_t rng)
{
    obf->op = p->op;

    // create ykj
    mpz_t **ykj = lin_malloc((p->op->c+1) * sizeof(mpz_t*));
    for (int k = 0; k < p->op->c; k++) {
        ykj[k] = lin_malloc(p->op->ell * sizeof(mpz_t));
        for (int j = 0; j < p->op->ell; j++) {
            mpz_init(ykj[k][j]);
            mpz_urandomm_aes(ykj[k][j], rng, get_moduli(p)[0]);
        }
    }
    // the cth ykj has length m (number of secret bits)
    ykj[p->op->c] = lin_malloc(p->op->m * sizeof(mpz_t));
    for (int j = 0; j < p->op->m; j++) {
        mpz_init(ykj[p->op->c][j]);
        mpz_urandomm_aes(ykj[p->op->c][j], rng, get_moduli(p)[0]);
    }

    // create whatk and what
    mpz_t **whatk = lin_malloc((p->op->c+1) * sizeof(mpz_t*));
    for (int k = 0; k < p->op->c; k++) {
        whatk[k] = mpz_vect_create(p->op->c+3);
        mpz_urandomm_vect_aes(whatk[k], get_moduli(p), p->op->c+3, rng);
        mpz_set_ui(whatk[k][k+2], 0);
    }
    mpz_t *what = mpz_vect_create(p->op->c+3);
    mpz_urandomm_vect_aes(what, get_moduli(p), p->op->c+3, rng);
    mpz_set_ui(what[p->op->c+2], 0);

    ////////////////////////////////////////////////////////////////////////////////
    // obfuscation

    encode_Zstar(obf->Zstar, p, rng);

    // encode Rks and Zksj
    #pragma omp parallel for schedule(dynamic,1) collapse(2)
    for (int k = 0; k < p->op->c; k++) {
        for (int s = 0; s < p->op->q; s++) {
            mpz_t *tmp = mpz_vect_create(p->op->c+3);
            mpz_urandomm_vect_aes(tmp, get_moduli(p), p->op->c+3, rng);
            encode_Rks(obf->Rks[k][s], p, rng, tmp, k, s);
            for (int j = 0; j < p->op->ell; j++) {
                encode_Zksj(obf->Zksj[k][s][j], p, rng, tmp, ykj[k][j], k, s, j);
            }
            mpz_vect_destroy(tmp, p->op->c+3);
        }
    }

    // encode Rc and Zcj
    mpz_t *rs = mpz_vect_create(p->op->c+3);
    mpz_urandomm_vect_aes(rs, get_moduli(p), p->op->c+3, rng);
    encode_Rc(obf->Rc, p, rng, rs);
    #pragma omp parallel for
    for (int j = 0; j < p->op->m; j++) {
        encode_Zcj(obf->Zcj[j], p, rng, rs, ykj[p->op->c][j], p->op->circ->consts[j]);
    }
    mpz_vect_destroy(rs, p->op->c+3);

    // encode Rhatkso and Zhatkso
    #pragma omp parallel for schedule(dynamic,1) collapse(3)
    for (int o = 0; o < p->op->gamma; o++) {
        for (int k = 0; k < p->op->c; k++) {
            for (int s = 0; s < p->op->q; s++) {
                mpz_t *tmp = mpz_vect_create(p->op->c+3);
                mpz_urandomm_vect_aes(tmp, get_moduli(p), p->op->c+3, rng);
                encode_Rhatkso(obf->Rhatkso[k][s][o], p, rng, tmp, k, s, o);
                encode_Zhatkso(obf->Zhatkso[k][s][o], p, rng, tmp, whatk[k], k, s, o);
                mpz_vect_destroy(tmp, p->op->c+3);
            }
        }
    }

    // encode Rhato and Zhato
    #pragma omp parallel for
    for (int o = 0; o < p->op->gamma; o++) {
        mpz_t *tmp = mpz_vect_create(p->op->c+3);
        mpz_urandomm_vect_aes(tmp, get_moduli(p), p->op->c+3, rng);
        encode_Rhato(obf->Rhato[o], p, rng, tmp, o);
        encode_Zhato(obf->Zhato[o], p, rng, tmp, what, o);
        mpz_vect_destroy(tmp, p->op->c+3);
    }

    // encode Rbaro and Zbaro
    #pragma omp parallel for
    for (int o = 0; o < p->op->gamma; o++) {
        mpz_t *tmp = mpz_vect_create(p->op->c+3);
        mpz_urandomm_vect_aes(tmp, get_moduli(p), p->op->c+3, rng);
        encode_Rbaro(obf->Rbaro[o], p, rng, tmp, o);
        encode_Zbaro(obf->Zbaro[o], p, rng, tmp, what, whatk, ykj, p->op->circ, o);
        mpz_vect_destroy(tmp, p->op->c+3);
    }

    ////////////////////////////////////////////////////////////////////////////////
    // cleanup

    // delete ykj
    for (int k = 0; k < p->op->c; k++) {
        for (int j = 0; j < p->op->ell; j++)
            mpz_clear(ykj[k][j]);
        free(ykj[k]);
    }
    for (int j = 0; j < p->op->m; j++)
        mpz_clear(ykj[p->op->c][j]);
    free(ykj[p->op->c]);
    free(ykj);

    // delete whatk and what
    for (int k = 0; k < p->op->c; k++) {
        mpz_vect_destroy(whatk[k], p->op->c+3);
    }
    free(whatk);
    mpz_vect_destroy(what, p->op->c+3);
}

void encode_Zstar (encoding *enc, secret_params *p, aes_randstate_t rng)
{
    mpz_t *inps = mpz_vect_create(p->op->c+3);
    mpz_set_ui(inps[0], 1);
    mpz_set_ui(inps[1], 1);
    mpz_urandomm_vect_aes(inps + 2, get_moduli(p) + 2, p->op->c+1, rng);

    level *vstar = level_create_vstar(p->op);
    encode(enc, inps, p->op->c+3, vstar, p, rng);

    mpz_vect_destroy(inps, p->op->c+3);
    level_destroy(vstar);
}


void encode_Rks (encoding *enc, secret_params *p, aes_randstate_t rng, mpz_t *rs, size_t k, size_t s)
{
    level *vsk = level_create_vks(p->op, k, s);
    encode(enc, rs, p->op->c+3, vsk, p, rng);
    level_destroy(vsk);
}

void encode_Zksj (
    encoding *enc,
    secret_params *p,
    aes_randstate_t rng,
    mpz_t *rs,
    mpz_t ykj,
    size_t k,
    size_t s,
    size_t j
) {
    mpz_t *w = mpz_vect_create(p->op->c+3);

    mpz_set(w[0], ykj);
    mpz_set_ui(w[1], bit(s,j));
    mpz_urandomm_vect_aes(w+2, get_moduli(p)+2, p->op->c+1, rng);

    mpz_vect_mul(w, w, rs, p->op->c+3);
    mpz_vect_mod(w, w, get_moduli(p), p->op->c+3);

    level *lvl = level_create_vks(p->op, k, s);
    level *vstar = level_create_vstar(p->op);
    level_add(lvl, lvl, vstar);

    encode(enc, w, p->op->c+3, lvl, p, rng);

    level_destroy(vstar);
    level_destroy(lvl);
    mpz_vect_destroy(w, p->op->c+3);
}

void encode_Rc (
    encoding *enc,
    secret_params *p,
    aes_randstate_t rng,
    mpz_t *rs
){
    level *vc = level_create_vc(p->op);
    encode(enc, rs, p->op->c+3, vc, p, rng);
    level_destroy(vc);
}

void encode_Zcj (
    encoding *enc,
    secret_params *p,
    aes_randstate_t rng,
    mpz_t *rs,
    mpz_t ykj,
    int const_val
) {
    mpz_t *w = mpz_vect_create(p->op->c+3);
    mpz_set(w[0], ykj);
    mpz_set_ui(w[1], const_val);
    mpz_urandomm_vect_aes(w+2, get_moduli(p)+2, p->op->c+1, rng);

    mpz_vect_mul(w, w, rs, p->op->c+3);
    mpz_vect_mod(w, w, get_moduli(p), p->op->c+3);

    level *lvl = level_create_vc(p->op);
    level *vstar = level_create_vstar(p->op);
    level_add(lvl, lvl, vstar);

    encode(enc, w, p->op->c+3, lvl, p, rng);

    level_destroy(vstar);
    level_destroy(lvl);
    mpz_vect_destroy(w, p->op->c+3);
}

void encode_Rhatkso(
    encoding *enc,
    secret_params *p,
    aes_randstate_t rng,
    mpz_t *rs,
    size_t k,
    size_t s,
    size_t o
) {
    level *vhatkso = level_create_vhatkso(p->op, k, s, o);
    encode(enc, rs, p->op->c+3, vhatkso, p, rng);
    level_destroy(vhatkso);
}

void encode_Zhatkso (
    encoding *enc,
    secret_params *p,
    aes_randstate_t rng,
    mpz_t *rs,
    mpz_t *whatk,
    size_t k,
    size_t s,
    size_t o
) {
    mpz_t *inp = mpz_vect_create(p->op->c+3);
    mpz_vect_mul(inp, whatk, rs, p->op->c+3);
    mpz_vect_mod(inp, inp, get_moduli(p), p->op->c+3);

    level *lvl   = level_create_vhatkso(p->op, k, s, o);
    level *vstar = level_create_vstar(p->op);
    level_add(lvl, lvl, vstar);

    encode(enc, inp, p->op->c+3, lvl, p, rng);

    level_destroy(vstar);
    level_destroy(lvl);
    mpz_vect_destroy(inp, p->op->c+3);
}

void encode_Rhato (
    encoding *enc,
    secret_params *p,
    aes_randstate_t rng,
    mpz_t *rs,
    size_t o
) {
    level *vhato = level_create_vhato(p->op, o);
    encode(enc, rs, p->op->c+3, vhato, p, rng);
    level_destroy(vhato);
}

void encode_Zhato (
    encoding *enc,
    secret_params *p,
    aes_randstate_t rng,
    mpz_t *rs,
    mpz_t *what,
    size_t o
) {
    mpz_t *inp = mpz_vect_create(p->op->c+3);
    mpz_vect_mul(inp, what, rs, p->op->c+3);
    mpz_vect_mod(inp, inp, get_moduli(p), p->op->c+3);

    level *vhato = level_create_vhato(p->op, o);
    level *lvl = level_create_vstar(p->op);
    level_add(lvl, lvl, vhato);

    encode(enc, inp, p->op->c+3, lvl, p, rng);

    level_destroy(lvl);
    level_destroy(vhato);
    mpz_vect_destroy(inp, p->op->c+3);
}

void encode_Rbaro(
    encoding *enc,
    secret_params *p,
    aes_randstate_t rng,
    mpz_t *rs,
    size_t o
) {
    level *vbaro = level_create_vbaro(p->op, o);
    encode(enc, rs, p->op->c+3, vbaro, p, rng);
    level_destroy(vbaro);
}

void encode_Zbaro(
    encoding *enc,
    secret_params *p,
    aes_randstate_t rng,
    mpz_t *rs,
    mpz_t *what,
    mpz_t **whatk,
    mpz_t **ykj,
    acirc *c,
    size_t o
) {
    mpz_t ybar;
    mpz_init(ybar);

    mpz_t *xs = mpz_vect_create(c->ninputs);
    for (int k = 0; k < p->op->c; k++) {
        for (int j = 0; j < p->op->ell; j++) {
            sym_id sym = {k, j};
            input_id id = p->op->rchunker(sym, c->ninputs, p->op->c);
            if (id >= c->ninputs)
                continue;
            mpz_set(xs[id], ykj[k][j]);
        }
    }
    acirc_eval_mpz_mod(ybar, c, c->outrefs[o], xs, ykj[p->op->c], get_moduli(p)[0]);

    mpz_t *tmp = mpz_vect_create(p->op->c+3);
    mpz_vect_set(tmp, what, p->op->c+3);
    for (int k = 0; k < p->op->c; k++) {
        mpz_vect_mul(tmp, tmp, whatk[k],      p->op->c+3);
        mpz_vect_mod(tmp, tmp, get_moduli(p), p->op->c+3);
    }

    mpz_t *w = mpz_vect_create(p->op->c+3);
    mpz_set(w[0], ybar);
    mpz_set_ui(w[1], 1);
    mpz_vect_mul(w, w, tmp,           p->op->c+3);
    mpz_vect_mod(w, w, get_moduli(p), p->op->c+3);

    mpz_vect_mul(w, w, rs,            p->op->c+3);
    mpz_vect_mod(w, w, get_moduli(p), p->op->c+3);

    level *lvl   = level_create_vbaro(p->op, o);
    level *vstar = level_create_vstar(p->op);
    level_mul_ui(vstar, vstar, p->op->D);
    level_add(lvl, lvl, vstar);

    encode(enc, w, p->op->c+3, lvl, p, rng);

    mpz_clear(ybar);
    mpz_vect_destroy(tmp, p->op->c+3);
    mpz_vect_destroy(xs, c->ninputs);
    mpz_vect_destroy(w, p->op->c+3);
    level_destroy(vstar);
    level_destroy(lvl);
}

////////////////////////////////////////////////////////////////////////////////
// serialization

void obfuscation_eq (const obfuscation *x, const obfuscation *y)
{
    obf_params op = *x->op;
    assert(encoding_eq(x->Zstar, y->Zstar));
    for (int k = 0; k < op.c; k++) {
        for (int s = 0; s < op.q; s++) {
            assert(encoding_eq(x->Rks[k][s], y->Rks[k][s]));
        }
    }

    for (int k = 0; k < op.c; k++) {
        for (int s = 0; s < op.q; s++) {
            for (int j = 0; j < op.ell; j++) {
                assert(encoding_eq(x->Zksj[k][s][j], y->Zksj[k][s][j]));
            }
        }
    }

    assert(encoding_eq(x->Rc, y->Rc));
    for (int j = 0; j < op.m; j++) {
        assert(encoding_eq(x->Zcj[j], y->Zcj[j]));
    }

    for (int k = 0; k < op.c; k++) {
        for (int s = 0; s < op.q; s++) {
            for (int o = 0; o < op.gamma; o++) {
                assert(encoding_eq(x->Rhatkso[k][s][o], y->Rhatkso[k][s][o]));
                assert(encoding_eq(x->Zhatkso[k][s][o], y->Zhatkso[k][s][o]));
            }
        }
    }

    for (int o = 0; o < op.gamma; o++) {
        assert(encoding_eq(x->Rhato[o], y->Rhato[o]));
        assert(encoding_eq(x->Zhato[o], y->Zhato[o]));
    }

    for (int o = 0; o < op.gamma; o++) {
        assert(encoding_eq(x->Rbaro[o], y->Rbaro[o]));
        assert(encoding_eq(x->Zbaro[o], y->Zbaro[o]));
    }
}

void obfuscation_write (FILE *const fp, const obfuscation *obf)
{
    obf_params op = *obf->op;

    encoding_write(fp, obf->Zstar);
    PUT_NEWLINE(fp);

    for (int k = 0; k < op.c; k++) {
        for (int s = 0; s < op.q; s++) {
            encoding_write(fp, obf->Rks[k][s]);
            PUT_NEWLINE(fp);
        }
    }

    for (int k = 0; k < op.c; k++) {
        for (int s = 0; s < op.q; s++) {
            for (int j = 0; j < op.ell; j++) {
                encoding_write(fp, obf->Zksj[k][s][j]);
                PUT_NEWLINE(fp);
            }
        }
    }

    encoding_write(fp, obf->Rc);
    PUT_NEWLINE(fp);
    for (int j = 0; j < op.m; j++) {
        encoding_write(fp, obf->Zcj[j]);
        PUT_NEWLINE(fp);
    }

    for (int k = 0; k < op.c; k++) {
        for (int s = 0; s < op.q; s++) {
            for (int o = 0; o < op.gamma; o++) {
                encoding_write(fp, obf->Rhatkso[k][s][o]);
                PUT_NEWLINE(fp);
                encoding_write(fp, obf->Zhatkso[k][s][o]);
                PUT_NEWLINE(fp);
            }
        }
    }

    for (int o = 0; o < op.gamma; o++) {
        encoding_write(fp, obf->Rhato[o]);
        PUT_NEWLINE(fp);
        encoding_write(fp, obf->Zhato[o]);
        PUT_NEWLINE(fp);
    }

    for (int o = 0; o < op.gamma; o++) {
        encoding_write(fp, obf->Rbaro[o]);
        PUT_NEWLINE(fp);
        encoding_write(fp, obf->Zbaro[o]);
        PUT_NEWLINE(fp);
    }
}

void obfuscation_read (obfuscation *obf, FILE *const fp, obf_params *p)
{
    obf->op = p;
    obf_params op = *p;

    obf->Zstar = lin_malloc(sizeof(encoding));
    encoding_read(obf->Zstar, fp);
    GET_NEWLINE(fp);

    obf->Rks = lin_malloc(op.c * sizeof(encoding**));
    for (int k = 0; k < op.c; k++) {
        obf->Rks[k] = lin_malloc(op.q * sizeof(encoding*));
        for (int s = 0; s < op.q; s++) {
            obf->Rks[k][s] = lin_malloc(sizeof(encoding));
            encoding_read(obf->Rks[k][s], fp);
            GET_NEWLINE(fp);
        }
    }

    obf->Zksj = lin_malloc(op.c * sizeof(encoding***));
    for (int k = 0; k < op.c; k++) {
        obf->Zksj[k] = lin_malloc(op.q * sizeof(encoding**));
        for (int s = 0; s < op.q; s++) {
            obf->Zksj[k][s] = lin_malloc(op.ell * sizeof(encoding*));
            for (int j = 0; j < op.ell; j++) {
                obf->Zksj[k][s][j] = lin_malloc(sizeof(encoding));
                encoding_read(obf->Zksj[k][s][j], fp);
                GET_NEWLINE(fp);
            }
        }
    }

    obf->Rc = lin_malloc(sizeof(encoding));
    encoding_read(obf->Rc, fp);
    GET_NEWLINE(fp);
    obf->Zcj = lin_malloc(op.m * sizeof(encoding*));
    for (int j = 0; j < op.m; j++) {
        obf->Zcj[j] = lin_malloc(sizeof(encoding));
        encoding_read(obf->Zcj[j], fp);
        GET_NEWLINE(fp);
    }

    obf->Rhatkso = lin_malloc(op.c * sizeof(encoding***));
    obf->Zhatkso = lin_malloc(op.c * sizeof(encoding***));
    for (int k = 0; k < op.c; k++) {
        obf->Rhatkso[k] = lin_malloc(op.q * sizeof(encoding**));
        obf->Zhatkso[k] = lin_malloc(op.q * sizeof(encoding**));
        for (int s = 0; s < op.q; s++) {
            obf->Rhatkso[k][s] = lin_malloc(op.gamma * sizeof(encoding*));
            obf->Zhatkso[k][s] = lin_malloc(op.gamma * sizeof(encoding*));
            for (int o = 0; o < op.gamma; o++) {
                obf->Rhatkso[k][s][o] = lin_malloc(sizeof(encoding));
                obf->Zhatkso[k][s][o] = lin_malloc(sizeof(encoding));
                encoding_read(obf->Rhatkso[k][s][o], fp);
                GET_NEWLINE(fp);
                encoding_read(obf->Zhatkso[k][s][o], fp);
                GET_NEWLINE(fp);
            }
        }
    }

    obf->Rhato = lin_malloc(op.gamma * sizeof(encoding*));
    obf->Zhato = lin_malloc(op.gamma * sizeof(encoding*));
    for (int o = 0; o < op.gamma; o++) {
        obf->Rhato[o] = lin_malloc(sizeof(encoding));
        obf->Zhato[o] = lin_malloc(sizeof(encoding));
        encoding_read(obf->Rhato[o], fp);
        GET_NEWLINE(fp);
        encoding_read(obf->Zhato[o], fp);
        GET_NEWLINE(fp);
    }

    obf->Rbaro = lin_malloc(op.gamma * sizeof(encoding*));
    obf->Zbaro = lin_malloc(op.gamma * sizeof(encoding*));
    for (int o = 0; o < op.gamma; o++) {
        obf->Rbaro[o] = lin_malloc(sizeof(encoding));
        obf->Zbaro[o] = lin_malloc(sizeof(encoding));
        encoding_read(obf->Rbaro[o], fp);
        GET_NEWLINE(fp);
        encoding_read(obf->Zbaro[o], fp);
        GET_NEWLINE(fp);
    }
}
