/*
 * RTR-OS - the system web server.
 *
 * Routes: "/" delivers the web page; "/api/stats" delivers the statistics
 * as JSON, read from the kernel on every request; "/api/config" delivers the
 * installation manifest (GET) or receives a new one and reboots (POST).
 * Each connection serves one request and is then closed.
 */
#include "http.h"

#include <string.h>

#include <rtr/user.h>

#include "genet.h"
#include "lwip/tcp.h"
#include "system.h"

#define HTTP_PORT           80U
#define CONNECTIONS_MAX     8U
#define REQUEST_MAX         (MANIFEST_TEXT_MAX + 1024U)     /* headers plus a whole manifest */
#define HEADER_MAX          192U
#define JSON_MAX            12288U
#define IDLE_POLLS_MAX      10U         /* 0.5 s polls without activity before closing */

struct connection {
    bool used;
    struct tcp_pcb *pcb;
    char request[REQUEST_MAX];
    size_t request_size;
    bool answered;

    char header[HEADER_MAX];
    size_t header_size;
    const char *body;
    size_t body_size;
    size_t queued;                      /* bytes already handed to the stack, header included */
    size_t acknowledged;                /* bytes confirmed by the other side */
    uint32_t idle_polls;

    bool uploading;                     /* a package body is being received */
    uint8_t *upload;
    size_t upload_capacity;
    size_t upload_expected;
    size_t upload_size;

    char json[JSON_MAX];
};

static bool upload_in_progress;

/* Text assembly in a fixed-size buffer; `overflow` flags that it did not fit. */
struct writer {
    char *buffer;
    size_t capacity;
    size_t size;
    bool overflow;
};

static struct connection connections[CONNECTIONS_MAX];
static struct http_counters counters;
static struct rtr_stats stats;

static const char not_found[] = "No such route.\n";
static const char unavailable[] = "{\"error\":\"statistics unavailable\"}\n";

static void put_char(struct writer *w, char c)
{
    if ((w->size + 1U) < w->capacity) {
        w->buffer[w->size] = c;
        w->size++;
        w->buffer[w->size] = '\0';
    } else {
        w->overflow = true;
    }
}

static void put_text(struct writer *w, const char *text)
{
    for (size_t i = 0U; text[i] != '\0'; i++) {
        put_char(w, text[i]);
    }
}

static void put_number(struct writer *w, uint64_t value)
{
    char digits[20];
    size_t count = 0U;

    do {
        digits[count] = (char)('0' + (char)(value % 10U));
        count++;
        value /= 10U;
    } while ((value != 0U) && (count < sizeof(digits)));

    while (count > 0U) {
        count--;
        put_char(w, digits[count]);
    }
}

static void put_hex_byte(struct writer *w, uint8_t value)
{
    static const char table[] = "0123456789abcdef";

    put_char(w, table[value >> 4U]);
    put_char(w, table[value & 0xFU]);
}

/* Writes `"name":` and, unless `first`, the comma separating it from the previous field. */
static void put_key(struct writer *w, const char *name, bool first)
{
    if (!first) {
        put_char(w, ',');
    }
    put_char(w, '"');
    put_text(w, name);
    put_text(w, "\":");
}

static void put_field(struct writer *w, const char *name, uint64_t value, bool first)
{
    put_key(w, name, first);
    put_number(w, value);
}

/* Text from the kernel: only characters that need no JSON escaping get through. */
static void put_string_field(struct writer *w, const char *name, const char *text, bool first)
{
    put_key(w, name, first);
    put_char(w, '"');
    for (size_t i = 0U; (i < RTR_NAME_MAX) && (text[i] != '\0'); i++) {
        char c = text[i];

        put_char(w, ((c >= ' ') && (c != '"') && (c != '\\') && (c < 0x7F)) ? c : '?');
    }
    put_char(w, '"');
}

