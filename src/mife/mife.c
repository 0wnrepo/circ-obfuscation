#include "mife.h"

#include "index_set.h"
#include "mife_params.h"
#include "reflist.h"
#include "vtables.h"
#include "util.h"

#include <assert.h>
#include <string.h>
#include <threadpool.h>

typedef struct {
    threadpool *pool;
    pthread_mutex_t *lock;
    size_t *count;
    size_t total;
} pool_info_t;

typedef struct mife_t {
    const mmap_vtable *mmap;
    const circ_params_t *cp;
    const encoding_vtable *enc_vt;
    const pp_vtable *pp_vt;
    const sp_vtable *sp_vt;
    secret_params *sp;
    public_params *pp;
    encoding *Chatstar;
    encoding **zhat;            /* [m] */
    mife_ciphertext_t *constants;
    mpz_t *const_betas;
} mife_t;

typedef struct mife_sk_t {
    const mmap_vtable *mmap;
    const circ_params_t *cp;
    const encoding_vtable *enc_vt;
    const pp_vtable *pp_vt;
    const sp_vtable *sp_vt;
    secret_params *sp;
    public_params *pp;
    mpz_t *const_betas;
    bool local;
} mife_sk_t;

typedef struct mife_ek_t {
    const mmap_vtable *mmap;
    const circ_params_t *cp;
    const encoding_vtable *enc_vt;
    const pp_vtable *pp_vt;
    public_params *pp;
    encoding *Chatstar;
    encoding **zhat;            /* [m] */
    mife_ciphertext_t *constants;
    bool local;
} mife_ek_t;

typedef struct mife_ciphertext_t {
    const encoding_vtable *enc_vt;
    size_t slot;
    size_t npowers;
    encoding **xhat;             /* [d_i] */
    encoding **uhat;             /* [npowers] */
    encoding **what;             /* [m] */
} mife_ciphertext_t;

typedef struct {
    const encoding_vtable *vt;
    encoding *enc;
    mpz_t *inps;
    size_t nslots;
    index_set *ix;
    const secret_params *sp;
    pthread_mutex_t *lock;
    size_t *count;
    size_t total;
} encode_args_t;

typedef struct {
    const mmap_vtable *mmap;
    acircref ref;
    const acirc *c;
    mife_ciphertext_t **cts;
    const mife_ek_t *ek;
    bool *mine;
    int *ready;
    void *cache;
    ref_list **deps;
    threadpool *pool;
    int *rop;
    size_t *kappas;
} decrypt_args_t;

static mife_ciphertext_t *
_mife_encrypt(const mife_sk_t *sk, const size_t slot, const int *inputs,
              const size_t npowers, const size_t nthreads, aes_randstate_t rng,
              pool_info_t *_pi, mpz_t *betas);

static int
populate_circ_degrees(const circ_params_t *cp, size_t **deg, size_t *deg_max)
{
    const size_t has_consts = cp->circ->consts.n ? 1 : 0;
    const size_t noutputs = cp->m;
    const acirc *circ = cp->circ;
    acirc_memo *memo = acirc_memo_new(circ);
    for (size_t i = 0; i < cp->n - has_consts; ++i) {
        for (size_t o = 0; o < noutputs; ++o) {
            deg[i][o] = acirc_var_degree(circ, circ->outputs.buf[o], i, memo);
            if (deg[i][o] > deg_max[i])
                deg_max[i] = deg[i][o];
        }
    }
    if (has_consts) {
        for (size_t o = 0; o < noutputs; ++o) {
            deg[cp->n - 1][o] = acirc_max_const_degree(circ);
        }
        deg_max[cp->n - 1] = acirc_max_const_degree(circ);
    }
    acirc_memo_free(memo, circ);
    return OK;
}

static int
populate_circ_input(const circ_params_t *cp, size_t slot, mpz_t *inputs,
                    mpz_t *consts, const mpz_t *betas)
{
    const size_t nconsts = cp->circ->consts.n;
    const size_t has_consts = nconsts ? 1 : 0;
    size_t idx = 0;
    for (size_t i = 0; i < cp->n - has_consts; ++i) {
        for (size_t j = 0; j < cp->ds[i]; ++j) {
            if (i == slot)
                mpz_set   (inputs[idx + j], betas[j]);
            else
                mpz_set_ui(inputs[idx + j], 1);
        }
        idx += cp->ds[i];
    }
    for (size_t i = 0; i < nconsts; ++i) {
        if (has_consts && slot == cp->n - 1)
            mpz_set   (consts[i], betas[i]);
        else
            mpz_set_ui(consts[i], 1);
    }
    return OK;
}

