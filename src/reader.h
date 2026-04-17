#ifndef CHEAPBIN_READER_H
#define CHEAPBIN_READER_H

#include <stddef.h>
#include <stdint.h>

/* Read an entire file into a malloc'd buffer.
   Returns 0 on success, -1 on error (prints message to stderr). */
int read_binary(const char *path, uint8_t **out_data, size_t *out_size);

#endif
