/* RTR-OS - system process: what the web server needs from the rest of the process. */
#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "manifest.h"

/* Current IP address of the interface, as text ("0.0.0.0" while there is none). */
const char *system_ip_text(void);

/* Page served at "/", embedded in the program by www.S. */
extern const char www_index_start[];
extern const char www_index_end[];

/* Where the running configuration came from. */
enum config_source {
    CONFIG_FROM_CARD,                   /* MANIFEST.TXT read from the SD card */
    CONFIG_CREATED,                     /* the card had no manifest: the default one was written to it */
    CONFIG_DEFAULT,                     /* no usable card: built-in defaults, changes cannot be saved */
};

struct config_state {
    enum config_source source;
    bool card_present;                  /* a FAT32 volume was mounted */
    bool writable;                      /* the card can take a new manifest */
    bool valid;                         /* the manifest text parsed */
    char error[MANIFEST_ERROR_MAX];     /* parse error, when not valid */
    uint32_t error_line;
    struct manifest manifest;           /* parsed contents, when valid */
    const char *text;                   /* the manifest text as read */
    size_t text_size;
    bool reboot_pending;
};

const struct config_state *system_config(void);

/* A program file found on the card, with what its header says. */
struct program_info {
    char file[13];                      /* "DEMO.BIN" */
    char name[RTR_NAME_MAX];            /* "demo": the name the manifest uses */
    char description[RTR_PROGRAM_DESCRIPTION_MAX];
    uint32_t size;
    bool valid;                         /* the header is a program header */
    bool realtime;                      /* RTR_PROGRAM_FLAG_REALTIME */
};

#define SYSTEM_PROGRAMS_MAX 16U

/* Lists the programs on the card, reading their headers. Returns how many were found. */
uint32_t system_programs(struct program_info *out, uint32_t capacity);

/* Device names the manifest may use. */
const char *const *system_device_names(size_t *count);

/*
 * Buffer for an application package being uploaded through the web
 * interface; only one upload at a time.
 */
uint8_t *system_upload_buffer(size_t *capacity);

/*
 * Unpacks a package (.rpkg) from the upload buffer onto the card. On
 * success `files` gets a comma-separated list of the files written. On
 * failure returns false and fills `error`.
 */
bool system_package_install(size_t size, char *files, size_t files_capacity,
                            char *error, size_t error_capacity) __attribute__((warn_unused_result));

/*
 * Validates `text` as a manifest, writes it to the card and schedules a
 * reboot. On failure returns false and fills `error`.
 */
bool system_config_save(const char *text, size_t size, char *error, size_t error_capacity,
                        uint32_t *line) __attribute__((warn_unused_result));

#endif