static int
eval_circ(const circ_params_t *cp, size_t slot, mpz_t *outputs,
          const mpz_t *inputs, const mpz_t *consts, const mpz_t *moduli)
{
    const size_t nrefs = acirc_nrefs(cp->circ);
    bool *known;
    mpz_t *cache;

    known = my_calloc(nrefs, sizeof known[0]);
    cache = my_calloc(nrefs, sizeof cache[0]);
    for (size_t o = 0; o < cp->m; ++o) {
        acirc_eval_mpz_mod_memo(outputs[o], cp->circ, cp->circ->outputs.buf[o],
                                inputs, consts, moduli[1 + slot], known, cache);
    }
    for (size_t i = 0; i < nrefs; ++i) {
        if (known[i])
            mpz_clear(cache[i]);
    }
    free(cache);
    free(known);
    return OK;
}

static void
encode_worker(void *wargs)
{
    encode_args_t *const args = wargs;

    encode(args->vt, args->enc, args->inps, args->nslots, args->ix, args->sp);
    if (g_verbose) {
        pthread_mutex_lock(args->lock);
        print_progress(++*args->count, args->total);
        pthread_mutex_unlock(args->lock);
    }
    mpz_vect_free(args->inps, args->nslots);
    index_set_free(args->ix);
    free(args);
}

static void
__encode(threadpool *pool, const encoding_vtable *vt, encoding *enc, mpz_t *inps,
         size_t nslots, index_set *ix, const secret_params *sp,
         pthread_mutex_t *lock, size_t *count, size_t total)
{
    encode_args_t *args = my_calloc(1, sizeof args[0]);
    args->vt = vt;
    args->enc = enc;
    args->inps = mpz_vect_new(nslots);
    for (size_t i = 0; i < nslots; ++i) {
        mpz_set(args->inps[i], inps[i]);
    }
    args->nslots = nslots;
    args->ix = ix;
    args->sp = sp;
    args->lock = lock;
    args->count = count;
    args->total = total;
    threadpool_add_job(pool, encode_worker, args);
}

void
mife_free(mife_t *mife)
{
    if (mife == NULL)
        return;
    if (mife->Chatstar)
        encoding_free(mife->enc_vt, mife->Chatstar);
    if (mife->zhat) {
        for (size_t o = 0; o < mife->cp->m; ++o)
            encoding_free(mife->enc_vt, mife->zhat[o]);
        free(mife->zhat);
    }
    if (mife->constants)
        mife_ciphertext_free(mife->constants, mife->cp);
    if (mife->pp)
        public_params_free(mife->pp_vt, mife->pp);
    if (mife->sp)
        secret_params_free(mife->sp_vt, mife->sp);
    free(mife);
}

mife_t *
mife_setup(const mmap_vtable *mmap, const obf_params_t *op, const size_t secparam,
           size_t *kappa, const size_t nthreads, aes_randstate_t rng)
{
    int result = ERR;
    mife_t *mife;
    const circ_params_t *cp = &op->cp;
    const size_t has_consts = cp->c ? 1 : 0;
    const size_t noutputs = cp->m;
    mpz_t *moduli = NULL;
    size_t **deg, *deg_max;
    threadpool *pool = threadpool_create(nthreads);
    pthread_mutex_t lock;
    size_t count = 0;
    size_t total = mife_num_encodings_setup(cp);
    index_set *const ix = index_set_new(mife_params_nzs(cp));
    mpz_t inps[1 + cp->n];
    mpz_vect_init(inps, 1 + cp->n);

    mife = my_calloc(1, sizeof mife[0]);
    mife->mmap = mmap;
    mife->cp = cp;
    mife->enc_vt = get_encoding_vtable(mmap);
    mife->pp_vt = get_pp_vtable(mmap);
    mife->sp_vt = get_sp_vtable(mmap);
    mife->sp = secret_params_new(mife->sp_vt, op, secparam, kappa, nthreads, rng);
    if (mife->sp == NULL)
        goto cleanup;
    mife->pp = public_params_new(mife->pp_vt, mife->sp_vt, mife->sp);
    if (mife->pp == NULL)
        goto cleanup;
    mife->zhat = my_calloc(noutputs, sizeof mife->zhat[0]);
    for (size_t o = 0; o < noutputs; ++o)
        mife->zhat[o] = encoding_new(mife->enc_vt, mife->pp_vt, mife->pp);
    if (has_consts)
        mife->const_betas = my_calloc(cp->c, sizeof mife->const_betas[0]);

    moduli = mpz_vect_create_of_fmpz(mmap->sk->plaintext_fields(mife->sp->sk),
                                     mmap->sk->nslots(mife->sp->sk));

    deg = my_calloc(cp->n, sizeof deg[0]);
    for (size_t i = 0; i < cp->n; ++i)
        deg[i] = my_calloc(noutputs, sizeof deg[i][0]);
    deg_max = my_calloc(cp->n, sizeof deg_max[0]);

    populate_circ_degrees(cp, deg, deg_max);

    pthread_mutex_init(&lock, NULL);

    pool_info_t pi = {
        .pool = pool,
        .lock = &lock,
        .count = &count,
        .total = total,
    };
    
    if (g_verbose)
        print_progress(count, total);

    for (size_t o = 0; o < noutputs; ++o) {
        mpz_t delta;
        mpz_init(delta);
        mpz_randomm_inv(delta, rng, moduli[0]);
        mpz_set(inps[0], delta);
        index_set_clear(ix);
        for (size_t i = 0; i < cp->n; ++i) {
            mpz_set_ui(inps[1 + i], 1);
            IX_W(ix, cp, i) = 1;
            IX_X(ix, cp, i) = deg_max[i] - deg[i][o];
        }
        IX_Z(ix) = 1;
        __encode(pool, mife->enc_vt, mife->zhat[o], inps, 1 + cp->n,
                 index_set_copy(ix), mife->sp, &lock, &count, total);
        mpz_clear(delta);
    }

    if (has_consts) {
        mife_sk_t *sk = mife_sk(mife);
        mife->constants = _mife_encrypt(sk, cp->n - 1, cp->circ->consts.buf, 1,
                                        nthreads, rng, &pi, mife->const_betas);
        if (mife->constants == NULL) {
            fprintf(stderr, "error: mife setup: unable to encrypt constants\n");
            goto cleanup;

        }
        mife_sk_free(sk);
        mife->Chatstar = NULL;
    } else {
        mife->constants = NULL;
        mife->Chatstar = encoding_new(mife->enc_vt, mife->pp_vt, mife->pp);
        index_set_clear(ix);
        mpz_set_ui(inps[0], 0);
        for (size_t i = 0; i < cp->n; ++i) {
            mpz_set_ui(inps[1 + i], 1);
            IX_X(ix, cp, i) = deg_max[i];
        }
        IX_Z(ix) = 1;
        __encode(pool, mife->enc_vt, mife->Chatstar, inps, 1 + cp->n,
                 index_set_copy(ix), mife->sp, &lock, &count, total);
    }

    result = OK;
cleanup:
    index_set_free(ix);
    mpz_vect_clear(inps, 1 + cp->n);
    for (size_t i = 0; i < cp->n; ++i)
        free(deg[i]);
    free(deg);
    free(deg_max);
    mpz_vect_free(moduli, mmap->sk->nslots(mife->sp->sk));
    threadpool_destroy(pool);
    pthread_mutex_destroy(&lock);
    if (result == OK)
        return mife;
    else {
        mife_free(mife);
        return NULL;
    }
}

