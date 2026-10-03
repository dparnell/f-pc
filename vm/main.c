/* main.c -- fpc: the F-PC native VM.
 *
 *   fpc [--batch] [-i image | -k kernel.seq] [-I dir]... [dos-line...]
 *       Load a saved image (-i, or $FPC_IMAGE), or else the F-PC kernel
 *       source with the seed, then run F-PC's own COLD start. The remaining
 *       arguments form the DOS command tail (DOS-LINE), as F-PC.EXE's
 *       command line did: "fpc - FLOAD X BYE".
 *
 *       --sdl opens an SDL window instead of using the terminal.
 *
 *   fpc --seed [-I dir] [-e forth]... [--batch] [file...]
 *       The bare seed interpreter (bootstrap testing).
 */
#include "vm.h"
#include "seed.h"

#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static vm_t *the_vm;
static void on_sigint(int sig) { (void)sig; if (the_vm) the_vm->interrupt = 1; }

static void usage(void)
{
    fprintf(stderr,
        "usage: fpc [--batch] [-i image | -k kernel.seq] [-I dir]... [dos-line...]\n"
        "       fpc --seed [-I dir]... [-e forth]... [--batch] [file...]\n");
    exit(2);
}

/* the installation: $FPC_HOME, else the directory above the executable */
static void fpc_home(char *out, size_t sz)
{
    const char *home = getenv("FPC_HOME");
    if (home) { snprintf(out, sz, "%s", home); return; }
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = 0;
        char *sl = strrchr(exe, '/');
        if (sl) *sl = 0;
        sl = strrchr(exe, '/');
        if (sl && sl != exe) *sl = 0;           /* up from vm/ (or bin/) */
        snprintf(out, sz, "%s", exe);
        return;
    }
    snprintf(out, sz, ".");
}

static void default_kernel(char *out, size_t sz)
{
    char home[PATH_MAX];
    fpc_home(home, sizeof home);
    snprintf(out, sz, "%s/SRC/KERNEL.SEQ", home);
}

static int run_seed(vm_t *vm, int argc, char **argv)
{
    int batch = 0, status = 0;
    for (int i = 1; i < argc; i++) {
        int r = 0;
        if (!strcmp(argv[i], "--seed")) continue;
        if (!strcmp(argv[i], "--batch")) batch = 1;
        else if (!strcmp(argv[i], "-I") && i + 1 < argc) seed_add_path(vm, argv[++i]);
        else if (!strcmp(argv[i], "-e") && i + 1 < argc) r = seed_eval(vm, argv[++i]);
        else if (argv[i][0] == '-' && argv[i][1]) usage();
        else r = seed_load(vm, argv[i]);
        if (r == E_BYE) { vm->host->flush(vm->host); return vm->exit_code; }
        if (r) { status = 1; if (batch) break; }
    }
    if (!batch) seed_quit(vm);
    vm->host->flush(vm->host);
    return status;
}

/* Run F-PC from COLD. Errors raised by C primitives re-enter Forth the way
 * the original's traps did: divide errors call DIV0FUNC, Control-C calls
 * WARM, anything else is reported through ?ERROR ( addr len true -- ). */
static int run_forth(vm_t *vm)
{
    ucell cold = seed_find(vm, "COLD"), qerror = seed_find(vm, "?ERROR");
    ucell div0 = seed_find(vm, "DIV0FUNC"), warm = seed_find(vm, "WARM");
    vm->resized_xt = seed_find(vm, "RESIZED");
    if (!cold || !qerror) { fprintf(stderr, "fpc: kernel has no COLD or ?ERROR\n"); return 2; }
    ucell next = cold;
    for (;;) {
        jmp_buf jb;
        vm->catch_jmp = &jb;
        if (setjmp(jb) == 0) {
            vm_execute(vm, next);
            next = cold;                    /* QUIT returned: should not happen */
            continue;
        }
        vm->catch_jmp = NULL;
        if (vm->throw_code == E_BYE || vm->bye) break;
        vm->rp = vm->rp0;
        vm->depth = 0;
        if (vm->sp > vm->sp0 || vm->sp < vm->sp0 - DSTACK_CELLS * CELL) vm->sp = vm->sp0;
        if (vm->throw_code == E_DIV0 && div0) { next = div0; continue; }
        if (vm->throw_code == E_INTERRUPT && warm) { next = warm; continue; }
        const char *msg = vm->throw_msg ? vm->throw_msg : "Error";
        ucell n = (ucell)strlen(msg), buf = vm->tib + TIB_SIZE - 160;
        if (n > 150) n = 150;
        memcpy(vm->mem + buf, msg, n);
        push(vm, buf); push(vm, n); push(vm, TRUE_F);
        next = qerror;
    }
    vm->host->flush(vm->host);
    return vm->exit_code;
}

