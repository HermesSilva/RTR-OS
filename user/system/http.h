/* RTR-OS - the system web server. */
#ifndef NETWEB_HTTP_H
#define NETWEB_HTTP_H

#include <stdbool.h>
#include <stdint.h>

/* Starts accepting connections on port 80. */
bool http_init(void);

struct http_counters {
    uint64_t connections;
    uint64_t requests;
    uint64_t refused;                   /* connections refused for lack of a slot */
};

const struct http_counters *http_counters(void);

#endif