static void put_duration(struct writer *w, const char *name, const struct rtr_duration *d, bool first)
{
    put_key(w, name, first);
    put_char(w, '{');
    put_field(w, "count", d->count, true);
    put_field(w, "min", d->minimum, false);
    put_field(w, "max", d->maximum, false);
    put_field(w, "sum", d->sum, false);
    put_field(w, "worst", d->worst, false);
    put_field(w, "worst_instant", d->worst_instant, false);
    put_char(w, '}');
}

static void put_task(struct writer *w, const struct rtr_task_stats *task, bool first)
{
    if (!first) {
        put_char(w, ',');
    }
    put_char(w, '{');
    put_string_field(w, "name", task->name, true);
    put_field(w, "state", task->state, false);
    put_field(w, "priority", task->priority, false);
    put_field(w, "realtime", task->realtime, false);
    put_field(w, "period", task->period, false);
    put_field(w, "limit_declared", task->limit_declared, false);
    put_field(w, "limit_effective", task->limit_effective, false);
    put_field(w, "activations", task->activations, false);
    put_field(w, "completions", task->completions, false);
    put_field(w, "overruns", task->overruns, false);
    put_field(w, "unfinished", task->unfinished, false);
    put_field(w, "cpu_total", task->cpu_total, false);
    put_field(w, "cpu_period_max", task->cpu_period_max, false);
    put_duration(w, "response", &task->response, false);
    put_key(w, "fault", false);
    put_char(w, '{');
    put_field(w, "syndrome", task->fault_syndrome, true);
    put_field(w, "address", task->fault_address, false);
    put_field(w, "pc", task->fault_pc, false);
    put_text(w, "}}");
}

static void put_network(struct writer *w)
{
    const struct genet_counters *net = genet_counters();
    const uint8_t *mac = genet_mac();

    put_key(w, "net", false);
    put_char(w, '{');
    put_field(w, "link", (genet_link_speed() != 0U) ? 1U : 0U, true);
    put_field(w, "speed", genet_link_speed(), false);
    put_key(w, "ip", false);
    put_char(w, '"');
    put_text(w, system_ip_text());
    put_char(w, '"');
    put_key(w, "mac", false);
    put_char(w, '"');
    for (size_t i = 0U; i < GENET_MAC_SIZE; i++) {
        if (i != 0U) {
            put_char(w, ':');
        }
        put_hex_byte(w, mac[i]);
    }
    put_char(w, '"');
    put_field(w, "rx_frames", net->rx_frames, false);
    put_field(w, "rx_bytes", net->rx_bytes, false);
    put_field(w, "rx_errors", net->rx_errors, false);
    put_field(w, "tx_frames", net->tx_frames, false);
    put_field(w, "tx_bytes", net->tx_bytes, false);
    put_field(w, "tx_dropped", net->tx_dropped, false);
    put_char(w, '}');

    put_key(w, "http", false);
    put_char(w, '{');
    put_field(w, "connections", counters.connections, true);
    put_field(w, "requests", counters.requests, false);
    put_field(w, "refused", counters.refused, false);
    put_char(w, '}');
}

/* Reads the kernel statistics and writes them as JSON. Returns false if they did not fit. */
static bool build_stats(struct writer *w)
{
    if (rtr_stats_read(&stats) != RTR_OK) {
        return false;
    }

    put_char(w, '{');
    put_key(w, "system", true);
    put_text(w, "\"RTR-OS\"");
    put_field(w, "instant", stats.instant, false);
    put_field(w, "hz", stats.counter_hz, false);
    put_field(w, "boot", stats.boot_instant, false);
    put_field(w, "idle", stats.idle_total, false);
    put_field(w, "switches", stats.context_switches, false);
    put_field(w, "irqs", stats.interrupts, false);
    put_field(w, "irqs_unhandled", stats.interrupts_unhandled, false);
    put_field(w, "syscalls", stats.system_calls, false);
    put_field(w, "timer_skipped", stats.timer_skipped, false);
    put_field(w, "console_dropped", stats.console_dropped, false);
    put_duration(w, "timer_delay", &stats.timer_delay, false);

    put_key(w, "memory", false);
    put_char(w, '{');
    put_field(w, "total", stats.memory_total, true);
    put_field(w, "kernel", stats.memory_kernel, false);
    put_field(w, "processes", stats.memory_processes, false);
    put_char(w, '}');

    put_key(w, "load", false);
    put_char(w, '{');
    put_field(w, "declared", stats.load_declared_ppm, true);
    put_field(w, "effective", stats.load_effective_ppm, false);
    put_field(w, "scaled", stats.scaling_applied, false);
    put_char(w, '}');

    put_network(w);

    put_key(w, "tasks", false);
    put_char(w, '[');
    for (uint32_t i = 0U; (i < stats.task_count) && (i < RTR_STATS_TASKS_MAX); i++) {
        put_task(w, &stats.tasks[i], i == 0U);
    }
    put_text(w, "]}\n");

    return !w->overflow;
}

