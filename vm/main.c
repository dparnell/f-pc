/* main.c -- fpc: the F-PC native VM.
 *
 *   fpc [-I dir]... [-e forth]... [--batch] [file...]
 *
 * Files are FLOADed in order, -e strings evaluated in order with them; then
 * the interactive prompt runs unless --batch is given.
 */
#include "vm.h"
#include "seed.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static vm_t *the_vm;
static void on_sigint(int sig) { (void)sig; if (the_vm) the_vm->interrupt = 1; }

static void usage(void)
{
    fprintf(stderr, "usage: fpc [-I dir]... [-e forth]... [--batch] [file...]\n");
    exit(2);
}

int main(int argc, char **argv)
{
    int batch = 0;
    host_t *host = host_batch_new();
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

    int status = 0;
    for (int i = 1; i < argc; i++) {
        int r = 0;
        if (!strcmp(argv[i], "--batch")) batch = 1;
        else if (!strcmp(argv[i], "-I") && i + 1 < argc) seed_add_path(vm, argv[++i]);
        else if (!strcmp(argv[i], "-e") && i + 1 < argc) r = seed_eval(vm, argv[++i]);
        else if (argv[i][0] == '-' && argv[i][1]) usage();
        else r = seed_load(vm, argv[i]);
        if (r == E_BYE) { host->flush(host); return vm->exit_code; }
        if (r) { status = 1; if (batch) break; }
    }
    if (!batch) seed_quit(vm);
    host->flush(host);
    return status;
}
