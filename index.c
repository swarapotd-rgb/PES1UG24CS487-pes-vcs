// index.c — Staging area implementation

#include "index.h"
#include "tree.h" // Added this to fix implicit declaration of object_write
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

// Helper to get file mode for the index
static int get_file_mode(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0100644;
    if (S_ISDIR(st.st_mode)) return 0040000;
    if (st.st_mode & S_IXUSR) return 0100755;
    return 0100644;
}

// ─── PROVIDED ─────────────────────────────────────────────────────

IndexEntry* index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0)
            return &index->entries[i];
    }
    return NULL;
}

int index_remove(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            int remaining = index->count - i - 1;
            if (remaining > 0)
                memmove(&index->entries[i],
                        &index->entries[i + 1],
                        remaining * sizeof(IndexEntry));
            index->count--;
            return index_save(index);
        }
    }
    return -1;
}

int index_status(const Index *index) {
    printf("Staged changes:\n");
    if (index->count == 0)
        printf("  (nothing to show)\n");

    for (int i = 0; i < index->count; i++) {
        printf("  staged:     %s\n", index->entries[i].path);
    }
    printf("\n");
    return 0;
}

// ─── IMPLEMENTATION ───────────────────────────────────────────────

static int cmp_entries(const void *a, const void *b) {
    return strcmp(((IndexEntry*)a)->path, ((IndexEntry*)b)->path);
}

int index_load(Index *index) {
    index->count = 0;

    FILE *f = fopen(".pes/index", "r");
    if (!f) return 0;

    while (index->count < MAX_INDEX_ENTRIES) {
        IndexEntry *e = &index->entries[index->count];
        char hex[HASH_HEX_SIZE + 1];

        if (fscanf(f, "%o %64s %ld %u %255s\n",
                    &e->mode,
                    hex,
                    &e->mtime_sec,
                    &e->size,
                    e->path) != 5)
            break;

        if (hex_to_hash(hex, &e->hash) != 0) {
            fclose(f);
            return -1;
        }
        index->count++;
    }

    fclose(f);
    return 0;
}

int index_save(const Index *index) {
    FILE *f = fopen(".pes/index.tmp", "w");
    if (!f) return -1;

    // FIX: Avoid copying the whole Index struct to the stack to prevent SegFault
    // Instead, we cast to non-const to sort in place or just write if already sorted.
    // For the lab, sorting the actual index is usually acceptable:
    Index *non_const_index = (Index *)index;
    qsort(non_const_index->entries, non_const_index->count, sizeof(IndexEntry), cmp_entries);

    for (int i = 0; i < index->count; i++) {
        char hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&index->entries[i].hash, hex);

        fprintf(f, "%o %s %ld %u %s\n",
                index->entries[i].mode,
                hex,
                (long)index->entries[i].mtime_sec,
                index->entries[i].size,
                index->entries[i].path);
    }

    fflush(f);
    fsync(fileno(f));
    fclose(f);

    if (rename(".pes/index.tmp", ".pes/index") != 0)
        return -1;

    return 0;
}

int index_add(Index *index, const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    size_t size = st.st_size;
    void *data = NULL;

    if (size > 0) {
        data = malloc(size);
        if (!data) {
            fclose(f);
            return -1;
        }
        if (fread(data, 1, size, f) != size) {
            fclose(f);
            free(data);
            return -1;
        }
    }
    fclose(f);

    ObjectID id;
    if (object_write(OBJ_BLOB, data, size, &id) != 0) {
        if (data) free(data);
        return -1;
    }
    if (data) free(data);

    IndexEntry *e = index_find(index, path);
    if (!e) {
        if (index->count >= MAX_INDEX_ENTRIES) return -1;
        e = &index->entries[index->count++];
    }

    e->mode = get_file_mode(path);
    e->hash = id;
    e->mtime_sec = st.st_mtime;
    e->size = (uint32_t)st.st_size;
    strncpy(e->path, path, sizeof(e->path) - 1);

    return index_save(index);
}