static void connection_release(struct connection *c)
{
    if (c->uploading) {
        upload_in_progress = false;     /* the upload was cut short */
        c->uploading = false;
    }
    if (c->pcb != NULL) {
        tcp_arg(c->pcb, NULL);
        tcp_recv(c->pcb, NULL);
        tcp_sent(c->pcb, NULL);
        tcp_err(c->pcb, NULL);
        tcp_poll(c->pcb, NULL, 0U);
    }
    c->pcb = NULL;
    c->used = false;
}

static void connection_close(struct connection *c)
{
    struct tcp_pcb *pcb = c->pcb;

    connection_release(c);
    if ((pcb != NULL) && (tcp_close(pcb) != ERR_OK)) {
        tcp_abort(pcb);
    }
}

/* Hands the stack what is left of the response, as far as the send window allows. */
static void send_more(struct connection *c)
{
    size_t total = c->header_size + c->body_size;

    while (c->queued < total) {
        bool in_header = (c->queued < c->header_size);
        const char *source = in_header ? &c->header[c->queued] : &c->body[c->queued - c->header_size];
        size_t remaining = in_header ? (c->header_size - c->queued) : (total - c->queued);
        size_t room = tcp_sndbuf(c->pcb);
        size_t chunk = (remaining < room) ? remaining : room;

        if (chunk == 0U) {
            break;
        }
        /* The header is copied; the body stays where it is until the connection closes. */
        if (tcp_write(c->pcb, source, (u16_t)chunk, in_header ? TCP_WRITE_FLAG_COPY : 0U) != ERR_OK) {
            break;
        }
        c->queued += chunk;
    }
    (void)tcp_output(c->pcb);
}

static void respond(struct connection *c, const char *status, const char *type,
                    const char *body, size_t body_size)
{
    struct writer w = { .buffer = c->header, .capacity = sizeof(c->header) };

    put_text(&w, "HTTP/1.1 ");
    put_text(&w, status);
    put_text(&w, "\r\nContent-Type: ");
    put_text(&w, type);
    put_text(&w, "\r\nContent-Length: ");
    put_number(&w, body_size);
    put_text(&w, "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");

    c->header_size = w.size;
    c->body = body;
    c->body_size = body_size;
    c->queued = 0U;
    c->acknowledged = 0U;
    c->answered = true;
    counters.requests++;
    send_more(c);
}

/* Writes a JSON string with the escapes JSON requires. */
static void put_json_string(struct writer *w, const char *text, size_t size)
{
    static const char hex[] = "0123456789abcdef";

    put_char(w, '"');
    for (size_t i = 0U; i < size; i++) {
        unsigned char c = (unsigned char)text[i];

        if (c == '"') {
            put_text(w, "\\\"");
        } else if (c == '\\') {
            put_text(w, "\\\\");
        } else if (c == '\n') {
            put_text(w, "\\n");
        } else if (c == '\r') {
            put_text(w, "\\r");
        } else if (c == '\t') {
            put_text(w, "\\t");
        } else if (c < 0x20U) {
            put_text(w, "\\u00");
            put_char(w, hex[c >> 4U]);
            put_char(w, hex[c & 0xFU]);
        } else {
            put_char(w, (char)c);
        }
    }
    put_char(w, '"');
}