mife_sk_t *
mife_sk(const mife_t *mife)
{
    mife_sk_t *sk;
    sk = my_calloc(1, sizeof sk[0]);
    sk->mmap = mife->mmap;
    sk->cp = mife->cp;
    sk->enc_vt = mife->enc_vt;
    sk->pp_vt = mife->pp_vt;
    sk->sp_vt = mife->sp_vt;
    sk->sp = mife->sp;
    sk->pp = mife->pp;
    sk->const_betas = mife->const_betas;
    sk->local = false;
    return sk;
}

void
mife_sk_free(mife_sk_t *sk)
{
    if (sk == NULL)
        return;
    if (sk->local) {
        if (sk->pp)
            public_params_free(sk->pp_vt, sk->pp);
        if (sk->sp)
            secret_params_free(sk->sp_vt, sk->sp);
        if (sk->const_betas)
            mpz_vect_free(sk->const_betas, sk->cp->c);
    }
    free(sk);
}

int
mife_sk_fwrite(const mife_sk_t *sk, FILE *fp)
{
    public_params_fwrite(sk->pp_vt, sk->pp, fp);
    secret_params_fwrite(sk->sp_vt, sk->sp, fp);
    if (sk->cp->c)
        for (size_t o = 0; o < sk->cp->c; ++o)
            gmp_fprintf(fp, "%Zd\n", sk->const_betas[o]);
    return OK;
}

mife_sk_t *
mife_sk_fread(const mmap_vtable *mmap, const obf_params_t *op, FILE *fp)
{
    const circ_params_t *cp = &op->cp;
    double start, end;
    mife_sk_t *sk;

    sk = my_calloc(1, sizeof sk[0]);
    sk->mmap = mmap;
    sk->cp = cp;
    sk->enc_vt = get_encoding_vtable(mmap);
    sk->pp_vt = get_pp_vtable(mmap);
    sk->sp_vt = get_sp_vtable(mmap);
    start = current_time();
    sk->pp = public_params_fread(sk->pp_vt, op, fp);
    end = current_time();
    if (g_verbose)
        fprintf(stderr, "  Reading pp from disk: %.2fs\n", end - start);
    start = current_time();
    sk->sp = secret_params_fread(sk->sp_vt, cp, fp);
    end = current_time();
    if (g_verbose)
        fprintf(stderr, "  Reading sp from disk: %.2fs\n", end - start);
    if (sk->cp->c) {
        sk->const_betas = my_calloc(sk->cp->c, sizeof sk->const_betas[0]);
        for (size_t o = 0; o < sk->cp->c; ++o)
            gmp_fscanf(fp, "%Zd\n", &sk->const_betas[o]);
    }
    sk->local = true;
    return sk;
}

