#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../include/hui/hui_ir.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s file.huir\n", argv[0]);
        return 1;
    }
    const char *path = argv[1];
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror("open");
        return 1;
    }
    HUIR_header header;
    if (fread(&header, 1, sizeof(header), f) != sizeof(header)) {
        fprintf(stderr, "read error\n");
        fclose(f);
        return 1;
    }
    if (header.magic != HUIR_MAGIC) {
        fprintf(stderr, "bad magic\n");
        fclose(f);
        return 1;
    }
    printf("HUIR %u.%u\n", header.major, header.minor);
    fseek(f, (long) header.toc_offset, SEEK_SET);
    HUIR_toc_entry entry;
    if (fread(&entry, 1, sizeof(entry), f) != sizeof(entry)) {
        fprintf(stderr, "toc read error\n");
        fclose(f);
        return 1;
    }
    printf("chunk '%c%c%c%c' len=%llu offset=%llu\n",
           entry.fourcc & 0xFF,
           (entry.fourcc >> 8) & 0xFF,
           (entry.fourcc >> 16) & 0xFF,
           (entry.fourcc >> 24) & 0xFF,
           (unsigned long long) entry.length,
           (unsigned long long) entry.offset);
    fclose(f);
    return 0;
}