static void put_name_list(struct writer *w, const char *key, const char *const *names, size_t count)
{
    put_key(w, key, false);
    put_char(w, '[');
    for (size_t i = 0U; i < count; i++) {
        if (i != 0U) {
            put_char(w, ',');
        }
        put_json_string(w, names[i], strlen(names[i]));
    }
    put_char(w, ']');
}

static void put_process_spec(struct writer *w, const struct rtr_process_spec *spec, bool first)
{
    bool first_device = true;
    bool first_dma = true;

    if (!first) {
        put_char(w, ',');
    }
    put_char(w, '{');
    put_key(w, "name", true);
    put_json_string(w, spec->name, strlen(spec->name));
    put_key(w, "program", false);
    put_json_string(w, spec->program, strlen(spec->program));
    put_field(w, "argument", spec->argument, false);
    put_field(w, "priority", spec->priority, false);
    put_field(w, "realtime", spec->realtime, false);
    put_field(w, "core", spec->core, false);
    put_field(w, "period_us", spec->period_us, false);
    put_field(w, "limit_us", spec->limit_us, false);
    put_field(w, "floor_us", spec->floor_us, false);
    put_field(w, "system", spec->system, false);
    put_field(w, "stack_pages", spec->stack_pages, false);

    put_key(w, "devices", false);
    put_char(w, '[');
    for (uint32_t i = 0U; i < spec->region_count; i++) {
        if (spec->regions[i].kind == RTR_REGION_DEVICE) {
            if (!first_device) {
                put_char(w, ',');
            }
            put_json_string(w, spec->regions[i].name, strlen(spec->regions[i].name));
            first_device = false;
        }
    }
    put_char(w, ']');

    put_key(w, "dma", false);
    put_char(w, '[');
    for (uint32_t i = 0U; i < spec->region_count; i++) {
        if (spec->regions[i].kind == RTR_REGION_DMA) {
            if (!first_dma) {
                put_char(w, ',');
            }
            put_char(w, '{');
            put_key(w, "name", true);
            put_json_string(w, spec->regions[i].name, strlen(spec->regions[i].name));
            put_field(w, "kib", spec->regions[i].size / 1024U, false);
            put_char(w, '}');
            first_dma = false;
        }
    }
    put_text(w, "]}");
}

/* The configuration as JSON: where it came from, whether it parsed, and its contents. */
static bool build_config(struct writer *w)
{
    const struct config_state *state = system_config();
    static const char *const sources[] = { "card", "created", "default" };
    static struct program_info programs[SYSTEM_PROGRAMS_MAX];
    size_t count;
    const char *const *names;

    put_char(w, '{');
    put_key(w, "source", true);
    put_text(w, "\"");
    put_text(w, sources[state->source]);
    put_text(w, "\"");
    put_field(w, "writable", state->writable ? 1U : 0U, false);
    put_field(w, "valid", state->valid ? 1U : 0U, false);
    put_field(w, "reboot_pending", state->reboot_pending ? 1U : 0U, false);
    put_key(w, "error", false);
    put_json_string(w, state->error, strlen(state->error));
    put_field(w, "error_line", state->error_line, false);

    put_key(w, "programs", false);
    put_char(w, '[');
    count = system_programs(programs, SYSTEM_PROGRAMS_MAX);
    for (size_t i = 0U; i < count; i++) {
        const struct program_info *info = &programs[i];

        if (i != 0U) {
            put_char(w, ',');
        }
        put_char(w, '{');
        put_key(w, "name", true);
        put_json_string(w, info->name, strlen(info->name));
        put_key(w, "file", false);
        put_json_string(w, info->file, strlen(info->file));
        put_key(w, "description", false);
        put_json_string(w, info->description, strlen(info->description));
        put_field(w, "size", info->size, false);
        put_field(w, "valid", info->valid ? 1U : 0U, false);
        put_field(w, "realtime", info->realtime ? 1U : 0U, false);
        put_char(w, '}');
    }
    put_char(w, ']');
    names = system_device_names(&count);
    put_name_list(w, "devices", names, count);

    put_key(w, "processes", false);
    put_char(w, '[');
    if (state->valid) {
        for (uint32_t i = 0U; i < state->manifest.count; i++) {
            put_process_spec(w, &state->manifest.processes[i], i == 0U);
        }
    }
    put_char(w, ']');

    put_key(w, "text", false);
    put_json_string(w, state->text, state->text_size);
    put_text(w, "}\n");
    return !w->overflow;
}

