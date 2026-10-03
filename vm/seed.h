/* seed.h -- bootstrap interpreter interface */
#ifndef FPC_SEED_H
#define FPC_SEED_H
#include "vm.h"

void seed_init(vm_t *vm);
void seed_add_path(vm_t *vm, const char *dir);
void seed_include(vm_t *vm, const char *name);   /* throws on error      */
int  seed_load(vm_t *vm, const char *path);      /* 0 ok, else throw code */
int  seed_eval(vm_t *vm, const char *line);
int  seed_quit(vm_t *vm);                        /* REPL until EOF/BYE  */
#endif
