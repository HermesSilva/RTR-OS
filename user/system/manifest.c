/* RTR-OS - the installation manifest: parser and writer. */
#include "manifest.h"

#include <string.h>

#define LINE_MAX            128U
#define WORD_MAX            32U
#define WORDS_MAX           4U
#define KIB                 1024UL
#define MIB                 (1024UL * 1024UL)
#define TIME_MAX_US         3600000000UL    /* one hour */

static const char default_text[] =
    "# RTR-OS manifest\n"
    "version 1\n"
    "\n"
    "process system\n"
    "  priority 4\n"
    "  period 1 ms\n"
    "  limit 800 us\n"
    "  system yes\n"
    "  floor 400 us\n"
    "end\n"
    "\n"
    "process pulse\n"
    "  program pulse\n"
    "  argument 0\n"
    "  core 1\n"
    "  period 1 ms\n"
    "  stack 4\n"
    "end\n"
    "\n"
    "process report\n"
    "  program demo\n"
    "  argument 0\n"
    "  priority 1\n"
    "  period 5 s\n"
    "  limit 200 ms\n"
    "  system no\n"
    "  stack 4\n"
    "end\n";

struct parser {
    const char *text;
    size_t size;
    size_t position;
    uint32_t line;
    char *error;
    size_t error_capacity;
};

const char *manifest_default_text(void)
{
    return default_text;
}

static void set_error(struct parser *p, const char *message)
{
    size_t i = 0U;

    if ((p->error == NULL) || (p->error_capacity == 0U)) {
        return;
    }
    while ((i + 1U < p->error_capacity) && (message[i] != '\0')) {
        p->error[i] = message[i];
        i++;
    }
    p->error[i] = '\0';
}

/* Reads the next line into `words`; returns the word count, or UINT32_MAX at the end of the text. */
static uint32_t next_line(struct parser *p, char words[WORDS_MAX][WORD_MAX])
{
    uint32_t count = 0U;
    size_t length = 0U;
    bool comment = false;

    if (p->position >= p->size) {
        return UINT32_MAX;
    }
    p->line++;

    for (uint32_t w = 0U; w < WORDS_MAX; w++) {
        words[w][0] = '\0';
    }

    while ((p->position < p->size) && (p->text[p->position] != '\n')) {
        char c = p->text[p->position];

        p->position++;
        if (c == '#') {
            comment = true;
        }
        if (comment || (c == '\r')) {
            continue;
        }
        if ((c == ' ') || (c == '\t')) {
            if (length != 0U) {
                count++;
                length = 0U;
            }
            continue;
        }
        if (count >= WORDS_MAX) {
            set_error(p, "too many words on the line");
            return 0U;
        }
        if (length >= (WORD_MAX - 1U)) {
            set_error(p, "word too long");
            return 0U;
        }
        words[count][length] = c;
        length++;
        words[count][length] = '\0';
    }
    if (p->position < p->size) {
        p->position++;                  /* the newline */
    }
    if (length != 0U) {
        count++;
    }
    return count;
}

static bool parse_number(const char *word, uint64_t *value)
{
    uint64_t result = 0U;

    if (word[0] == '\0') {
        return false;
    }
    for (size_t i = 0U; word[i] != '\0'; i++) {
        if ((word[i] < '0') || (word[i] > '9') || (result > (UINT64_MAX / 10U))) {
            return false;
        }
        result = (result * 10U) + (uint64_t)(word[i] - '0');
    }
    *value = result;
    return true;
}

/* "<number> <unit>" with unit us, ms or s; a bare number is microseconds. */
static bool parse_time(const char *number, const char *unit, uint64_t *micros)
{
    uint64_t value;
    uint64_t scale = 1U;

    if (!parse_number(number, &value)) {
        return false;
    }
    if (strcmp(unit, "ms") == 0) {
        scale = 1000U;
    } else if (strcmp(unit, "s") == 0) {
        scale = 1000000U;
    } else if ((strcmp(unit, "us") != 0) && (unit[0] != '\0')) {
        return false;
    } else {
        /* microseconds */
    }
    if (value > (TIME_MAX_US / scale)) {
        return false;
    }
    *micros = value * scale;
    return true;
}

static bool parse_size(const char *number, const char *unit, uint64_t *bytes)
{
    uint64_t value;
    uint64_t scale;

    if (!parse_number(number, &value)) {
        return false;
    }
    if (strcmp(unit, "KiB") == 0) {
        scale = KIB;
    } else if (strcmp(unit, "MiB") == 0) {
        scale = MIB;
    } else {
        return false;
    }
    if (value > (64U * MIB / scale)) {
        return false;
    }
    *bytes = value * scale;
    return true;
}