/* Position of the first byte after the header block, or 0 if the block is not complete. */
static size_t header_end(const struct connection *c)
{
    for (size_t i = 3U; i < c->request_size; i++) {
        if ((c->request[i - 3U] == '\r') && (c->request[i - 2U] == '\n') &&
            (c->request[i - 1U] == '\r') && (c->request[i] == '\n')) {
            return i + 1U;
        }
    }
    return 0U;
}

static size_t content_length(const struct connection *c, size_t headers)
{
    static const char field[] = "content-length:";
    size_t length = sizeof(field) - 1U;

    for (size_t i = 0U; (i + length) < headers; i++) {
        bool match = true;

        for (size_t k = 0U; k < length; k++) {
            char a = c->request[i + k];

            if ((a >= 'A') && (a <= 'Z')) {
                a = (char)(a + 32);
            }
            if (a != field[k]) {
                match = false;
                break;
            }
        }
        if (match && ((i == 0U) || (c->request[i - 1U] == '\n'))) {
            size_t value = 0U;
            size_t j = i + length;

            while ((j < headers) && (c->request[j] == ' ')) {
                j++;
            }
            while ((j < headers) && (c->request[j] >= '0') && (c->request[j] <= '9')) {
                value = (value * 10U) + (size_t)(c->request[j] - '0');
                j++;
            }
            return value;
        }
    }
    return 0U;
}

static bool method_is(const struct connection *c, const char *method)
{
    size_t length = strlen(method);

    return (c->request_size > length) && (strncmp(c->request, method, length) == 0) &&
           (c->request[length] == ' ');
}

/* True if the request line is "<method> <path> ..." or "<method> <path>?<query> ...". */
static bool path_is(const struct connection *c, const char *method, const char *path)
{
    size_t start = strlen(method) + 1U;
    size_t length = strlen(path);
    char after;

    if (!method_is(c, method) || (c->request_size <= (start + length)) ||
        (strncmp(&c->request[start], path, length) != 0)) {
        return false;
    }
    after = c->request[start + length];
    return (after == ' ') || (after == '?');
}

static void handle_config_save(struct connection *c, size_t headers)
{
    struct writer w = { .buffer = c->json, .capacity = sizeof(c->json) };
    char error[MANIFEST_ERROR_MAX] = "";
    uint32_t line = 0U;
    size_t size = c->request_size - headers;

    if (system_config_save(&c->request[headers], size, error, sizeof(error), &line)) {
        put_text(&w, "{\"status\":\"saved\",\"rebooting\":1}\n");
        respond(c, "200 OK", "application/json; charset=utf-8", c->json, w.size);
        return;
    }

    put_text(&w, "{\"error\":");
    put_json_string(&w, error, strlen(error));
    put_field(&w, "line", line, false);
    put_text(&w, "}\n");
    respond(c, "400 Bad Request", "application/json; charset=utf-8", c->json, w.size);
}