mife_ek_t *
mife_ek(const mife_t *mife)
{
    mife_ek_t *ek;
    ek = my_calloc(1, sizeof ek[0]);
    ek->mmap = mife->mmap;
    ek->cp = mife->cp;
    ek->enc_vt = mife->enc_vt;
    ek->pp_vt = mife->pp_vt;
    ek->pp = mife->pp;
    ek->Chatstar = mife->Chatstar;
    ek->zhat = mife->zhat;
    ek->constants = mife->constants;
    ek->local = false;
    return ek;
}

void
mife_ek_free(mife_ek_t *ek)
{
    if (ek == NULL)
        return;
    if (ek->local) {
        if (ek->pp)
            public_params_free(ek->pp_vt, ek->pp);
        if (ek->Chatstar)
            encoding_free(ek->enc_vt, ek->Chatstar);
        if (ek->zhat) {
            for (size_t o = 0; o < ek->cp->m; ++o)
                encoding_free(ek->enc_vt, ek->zhat[o]);
            free(ek->zhat);
        }
    }
    free(ek);
}

int
mife_ek_fwrite(const mife_ek_t *ek, FILE *fp)
{
    public_params_fwrite(ek->pp_vt, ek->pp, fp);
    if (ek->constants) {
        fprintf(fp, "1\n");
        mife_ciphertext_fwrite(ek->constants, ek->cp, fp);
    } else {
        fprintf(fp, "0\n");
        encoding_fwrite(ek->enc_vt, ek->Chatstar, fp);
    }
    for (size_t o = 0; o < ek->cp->m; ++o)
        encoding_fwrite(ek->enc_vt, ek->zhat[o], fp);
    return OK;
}

mife_ek_t *
mife_ek_fread(const mmap_vtable *mmap, const obf_params_t *op, FILE *fp)
{
    const circ_params_t *cp = &op->cp;
    mife_ek_t *ek;
    int has_consts;

    ek = my_calloc(1, sizeof ek[0]);
    ek->local = true;
    ek->mmap = mmap;
    ek->cp = cp;
    ek->enc_vt = get_encoding_vtable(mmap);
    ek->pp_vt = get_pp_vtable(mmap);
    ek->pp = public_params_fread(ek->pp_vt, op, fp);
    fscanf(fp, "%d\n", &has_consts);
    if (has_consts) {
        if ((ek->constants = mife_ciphertext_fread(ek->mmap, ek->cp, fp)) == NULL)
            goto error;
    } else {
        if ((ek->Chatstar = encoding_fread(ek->enc_vt, fp)) == NULL)
            goto error;
    }
    ek->zhat = my_calloc(cp->m, sizeof ek->zhat[0]);
    for (size_t o = 0; o < cp->m; ++o)
        ek->zhat[o] = encoding_fread(ek->enc_vt, fp);
    return ek;
error:
    mife_ek_free(ek);
    return NULL;
}

void
mife_ciphertext_free(mife_ciphertext_t *ct, const circ_params_t *cp)
{
    if (ct == NULL)
        return;

    const size_t ninputs = cp->ds[ct->slot];

    for (size_t j = 0; j < ninputs; ++j) {
        encoding_free(ct->enc_vt, ct->xhat[j]);
    }
    for (size_t p = 0; p < ct->npowers; ++p) {
        encoding_free(ct->enc_vt, ct->uhat[p]);
    }
    free(ct->xhat);
    free(ct->uhat);
    for (size_t o = 0; o < cp->m; ++o) {
        encoding_free(ct->enc_vt, ct->what[o]);
    }
    free(ct->what);
    free(ct);
}

int
mife_ciphertext_fwrite(const mife_ciphertext_t *ct, const circ_params_t *cp,
                       FILE *fp)
{
    const size_t ninputs = cp->ds[ct->slot];
    fprintf(fp, "%lu\n", ct->slot);
    fprintf(fp, "%lu\n", ct->npowers);
    for (size_t j = 0; j < ninputs; ++j) {
        encoding_fwrite(ct->enc_vt, ct->xhat[j], fp);
    }
    for (size_t p = 0; p < ct->npowers; ++p) {
        encoding_fwrite(ct->enc_vt, ct->uhat[p], fp);
    }
    for (size_t o = 0; o < cp->m; ++o) {
        encoding_fwrite(ct->enc_vt, ct->what[o], fp);
    }

    return OK;
}