static bool parse_bool(const char *word, uint32_t *value)
{
    if (strcmp(word, "yes") == 0) {
        *value = 1U;
        return true;
    }
    if (strcmp(word, "no") == 0) {
        *value = 0U;
        return true;
    }
    return false;
}

static bool copy_name(char *to, const char *from)
{
    size_t length = strlen(from);

    if ((length == 0U) || (length >= RTR_NAME_MAX)) {
        return false;
    }
    (void)memcpy(to, from, length + 1U);
    return true;
}

static bool add_region(struct rtr_process_spec *spec, uint32_t kind, const char *name, uint64_t size)
{
    struct rtr_region_spec *region;

    if (spec->region_count >= RTR_SPEC_REGIONS_MAX) {
        return false;
    }
    region = &spec->regions[spec->region_count];
    region->kind = kind;
    region->size = size;
    if (!copy_name(region->name, name)) {
        return false;
    }
    spec->region_count++;
    return true;
}

/* One "key value" line inside a process block. */
static bool parse_field(struct parser *p, struct rtr_process_spec *spec,
                        char words[WORDS_MAX][WORD_MAX], uint32_t count)
{
    const char *key = words[0];
    uint64_t value;

    if (strcmp(key, "program") == 0) {
        return (count == 2U) && copy_name(spec->program, words[1]);
    }
    if (strcmp(key, "argument") == 0) {
        return (count == 2U) && parse_number(words[1], &spec->argument);
    }
    if (strcmp(key, "priority") == 0) {
        if ((count != 2U) || !parse_number(words[1], &value) || (value > 255U)) {
            return false;
        }
        spec->priority = (uint32_t)value;
        return true;
    }
    if (strcmp(key, "period") == 0) {
        return ((count == 2U) || (count == 3U)) && parse_time(words[1], words[2], &spec->period_us);
    }
    if (strcmp(key, "limit") == 0) {
        return ((count == 2U) || (count == 3U)) && parse_time(words[1], words[2], &spec->limit_us);
    }
    if (strcmp(key, "floor") == 0) {
        return ((count == 2U) || (count == 3U)) && parse_time(words[1], words[2], &spec->floor_us);
    }
    if (strcmp(key, "system") == 0) {
        return (count == 2U) && parse_bool(words[1], &spec->system);
    }

    if (strcmp(key, "core") == 0) {
        if ((count != 2U) || !parse_number(words[1], &value) || (value == 0U) || (value > 3U)) {
            return false;
        }
        spec->core = (uint32_t)value;
        return true;
    }
    if (strcmp(key, "stack") == 0) {
        if ((count != 2U) || !parse_number(words[1], &value) || (value == 0U) || (value > 256U)) {
            return false;
        }
        spec->stack_pages = value;
        return true;
    }
    if (strcmp(key, "device") == 0) {
        return (count == 2U) && add_region(spec, RTR_REGION_DEVICE, words[1], 0U);
    }
    if (strcmp(key, "dma") == 0) {
        return (count == 4U) && parse_size(words[2], words[3], &value) &&
               add_region(spec, RTR_REGION_DMA, words[1], value);
    }
    set_error(p, "unknown field");
    return false;
}

bool manifest_parse(const char *text, size_t size, struct manifest *out,
                    char *error, size_t error_capacity, uint32_t *line)
{
    char local_error[MANIFEST_ERROR_MAX];
    uint32_t local_line;
    struct parser p;
    char words[WORDS_MAX][WORD_MAX];
    struct rtr_process_spec *spec = NULL;
    bool version_seen = false;

    if ((text == NULL) || (out == NULL)) {
        return false;
    }
    if ((error == NULL) || (error_capacity == 0U)) {
        error = local_error;
        error_capacity = sizeof(local_error);
    }
    if (line == NULL) {
        line = &local_line;
    }
    p = (struct parser){ .text = text, .size = size, .error = error, .error_capacity = error_capacity };
    (void)memset(out, 0, sizeof(*out));
    error[0] = '\0';

    for (uint32_t guard = 0U; guard < (MANIFEST_TEXT_MAX / 2U); guard++) {
        uint32_t count = next_line(&p, words);
        bool ok;

        if (count == UINT32_MAX) {
            break;
        }
        if (error[0] != '\0') {
            *line = p.line;
            return false;
        }
        if (count == 0U) {
            continue;
        }

        if (spec == NULL) {
            if (strcmp(words[0], "version") == 0) {
                ok = (count == 2U) && (strcmp(words[1], "1") == 0);
                version_seen = ok;
                if (!ok) {
                    set_error(&p, "unsupported manifest version");
                }
            } else if (strcmp(words[0], "process") == 0) {
                ok = (count == 2U) && (out->count < MANIFEST_PROCESSES_MAX);
                if (ok) {
                    spec = &out->processes[out->count];
                    (void)memset(spec, 0, sizeof(*spec));
                    ok = copy_name(spec->name, words[1]);
                    spec->stack_pages = 4U;
                    if (strcmp(words[1], "system") == 0) {
                        (void)copy_name(spec->program, "system");
                    }
                }
                if (!ok) {
                    set_error(&p, "bad process line, or too many processes");
                }
            } else {
                set_error(&p, "expected 'process' or 'version'");
                ok = false;
            }
        } else if (strcmp(words[0], "end") == 0) {
            ok = (spec->program[0] != '\0') && (spec->period_us != 0U) &&
                 (spec->limit_us <= spec->period_us);
            if (!ok) {
                set_error(&p, "process needs a program and a period, and limit <= period");
            }
            out->count++;
            spec = NULL;
        } else {
            ok = parse_field(&p, spec, words, count);
            if (!ok && (error[0] == '\0')) {
                set_error(&p, "bad value");
            }
        }

        if (!ok) {
            *line = p.line;
            return false;
        }
    }

    if (spec != NULL) {
        set_error(&p, "process block without 'end'");
        *line = p.line;
        return false;
    }
    if (!version_seen) {
        set_error(&p, "missing 'version 1' line");
        *line = p.line;
        return false;
    }
    return true;
}

