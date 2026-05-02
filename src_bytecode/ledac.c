#include "bc_emit.h"
#include "bytecode_frontend_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *argv0) { fprintf(stderr, "usage: %s input.led -o output.lbc\n", argv0); }

int main(int argc, char **argv) {
    const char *input_path = NULL;
    const char *output_path = NULL;
    struct symbolTableRecord *symbols = NULL;
    struct statementRecord *first_statement = NULL;
    struct bc_module module;
    char message[256];
    FILE *output;
    int i;

    for(i = 1; i < argc; ++i) {
        if(strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if(input_path == NULL) {
            input_path = argv[i];
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if(input_path == NULL || output_path == NULL) {
        usage(argv[0]);
        return 1;
    }

    if(!bytecode_parse_file((char *)input_path, &symbols, &first_statement)) {
        fprintf(stderr, "ledac: unable to parse %s\n", input_path);
        return 1;
    }

    if(!bc_compile_top_level(symbols, first_statement, &module, message, sizeof(message))) {
        fprintf(stderr, "ledac: %s\n", message);
        return 1;
    }

    output = fopen(output_path, "wb");
    if(output == NULL) {
        fprintf(stderr, "ledac: unable to open %s for writing\n", output_path);
        bc_module_free(&module);
        return 1;
    }

    if(!bc_module_write(output, &module)) {
        fprintf(stderr, "ledac: unable to write bytecode module to %s\n", output_path);
        fclose(output);
        bc_module_free(&module);
        return 1;
    }

    fclose(output);
    fprintf(stderr, "ledac: wrote bytecode for %s to %s\n", input_path, output_path);
    fprintf(stderr, "ledac: note: %s\n", message);
    bc_module_free(&module);
    return 0;
}