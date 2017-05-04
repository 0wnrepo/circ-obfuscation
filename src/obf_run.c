#include "obf_run.h"
#include "util.h"

#include <string.h>
#include <mmap/mmap_dummy.h>

int
obf_run_obfuscate(const mmap_vtable *mmap, const obfuscator_vtable *vt,
                  const char *fname, obf_params_t *op, size_t secparam,
                  size_t *kappa, size_t nthreads, aes_randstate_t rng)
{
    obfuscation *obf;
    double start, end, _start, _end;

    start = current_time();
    _start = current_time();
    obf = vt->obfuscate(mmap, op, secparam, kappa, nthreads, rng);
    if (obf == NULL) {
        fprintf(stderr, "error: obfuscation failed\n");
        goto error;
    }
    _end = current_time();
    if (g_verbose)
        fprintf(stderr, "obfuscate:     %.2fs\n", _end - _start);

    if (fname) {
        FILE *fp;

        if ((fp = fopen(fname, "w")) == NULL) {
            fprintf(stderr, "error: unable to open '%s' for writing\n", fname);
            exit(EXIT_FAILURE);
        }

        _start = current_time();
        if (vt->fwrite(obf, fp) == ERR) {
            fprintf(stderr, "error: writing obfuscation to disk failed\n");
            fclose(fp);
            goto error;
        }
        fclose(fp);
        _end = current_time();
        if (g_verbose)
            fprintf(stderr, "write to disk: %.2fs\n", _end - _start);
    }

    end = current_time();
    if (g_verbose)
        fprintf(stderr, "total:         %.2fs\n", end - start);

    vt->free(obf);
    return OK;
error:
    vt->free(obf);
    return ERR;
}

int
obf_run_evaluate(const mmap_vtable *mmap, const obfuscator_vtable *vt, 
                 const char *fname, obf_params_t *op, const int *input,
                 int *output, size_t nthreads, size_t *kappa,
                 size_t *max_npowers)
{
    double start, end, _start, _end;
    obfuscation *obf;
    FILE *fp;
    int ret = ERR;

    if ((fp = fopen(fname, "r")) == NULL) {
        fprintf(stderr, "error: unable to open '%s' for reading\n", fname);
        return ERR;
    }

    start = current_time();
    _start = current_time();
    if ((obf = vt->fread(mmap, op, fp)) == NULL) {
        fprintf(stderr, "error: reading obfuscator failed\n");
        goto cleanup;
    }
    _end = current_time();
    if (g_verbose)
        fprintf(stderr, "read from disk: %.2fs\n", _end - _start);

    _start = current_time();
    if (vt->evaluate(obf, output, input, nthreads, kappa, max_npowers) == ERR)
        goto cleanup;
    _end = current_time();
    if (g_verbose)
        fprintf(stderr, "evaluate:       %.2fs\n", _end - _start);

    end = current_time();
    if (g_verbose)
        fprintf(stderr, "total:          %.2fs\n", end - start);
    ret = OK;
cleanup:
    fclose(fp);
    vt->free(obf);
    return ret;
}

size_t
obf_run_smart_kappa(const obfuscator_vtable *vt, const acirc *circ,
                    obf_params_t *op, size_t nthreads, aes_randstate_t rng)
{
    bool verbosity = g_verbose;
    const char *fname = "/tmp/smart-kappa.obf";
    size_t kappa = 0;

    if (g_verbose) {
        fprintf(stderr, "Choosing κ and #powers smartly...\n");
    }

    g_verbose = false;

    if (obf_run_obfuscate(&dummy_vtable, vt, fname, op, 8, &kappa, nthreads, rng) == ERR) {
        fprintf(stderr, "error: unable to obfuscate to determine smart κ settings\n");
        return 0;
    }

    int input[circ->ninputs];
    int output[circ->outputs.n];

    memset(input, '\0', sizeof input);
    memset(output, '\0', sizeof output);
    if (obf_run_evaluate(&dummy_vtable, vt, fname, op, input, output, nthreads,
                          &kappa, NULL) == ERR) {
        fprintf(stderr, "error: unable to evaluate to determine smart κ settings\n");
        return 0;
    }

    g_verbose = verbosity;
    if (g_verbose)
        fprintf(stderr, "* setting κ →  %lu\n", kappa);

    return kappa;
}