mife_ciphertext_t *
mife_ciphertext_fread(const mmap_vtable *mmap, const circ_params_t *cp, FILE *fp)
{
    mife_ciphertext_t *ct;
    size_t ninputs;

    ct = my_calloc(1, sizeof ct[0]);
    ct->enc_vt = get_encoding_vtable(mmap);
    if (fscanf(fp, "%lu\n", &ct->slot) != 1) {
        fprintf(stderr, "error (%s): cannot read slot number\n", __func__);
        goto error;
    }
    if (ct->slot >= cp->n) {
        fprintf(stderr, "error (%s): slot number > number of slots\n", __func__);
        goto error;
    }
    if (fscanf(fp, "%lu\n", &ct->npowers) != 1) {
        fprintf(stderr, "error (%s): cannot read number of powers\n", __func__);
        goto error;
    }
    ninputs = cp->ds[ct->slot];
    ct->xhat = my_calloc(ninputs, sizeof ct->xhat[0]);
    for (size_t j = 0; j < ninputs; ++j) {
        ct->xhat[j] = encoding_fread(ct->enc_vt, fp);
    }
    ct->uhat = my_calloc(ct->npowers, sizeof ct->uhat[0]);
    for (size_t p = 0; p < ct->npowers; ++p) {
        ct->uhat[p] = encoding_fread(ct->enc_vt, fp);
    }
    ct->what = my_calloc(cp->m, sizeof ct->what[0]);
    for (size_t o = 0; o < cp->m; ++o) {
        ct->what[o] = encoding_fread(ct->enc_vt, fp);
    }

    return ct;
error:
    free(ct);
    return NULL;
}

