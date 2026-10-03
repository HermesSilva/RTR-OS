/*
 * RTR-OS - FAT32 volume on the SD card: files in the root directory only.
 *
 * Supports what the manifest needs: find a file by its 8.3 name, read it,
 * and write it back, growing or shrinking it. No long names, no
 * subdirectories, no file creation.
 */
#ifndef SYSTEM_FAT32_H
#define SYSTEM_FAT32_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Mounts the volume: the first FAT32 partition of the card, or a card formatted without a partition table. */
bool fat32_mount(void);

/* Reads the file `name` ("MANIFEST.TXT") into `buffer`; `*size` gets its size. */
bool fat32_read(const char *name, uint8_t *buffer, size_t capacity, size_t *size)
    __attribute__((warn_unused_result));

/* Reads at most `capacity` bytes from the start of the file; `*size` gets how many were read. */
bool fat32_read_head(const char *name, uint8_t *buffer, size_t capacity, size_t *size)
    __attribute__((warn_unused_result));

struct fat32_entry {
    char name[13];                      /* "NAME.EXT", NUL-terminated */
    uint32_t size;
};

/* Lists the root directory files with extension `extension` ("BIN"). Returns how many were stored. */
uint32_t fat32_list(const char *extension, struct fat32_entry *out, uint32_t capacity);

/* Replaces the contents of an existing file. */
bool fat32_write(const char *name, const uint8_t *data, size_t size) __attribute__((warn_unused_result));

/* Creates an empty file in the root directory if it does not exist. */
bool fat32_create(const char *name) __attribute__((warn_unused_result));

#endif