static void handle_request(struct connection *c, size_t headers)
{
    if (path_is(c, "GET", "/") || path_is(c, "GET", "/index.html")) {
        respond(c, "200 OK", "text/html; charset=utf-8", www_index_start,
                (size_t)(www_index_end - www_index_start));
    } else if (path_is(c, "GET", "/api/stats")) {
        struct writer w = { .buffer = c->json, .capacity = sizeof(c->json) };

        if (build_stats(&w)) {
            respond(c, "200 OK", "application/json; charset=utf-8", c->json, w.size);
        } else {
            respond(c, "503 Service Unavailable", "application/json; charset=utf-8",
                    unavailable, sizeof(unavailable) - 1U);
        }
    } else if (path_is(c, "GET", "/api/config")) {
        struct writer w = { .buffer = c->json, .capacity = sizeof(c->json) };

        if (build_config(&w)) {
            respond(c, "200 OK", "application/json; charset=utf-8", c->json, w.size);
        } else {
            respond(c, "503 Service Unavailable", "application/json; charset=utf-8",
                    unavailable, sizeof(unavailable) - 1U);
        }
    } else if (path_is(c, "POST", "/api/config")) {
        handle_config_save(c, headers);
    } else if (!method_is(c, "GET") && !method_is(c, "POST")) {
        respond(c, "405 Method Not Allowed", "text/plain; charset=utf-8", not_found, 0U);
    } else {
        respond(c, "404 Not Found", "text/plain; charset=utf-8", not_found, sizeof(not_found) - 1U);
    }
}

/* Switches a package upload to the upload buffer, moving the body bytes already received. */
static void upload_begin(struct connection *c, size_t headers)
{
    size_t capacity = 0U;
    uint8_t *buffer = system_upload_buffer(&capacity);
    size_t expected = content_length(c, headers);
    size_t already = c->request_size - headers;

    if (upload_in_progress || (expected == 0U) || (expected > capacity)) {
        struct writer w = { .buffer = c->json, .capacity = sizeof(c->json) };

        put_text(&w, upload_in_progress ? "{\"error\":\"another upload is in progress\"}\n"
                                        : "{\"error\":\"package missing or larger than 2 MiB\"}\n");
        respond(c, "400 Bad Request", "application/json; charset=utf-8", c->json, w.size);
        return;
    }

    upload_in_progress = true;
    c->uploading = true;
    c->upload = buffer;
    c->upload_capacity = capacity;
    c->upload_expected = expected;
    c->upload_size = (already < capacity) ? already : capacity;
    (void)memcpy(buffer, &c->request[headers], c->upload_size);
    c->request_size = headers;
}

static void handle_package_install(struct connection *c)
{
    struct writer w = { .buffer = c->json, .capacity = sizeof(c->json) };
    char files[256] = "";
    char error[MANIFEST_ERROR_MAX] = "";

    upload_in_progress = false;
    c->uploading = false;
    if (system_package_install(c->upload_size, files, sizeof(files), error, sizeof(error))) {
        put_text(&w, "{\"status\":\"installed\",\"files\":");
        put_json_string(&w, files, strlen(files));
        put_text(&w, "}\n");
        respond(c, "200 OK", "application/json; charset=utf-8", c->json, w.size);
    } else {
        put_text(&w, "{\"error\":");
        put_json_string(&w, error, strlen(error));
        put_text(&w, "}\n");
        respond(c, "400 Bad Request", "application/json; charset=utf-8", c->json, w.size);
    }
}

/* A request is complete when its headers ended and, for a POST, the body arrived too. */
static bool request_complete(const struct connection *c, size_t *headers)
{
    *headers = header_end(c);
    if (*headers == 0U) {
        return c->request_size >= (REQUEST_MAX - 1U);
    }
    if (method_is(c, "POST")) {
        return c->request_size >= (*headers + content_length(c, *headers));
    }
    return true;
}

