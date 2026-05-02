#include "vm.h"

#include <stdio.h>

int main(int argc, char **argv) {
    struct bc_module module;
    struct bc_vm vm;
    char error_buffer[256];
    FILE *input;

    if(argc != 2) {
        fprintf(stderr, "usage: %s program.lbc\n", argv[0]);
        return 1;
    }

    input = fopen(argv[1], "rb");
    if(input == NULL) {
        fprintf(stderr, "ledavm: unable to open %s\n", argv[1]);
        return 1;
    }

    if(!bc_module_read(input, &module)) {
        fprintf(stderr, "ledavm: unable to read bytecode module\n");
        fclose(input);
        return 1;
    }
    fclose(input);

    if(!bc_vm_init(&vm, &module)) {
        fprintf(stderr, "ledavm: invalid module entry point\n");
        bc_module_free(&module);
        return 1;
    }

    if(!bc_vm_run(&vm, error_buffer, sizeof(error_buffer))) {
        fprintf(stderr, "ledavm: %s\n", error_buffer);
        bc_module_free(&module);
        return 1;
    }

    bc_module_free(&module);
    return 0;
}