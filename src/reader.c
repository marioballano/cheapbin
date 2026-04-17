#include "reader.h"
#include <stdio.h>
#include <stdlib.h>

int read_binary(const char *path, uint8_t **out_data, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: cannot open '%s': ", path);
        perror(NULL);
        return -1;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        perror("error: fseek");
        fclose(f);
        return -1;
    }

    long len = ftell(f);
    if (len < 0) {
        perror("error: ftell");
        fclose(f);
        return -1;
    }
    if (len == 0) {
        fprintf(stderr, "error: '%s' is empty\n", path);
        fclose(f);
        return -1;
    }

    rewind(f);

    uint8_t *buf = malloc((size_t)len);
    if (!buf) {
        fprintf(stderr, "error: out of memory (%ld bytes)\n", len);
        fclose(f);
        return -1;
    }

    size_t nread = fread(buf, 1, (size_t)len, f);
    fclose(f);

    if (nread != (size_t)len) {
        fprintf(stderr, "error: short read on '%s'\n", path);
        free(buf);
        return -1;
    }

    *out_data = buf;
    *out_size = (size_t)len;
    return 0;
}
