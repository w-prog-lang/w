#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printf("Usage: wlang <source>\n");
    return 1;
  }

  FILE *f = fopen(argv[1], "r");
  if (!f) {
    perror("Failed to open source file");
    return 1;
  }

  fseek(f, 0, SEEK_END);
  long src_size = ftell(f);
  rewind(f);

  char *src = malloc(src_size + 1);
  fread(src, 1, src_size, f);
  src[src_size] = '\0';

  FILE *out = fopen("output.c", "w");
  fprintf(out, "");
  fclose(out);

  system("gcc -o output output.c");
  system("rm output.c");

  free(src);
  return 0;
}