struct writer {
    char *text;
    size_t capacity;
    size_t size;
    bool overflow;
};

static void put(struct writer *w, const char *text)
{
    for (size_t i = 0U; text[i] != '\0'; i++) {
        if ((w->size + 1U) < w->capacity) {
            w->text[w->size] = text[i];
            w->size++;
            w->text[w->size] = '\0';
        } else {
            w->overflow = true;
        }
    }
}

static void put_number(struct writer *w, uint64_t value)
{
    char digits[21];
    size_t count = 0U;

    do {
        digits[19U - count] = (char)('0' + (char)(value % 10U));
        count++;
        value /= 10U;
    } while ((value != 0U) && (count < 20U));
    digits[20] = '\0';
    put(w, &digits[20U - count]);
}

/* Writes a time in the largest unit that keeps it an integer. */
static void put_time(struct writer *w, const char *key, uint64_t micros)
{
    put(w, "  ");
    put(w, key);
    put(w, " ");
    if ((micros % 1000000U) == 0U) {
        put_number(w, micros / 1000000U);
        put(w, " s\n");
    } else if ((micros % 1000U) == 0U) {
        put_number(w, micros / 1000U);
        put(w, " ms\n");
    } else {
        put_number(w, micros);
        put(w, " us\n");
    }
}

size_t manifest_serialize(const struct manifest *manifest, char *text, size_t capacity)
{
    struct writer w = { .text = text, .capacity = capacity };

    if ((manifest == NULL) || (text == NULL) || (capacity == 0U)) {
        return 0U;
    }
    text[0] = '\0';
    put(&w, "# RTR-OS manifest\nversion 1\n");

    for (uint32_t i = 0U; (i < manifest->count) && (i < MANIFEST_PROCESSES_MAX); i++) {
        const struct rtr_process_spec *spec = &manifest->processes[i];
        bool is_system = (strcmp(spec->name, "system") == 0);

        put(&w, "\nprocess ");
        put(&w, spec->name);
        put(&w, "\n");
        if (!is_system) {
            put(&w, "  program ");
            put(&w, spec->program);
            put(&w, "\n  argument ");
            put_number(&w, spec->argument);
            put(&w, "\n");
        }
        if (spec->realtime != 0U) {
            put(&w, "  core ");
            put_number(&w, spec->core);
            put(&w, "\n");
            put_time(&w, "period", spec->period_us);
        } else {
            put(&w, "  priority ");
            put_number(&w, spec->priority);
            put(&w, "\n");
            put_time(&w, "period", spec->period_us);
            put_time(&w, "limit", spec->limit_us);
            put(&w, (spec->system != 0U) ? "  system yes\n" : "  system no\n");
            if (spec->system != 0U) {
                put_time(&w, "floor", spec->floor_us);
            }
        }
        if (!is_system) {
            put(&w, "  stack ");
            put_number(&w, spec->stack_pages);
            put(&w, "\n");
            for (uint32_t r = 0U; r < spec->region_count; r++) {
                const struct rtr_region_spec *region = &spec->regions[r];

                if (region->kind == RTR_REGION_DEVICE) {
                    put(&w, "  device ");
                    put(&w, region->name);
                    put(&w, "\n");
                } else {
                    put(&w, "  dma ");
                    put(&w, region->name);
                    put(&w, " ");
                    put_number(&w, region->size / KIB);
                    put(&w, " KiB\n");
                }
            }
        }
        put(&w, "end\n");
    }

    return w.overflow ? 0U : w.size;
}
