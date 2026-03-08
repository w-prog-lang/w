// #include <libtcc.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG true

size_t get_token_len(char* ptr) {
    size_t len = 0;
    for (; *ptr != ' '; ptr++) len++;
    return len;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: wlang <source>\n");
        return 1;
    }

    int filename_len = strlen(argv[1]);

    if (filename_len < 3) {
        fprintf(stderr, "Invalid filename\n");
        return 1;
    }

    char* dot = strrchr(argv[1], '.');
    if (dot == NULL || dot != &argv[1][filename_len - 2]) {
        fprintf(stderr, "Not a .w file\n");
        return 1;
    }

    char* ext = dot + 1;
    if (strcmp(ext, "w") != 0 && strcmp(ext, "W") != 0) {
        fprintf(stderr, "Not a .w file\n");
        return 1;
    }

    int name_len = dot - argv[1];
    char* exe_name = malloc(name_len + 1);
    strncpy(exe_name, argv[1], name_len);
    exe_name[name_len] = '\0';

    FILE* f = fopen(argv[1], "r");
    if (!f) {
        perror("Failed to open source file");
        free(exe_name);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long src_size = ftell(f);
    rewind(f);

    char* src = malloc(src_size + 1);
    fread(src, 1, src_size, f);
    src[src_size] = '\0';
    fclose(f);

    // char* gen = src;
    char* c = src;
    while (true) {
        if (!DEBUG) break;

        size_t token_len = get_token_len(c);
        char* token = malloc(token_len + 1);
        for (size_t i = 0; i < token_len; i++) {
            token[i] = c[i];
        }
        token[token_len] = '\0';

        if (strcmp(token, "vari") == 0) {
            printf("vari detected.");
        }

        c += token_len;
        if (*c == '\0') break;
        c++;
    }

    /**
    TCCState* tcc = tcc_new();
    if (!tcc) {
        fprintf(stderr, "Failed to initialize tcc\n");
        free(exe_name);
        free(src);
        return 1;
    }

    tcc_set_output_type(tcc, TCC_OUTPUT_EXE);

    if (tcc_compile_string(tcc, gen) == -1) {
        fprintf(stderr, "Failed to compile\n");
        tcc_delete(tcc);
        free(exe_name);
        free(src);
        return 1;
    }

    if (tcc_output_file(tcc, exe_name) == -1) {
        fprintf(stderr, "Failed to output file\n");
        tcc_delete(tcc);
        free(exe_name);
        free(src);
        return 1;
    }

    tcc_delete(tcc);
    **/
    free(exe_name);
    free(src);
    return 0;
}