static mife_ciphertext_t *
_mife_encrypt(const mife_sk_t *sk, const size_t slot, const int *inputs,
              const size_t npowers, const size_t nthreads, aes_randstate_t rng,
              pool_info_t *_pi, mpz_t *_betas)
{
    mife_ciphertext_t *ct;
    double start, end, _start, _end;
    const circ_params_t *cp = sk->cp;

    if (g_verbose && !_pi)
        fprintf(stderr, "  Encrypting...\n");

    start = current_time();
    _start = current_time();

    const size_t ninputs = cp->ds[slot];
    const size_t nconsts = cp->circ->consts.n;
    const size_t has_consts = nconsts ? 1 : 0;
    const size_t noutputs = cp->m;
    mpz_t *const moduli =
        mpz_vect_create_of_fmpz(sk->mmap->sk->plaintext_fields(sk->sp->sk),
                                sk->mmap->sk->nslots(sk->sp->sk));

    ct = my_calloc(1, sizeof ct[0]);
    ct->enc_vt = sk->enc_vt;
    ct->slot = slot;
    ct->npowers = npowers;
    ct->xhat = my_calloc(ninputs, sizeof ct->xhat[0]);
    for (size_t j = 0; j < ninputs; ++j) {
        ct->xhat[j] = encoding_new(sk->enc_vt, sk->pp_vt, sk->pp);
    }
    ct->uhat = my_calloc(npowers, sizeof ct->uhat[0]);
    for (size_t p = 0; p < npowers; ++p) {
        ct->uhat[p] = encoding_new(sk->enc_vt, sk->pp_vt, sk->pp);
    }
    ct->what = my_calloc(noutputs, sizeof ct->what[0]);
    for (size_t o = 0; o < noutputs; ++o) {
        ct->what[o] = encoding_new(sk->enc_vt, sk->pp_vt, sk->pp);
    }

    index_set *const ix = index_set_new(mife_params_nzs(cp));

    mpz_t slots[1 + cp->n];
    mpz_vect_init(slots, 1 + cp->n);
    mpz_t *betas;
    if (_betas) {
        betas = _betas;
    } else {
        betas = my_calloc(ninputs, sizeof betas[0]);
        mpz_vect_init(betas, ninputs);
    }

    mpz_t cs[noutputs];
    mpz_vect_init(cs, noutputs);
    mpz_t const_cs[noutputs];
    mpz_vect_init(const_cs, noutputs);

    for (size_t j = 0; j < ninputs; ++j) {
        mpz_randomm_inv(betas[j], rng, moduli[1 + slot]);
    }

    {
        mpz_t circ_inputs[circ_params_ninputs(cp)];
        mpz_vect_init(circ_inputs, circ_params_ninputs(cp));
        mpz_t consts[nconsts];
        mpz_vect_init(consts, nconsts);

        populate_circ_input(cp, slot, circ_inputs, consts, betas);
        eval_circ(cp, slot, cs, circ_inputs, consts, moduli);

        if (has_consts) {
            populate_circ_input(cp, cp->n - 1, circ_inputs, consts, sk->const_betas);
            eval_circ(cp, cp->n - 1, const_cs, circ_inputs, consts, moduli);
        }

        mpz_vect_clear(circ_inputs, circ_params_ninputs(cp));
        mpz_vect_clear(consts, nconsts);
    }

    _end = current_time();
    if (g_verbose && !_pi)
        fprintf(stderr, "    Initialize: %.2fs\n", _end - _start);

    threadpool *pool;
    pthread_mutex_t *lock;
    size_t *count, total;
    
    if (_pi) {
        pool = _pi->pool;
        lock = _pi->lock;
        count = _pi->count;
        total = _pi->total;
    } else {
        pool = threadpool_create(nthreads);
        lock = my_calloc(1, sizeof lock[0]);
        pthread_mutex_init(lock, NULL);
        count = my_calloc(1, sizeof count[0]);
        total = mife_num_encodings_encrypt(cp, slot, npowers);
    }

    _start = current_time();

    if (g_verbose && !_pi)
        print_progress(*count, total);
    
    /* Encode \hat xⱼ */
    index_set_clear(ix);
    IX_X(ix, cp, slot) = 1;
    for (size_t i = 0; i < cp->n; ++i) {
        mpz_set_ui(slots[1 + i], 1);
    }
    for (size_t j = 0; j < ninputs; ++j) {
        mpz_set_ui(slots[0], inputs[j]);
        mpz_set(slots[1 + slot], betas[j]);
        /* Encode \hat xⱼ := [xⱼ, 1, ..., 1, βⱼ, 1, ..., 1] */
        __encode(pool, sk->enc_vt, ct->xhat[j], slots, 1 + cp->n,
                 index_set_copy(ix), sk->sp, lock, count, total);
    }
    /* Encode \hat u_p */
    index_set_clear(ix);
    mpz_set_ui(slots[0], 1);
    mpz_set_ui(slots[1 + slot], 1);
    for (size_t p = 0; p < npowers; ++p) {
        IX_X(ix, cp, slot) = 1 << p;
        /* Encode \hat u_p = [1, ..., 1] */
        __encode(pool, sk->enc_vt, ct->uhat[p], slots, 1 + cp->n,
                 index_set_copy(ix), sk->sp, lock, count, total);
    }
    /* Encode \hat wₒ */
    if (!_betas) {
        index_set_clear(ix);
        IX_W(ix, cp, slot) = 1;
        if (slot == 0 && has_consts) {
            size_t **deg;
            size_t *deg_max;

            deg = my_calloc(cp->n, sizeof deg[0]);
            for (size_t i = 0; i < cp->n; ++i)
                deg[i] = my_calloc(noutputs, sizeof deg[i][0]);
            deg_max = my_calloc(cp->n, sizeof deg_max[0]);
            populate_circ_degrees(cp, deg, deg_max);
            for (size_t i = 0; i < cp->n; ++i)
                IX_X(ix, cp, i) = deg_max[i];
            IX_W(ix, cp, cp->n - 1) = 1;
            IX_Z(ix) = 1;
            for (size_t i = 0; i < cp->n; ++i)
                free(deg[i]);
            free(deg);
            free(deg_max);
        }
        mpz_set_ui(slots[0], 0);
        for (size_t o = 0; o < noutputs; ++o) {
            mpz_set(slots[1 + slot], cs[o]);
            if (slot == 0 && has_consts) {
                mpz_set(slots[cp->n], const_cs[o]);
            }
            /* Encode \hat wₒ = [0, 1, ..., 1, C†ₒ, 1, ..., 1] */
            __encode(pool, sk->enc_vt, ct->what[o], slots, 1 + cp->n,
                     index_set_copy(ix), sk->sp, lock, count, total);
        }
    }

    if (!_pi) {
        threadpool_destroy(pool);
        pthread_mutex_destroy(lock);
        free(lock);
        free(count);
    }

    _end = current_time();
    if (g_verbose && !_pi)
        fprintf(stderr, "    Encode: %.2fs\n", _end - _start);

    index_set_free(ix);
    mpz_vect_clear(slots, 1 + cp->n);
    if (!_betas)
        mpz_vect_free(betas, ninputs);
    mpz_vect_clear(cs, noutputs);
    mpz_vect_clear(const_cs, noutputs);
    mpz_vect_free(moduli, sk->mmap->sk->nslots(sk->sp->sk));

    end = current_time();
    if (g_verbose && !_pi)
        fprintf(stderr, "    Total: %.2fs\n", end - start);
    
    return ct;
}

mife_ciphertext_t *
mife_encrypt(const mife_sk_t *sk, const size_t slot, const int *inputs,
             const size_t npowers, const size_t nthreads, aes_randstate_t rng)
{
    if (sk == NULL || slot >= sk->cp->n || inputs == NULL || npowers == 0) {
        fprintf(stderr, "error: mife encrypt: invalid input\n");
        return NULL;
    }
    return _mife_encrypt(sk, slot, inputs, npowers, nthreads, rng, NULL, NULL);
}

static void
_raise_encoding(const mife_ek_t *ek, encoding *x, encoding **us, size_t npowers,
                size_t diff)
{
    assert(npowers > 0);
    while (diff > 0) {
        size_t p = 0;
        while (((size_t) 1 << (p+1)) <= diff && (p+1) < npowers)
            p++;
        encoding_mul(ek->enc_vt, ek->pp_vt, x, x, us[p], ek->pp);
        diff -= (1 << p);
    }
}