static err_t on_receive(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t error)
{
    struct connection *c = arg;

    if ((c == NULL) || (error != ERR_OK) || (p == NULL)) {
        if (p != NULL) {
            pbuf_free(p);
        }
        if (c != NULL) {
            connection_close(c);        /* the other side closed, or there was an error */
        }
        return ERR_OK;
    }

    c->idle_polls = 0U;
    if (c->uploading) {
        /* The body of a package upload goes straight to the upload buffer. */
        size_t room = c->upload_capacity - c->upload_size;
        size_t take = (p->tot_len < room) ? p->tot_len : room;

        (void)pbuf_copy_partial(p, &c->upload[c->upload_size], (u16_t)take, 0U);
        c->upload_size += take;
    } else if (!c->answered) {
        size_t room = (REQUEST_MAX - 1U) - c->request_size;
        size_t take = (p->tot_len < room) ? p->tot_len : room;

        (void)pbuf_copy_partial(p, &c->request[c->request_size], (u16_t)take, 0U);
        c->request_size += take;
        c->request[c->request_size] = '\0';
    } else {
        /* data after the request was answered: ignored */
    }
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    if (!c->answered) {
        size_t headers = 0U;

        if (!c->uploading && (header_end(c) != 0U) && path_is(c, "POST", "/api/packages")) {
            upload_begin(c, header_end(c));
        }
        if (c->uploading) {
            if (c->upload_size >= c->upload_expected) {
                handle_package_install(c);
            }
        } else if (request_complete(c, &headers)) {
            handle_request(c, headers);
        } else {
            /* waiting for more */
        }
    }
    return ERR_OK;
}

static err_t on_sent(void *arg, struct tcp_pcb *pcb, u16_t size)
{
    struct connection *c = arg;

    (void)pcb;
    if (c == NULL) {
        return ERR_OK;
    }

    c->idle_polls = 0U;
    c->acknowledged += size;
    if (c->acknowledged >= (c->header_size + c->body_size)) {
        connection_close(c);            /* whole response delivered */
    } else {
        send_more(c);
    }
    return ERR_OK;
}

static void on_error(void *arg, err_t error)
{
    struct connection *c = arg;

    (void)error;
    if (c != NULL) {
        c->pcb = NULL;                  /* the stack already discarded the connection */
        c->used = false;
    }
}

static err_t on_poll(void *arg, struct tcp_pcb *pcb)
{
    struct connection *c = arg;

    if (c == NULL) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }

    c->idle_polls++;
    if (c->idle_polls > IDLE_POLLS_MAX) {
        connection_release(c);
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    if (c->answered) {
        send_more(c);
    }
    return ERR_OK;
}

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t error)
{
    struct connection *c = NULL;

    (void)arg;
    if ((error != ERR_OK) || (pcb == NULL)) {
        return ERR_VAL;
    }

    for (size_t i = 0U; i < CONNECTIONS_MAX; i++) {
        if (!connections[i].used) {
            c = &connections[i];
            break;
        }
    }
    if (c == NULL) {
        counters.refused++;
        tcp_abort(pcb);
        return ERR_ABRT;
    }

    c->used = true;
    c->pcb = pcb;
    c->request_size = 0U;
    c->answered = false;
    c->uploading = false;
    c->upload_size = 0U;
    c->header_size = 0U;
    c->body = NULL;
    c->body_size = 0U;
    c->queued = 0U;
    c->acknowledged = 0U;
    c->idle_polls = 0U;
    counters.connections++;

    tcp_arg(pcb, c);
    tcp_recv(pcb, on_receive);
    tcp_sent(pcb, on_sent);
    tcp_err(pcb, on_error);
    tcp_poll(pcb, on_poll, 1U);
    return ERR_OK;
}

bool http_init(void)
{
    struct tcp_pcb *pcb = tcp_new();
    struct tcp_pcb *listener;

    if (pcb == NULL) {
        return false;
    }
    if (tcp_bind(pcb, IP_ANY_TYPE, HTTP_PORT) != ERR_OK) {
        (void)tcp_close(pcb);
        return false;
    }
    listener = tcp_listen(pcb);
    if (listener == NULL) {
        (void)tcp_close(pcb);
        return false;
    }
    tcp_accept(listener, on_accept);
    return true;
}

const struct http_counters *http_counters(void)
{
    return &counters;
}