int main(int argc, char **argv)
{
    int seed_only = 0, batch = !isatty(0) || !isatty(1), sdl = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed")) seed_only = batch = 1;
        if (!strcmp(argv[i], "--batch")) batch = 1;
        if (!strcmp(argv[i], "--sdl")) sdl = 1;
    }

    host_t *host = NULL;
#ifdef HAVE_SDL
    if (sdl && !seed_only) {
        host = host_sdl_new();
        if (!host) return 2;
        batch = 0;
    }
#else
    if (sdl) { fprintf(stderr, "fpc: built without SDL\n"); return 2; }
#endif
    if (!host) host = batch ? host_batch_new() : host_tty_new();
    vm_t *vm = vm_new(32u << 20, host);
    if (!vm) { perror("fpc"); return 2; }
    the_vm = vm;
    seed_init(vm);
    const char *env = getenv("FPC_PATH");
    if (env) {
        char *p = strdup(env), *s, *save = NULL;
        for (s = strtok_r(p, ":", &save); s; s = strtok_r(NULL, ":", &save)) seed_add_path(vm, s);
        free(p);
    }
    signal(SIGINT, on_sigint);
    if (seed_only) {
        screen_init(vm);
        return run_seed(vm, argc, argv);
    }

    char kernel[PATH_MAX], image[PATH_MAX] = "";
    default_kernel(kernel, sizeof kernel);
    if (getenv("FPC_IMAGE")) snprintf(image, sizeof image, "%s", getenv("FPC_IMAGE"));
    int i = 1;
    for (; i < argc; i++) {
        if (!strcmp(argv[i], "--batch") || !strcmp(argv[i], "--sdl")) continue;
        else if (!strcmp(argv[i], "-k") && i + 1 < argc) { snprintf(kernel, sizeof kernel, "%s", argv[++i]); image[0] = 0; }
        else if (!strcmp(argv[i], "-i") && i + 1 < argc) snprintf(image, sizeof image, "%s", argv[++i]);
        else if (!strcmp(argv[i], "-I") && i + 1 < argc) seed_add_path(vm, argv[++i]);
        else break;
    }
    /* the rest is the DOS command tail */
    char line[128] = "";
    size_t len = 0;
    for (; i < argc; i++) {
        size_t n = strlen(argv[i]);
        if (len + n + 1 >= sizeof line) { fprintf(stderr, "fpc: command line too long\n"); return 2; }
        if (len) line[len++] = ' ';
        memcpy(line + len, argv[i], n);
        len += n;
    }
    if (*image) {
        char err[256];
        if (image_load(vm, image, err, sizeof err) != 0) { fprintf(stderr, "fpc: %s\n", err); return 2; }
        heap_free_block(vm, sv(vm, SV_VIDEOBUF));   /* the saved screen */
        screen_init(vm);
    } else {
        screen_init(vm);
        if (seed_load(vm, kernel) != 0) {
            fprintf(stderr, "fpc: could not load the kernel from %s\n", kernel);
            return 2;
        }
    }
    vm->mem[vm->dosbuf + 128] = (uint8_t)len;       /* DOS-LINE */
    memcpy(vm->mem + vm->dosbuf + 129, line, len);
    char home[PATH_MAX];
    fpc_home(home, sizeof home);
    size_t hl = strlen(home);
    if (hl > 200) hl = 200;
    vm->mem[vm->dosbuf + 256] = (uint8_t)hl;        /* (FPC-HOME) */
    memcpy(vm->mem + vm->dosbuf + 257, home, hl);
#ifdef HAVE_SDL
    if (sdl) host_sdl_attach(host, vm); else
#endif
    if (!batch) host_tty_attach(host, vm);
    return run_forth(vm);
}