static int
raise_encoding(const mife_ek_t *ek, mife_ciphertext_t **cts,
               encoding *x, const index_set *target)
{
    const circ_params_t *const cp = ek->cp;
    const size_t has_consts = cp->c ? 1 : 0;
    index_set *ix;
    size_t diff;

    ix = index_set_difference(target, ek->enc_vt->mmap_set(x));
    if (ix == NULL)
        return ERR;
    for (size_t i = 0; i < cp->n - has_consts; i++) {
        diff = IX_X(ix, cp, i);
        if (diff > 0)
            _raise_encoding(ek, x, cts[i]->uhat, cts[i]->npowers, diff);
    }
    if (has_consts) {
        diff = IX_X(ix, cp, cp->n - 1);
        if (diff > 0)
            _raise_encoding(ek, x, ek->constants->uhat, ek->constants->npowers, diff);
    }
    index_set_free(ix);
    return OK;
}

static int
raise_encodings(const mife_ek_t *ek, mife_ciphertext_t **cts,
                encoding *x, encoding *y)
{
    index_set *ix;
    int ret = ERR;
    ix = index_set_union(ek->enc_vt->mmap_set(x), ek->enc_vt->mmap_set(y));
    if (ix == NULL)
        goto cleanup;
    if (raise_encoding(ek, cts, x, ix) == ERR)
        goto cleanup;
    if (raise_encoding(ek, cts, y, ix) == ERR)
        goto cleanup;
    ret = OK;
cleanup:
    if (ix)
        index_set_free(ix);
    return ret;
}

static void
decrypt_worker(void *vargs)
{
    decrypt_args_t *const dargs = vargs;
    const acircref ref = dargs->ref;
    const acirc *const c = dargs->c;
    mife_ciphertext_t **cts = dargs->cts;
    const mife_ek_t *const ek = dargs->ek;
    bool *const mine = dargs->mine;
    int *const ready = dargs->ready;
    encoding **cache = dargs->cache;
    ref_list *const *const deps = dargs->deps;
    threadpool *const pool = dargs->pool;
    int *const rop = dargs->rop;
    size_t *const kappas = dargs->kappas;

    const circ_params_t *const cp = ek->cp;
    const acirc_operation op = c->gates.gates[ref].op;
    const acircref *const args = c->gates.gates[ref].args;
    encoding *res = NULL;

    switch (op) {
    case OP_CONST: {
        const size_t bit = circ_params_bit(cp, cp->circ->ninputs + args[0]);
        /* XXX: check that bit is valid! */
        res = ek->constants->xhat[bit];
        mine[ref] = false;
        break;
    }
    case OP_INPUT: {
        const size_t slot = circ_params_slot(cp, args[0]);
        const size_t bit = circ_params_bit(cp, args[0]);
        /* XXX: check that slot and bit are valid! */
        res = cts[slot]->xhat[bit];
        mine[ref] = false;
        break;
    }
    case OP_ADD: case OP_SUB: case OP_MUL: {
        assert(c->gates.gates[ref].nargs == 2);
        res = encoding_new(ek->enc_vt, ek->pp_vt, ek->pp);
        mine[ref] = true;

        const encoding *const x = cache[args[0]];
        const encoding *const y = cache[args[1]];

        if (op == OP_MUL) {
            encoding_mul(ek->enc_vt, ek->pp_vt, res, x, y, ek->pp);
        } else {
            encoding *tmp_x, *tmp_y;
            tmp_x = encoding_new(ek->enc_vt, ek->pp_vt, ek->pp);
            tmp_y = encoding_new(ek->enc_vt, ek->pp_vt, ek->pp);
            encoding_set(ek->enc_vt, tmp_x, x);
            encoding_set(ek->enc_vt, tmp_y, y);
            if (!index_set_eq(ek->enc_vt->mmap_set(tmp_x), ek->enc_vt->mmap_set(tmp_y)))
                raise_encodings(ek, cts, tmp_x, tmp_y);
            if (op == OP_ADD) {
                encoding_add(ek->enc_vt, ek->pp_vt, res, tmp_x, tmp_y, ek->pp);
            } else if (op == OP_SUB) {
                encoding_sub(ek->enc_vt, ek->pp_vt, res, tmp_x, tmp_y, ek->pp);
            } else {
                abort();
            }
            encoding_free(ek->enc_vt, tmp_x);
            encoding_free(ek->enc_vt, tmp_y);
        }
        break;
    }
    default:
        fprintf(stderr, "fatal: op not supported\n");
        abort();
    }

    cache[ref] = res;

    {
        ref_list_node *cur = deps[ref]->first;
        while (cur != NULL) {
            const int num = __sync_add_and_fetch(&ready[cur->ref], 1);
            if (num == 2) {
                decrypt_args_t *newargs = my_calloc(1, sizeof newargs[0]);
                memcpy(newargs, dargs, sizeof newargs[0]);
                newargs->ref = cur->ref;
                threadpool_add_job(pool, decrypt_worker, newargs);
            } else {
                cur = cur->next;
            }
        }
    }

    free(dargs);

    ssize_t output = -1;
    for (size_t i = 0; i < cp->m; i++) {
        if (ref == c->outputs.buf[i]) {
            output = i;
            break;
        }
    }

    if (output != -1) {
        encoding *out, *lhs, *rhs;
        const index_set *const toplevel = ek->pp_vt->toplevel(ek->pp);
        int result;

        out = encoding_new(ek->enc_vt, ek->pp_vt, ek->pp);
        lhs = encoding_new(ek->enc_vt, ek->pp_vt, ek->pp);
        rhs = encoding_new(ek->enc_vt, ek->pp_vt, ek->pp);

        /* Compute LHS */
        encoding_mul(ek->enc_vt, ek->pp_vt, lhs, res, ek->zhat[output], ek->pp);
        raise_encoding(ek, cts, lhs, toplevel);
        if (!index_set_eq(ek->enc_vt->mmap_set(lhs), toplevel)) {
            fprintf(stderr, "error: lhs != toplevel\n");
            index_set_print(ek->enc_vt->mmap_set(lhs));
            index_set_print(toplevel);
            if (rop)
                rop[output] = 1;
            goto cleanup;
        }
        /* Compute RHS */
        if (ek->Chatstar) {
            encoding_set(ek->enc_vt, rhs, ek->Chatstar);
            for (size_t i = 0; i < cp->n; ++i)
                encoding_mul(ek->enc_vt, ek->pp_vt, rhs, rhs, cts[i]->what[output], ek->pp);
        } else {
            encoding_set(ek->enc_vt, rhs, cts[0]->what[output]);
            for (size_t i = 1; i < cp->n - 1; ++i)
                encoding_mul(ek->enc_vt, ek->pp_vt, rhs, rhs, cts[i]->what[output], ek->pp);
        }
        if (!index_set_eq(ek->enc_vt->mmap_set(rhs), toplevel)) {
            fprintf(stderr, "error: rhs != toplevel\n");
            index_set_print(ek->enc_vt->mmap_set(rhs));
            index_set_print(toplevel);
            if (rop)
                rop[output] = 1;
            goto cleanup;
        }
        encoding_sub(ek->enc_vt, ek->pp_vt, out, lhs, rhs, ek->pp);
        result = !encoding_is_zero(ek->enc_vt, ek->pp_vt, out, ek->pp);
        if (rop)
            rop[output] = result;
        if (kappas)
            kappas[output] = encoding_get_degree(ek->enc_vt, out);

    cleanup:
        encoding_free(ek->enc_vt, out);
        encoding_free(ek->enc_vt, lhs);
        encoding_free(ek->enc_vt, rhs);
    }
}

int
mife_decrypt(const mife_ek_t *ek, int *rop, mife_ciphertext_t **cts,
             size_t nthreads, size_t *kappa)
{
    const circ_params_t *cp = ek->cp;
    const acirc *const circ = cp->circ;
    int ret = ERR;

    if (ek == NULL || cts == NULL)
        return ERR;

    encoding **cache = my_calloc(acirc_nrefs(circ), sizeof cache[0]);
    bool *mine = my_calloc(acirc_nrefs(circ), sizeof mine[0]);
    int *ready = my_calloc(acirc_nrefs(circ), sizeof ready[0]);
    size_t *kappas = NULL;
    ref_list **deps = ref_lists_new(circ);
    threadpool *pool = threadpool_create(nthreads);

    if (kappa)
        kappas = my_calloc(cp->m, sizeof kappas[0]);

    for (size_t ref = 0; ref < acirc_nrefs(circ); ++ref) {
        acirc_operation op = circ->gates.gates[ref].op;
        if (op != OP_INPUT && op != OP_CONST)
            continue;
        decrypt_args_t *args = calloc(1, sizeof args[0]);
        args->mmap   = ek->mmap;
        args->ref    = ref;
        args->c      = circ;
        args->cts    = cts;
        args->ek     = ek;
        args->mine   = mine;
        args->ready  = ready;
        args->cache  = cache;
        args->deps   = deps;
        args->pool   = pool;
        args->rop    = rop;
        args->kappas = kappas;
        threadpool_add_job(pool, decrypt_worker, args);
    }
    ret = OK;

    threadpool_destroy(pool);

    if (kappa) {
        size_t maxkappa = 0;
        for (size_t i = 0; i < cp->m; i++) {
            if (kappas[i] > maxkappa)
                maxkappa = kappas[i];
        }
        free(kappas);
        *kappa = maxkappa;
    }

    for (size_t i = 0; i < acirc_nrefs(circ); i++) {
        if (mine[i]) {
            encoding_free(ek->enc_vt, cache[i]);
        }
    }
    ref_lists_free(deps, circ);
    free(cache);
    free(mine);
    free(ready);

    return ret;
}
