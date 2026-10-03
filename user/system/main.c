/*
 * RTR-OS - the system process (the boot set).
 *
 * Owns the SD card and the Ethernet controller. At startup it reads the
 * manifest from the card, asks the kernel to create the processes it
 * describes and seals the installation. Then it runs the network stack and
 * the web interface, by polling, every period. It does not use interrupts.
 */
#include <string.h>

#include <rtr/user.h>

#include "fat32.h"
#include "genet.h"
#include "http.h"
#include "lwip/dhcp.h"
#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"
#include "sdcard.h"
#include "system.h"

#define RECEIVE_BURST       32U         /* frames handled per period, at most */
#define LINK_POLL_PERIODS   250U        /* periods between link polls */
#define IP_TEXT_MAX         16U
#define MILLIS              1000U
#define MANIFEST_FILE       "MANIFEST.TXT"
#define REBOOT_DELAY_MS     800U        /* lets the response reach the browser first */
#define PROGRAM_IMAGE_MAX   (2U * 1024U * 1024U)
#define PROGRAM_EXTENSION   "BIN"

int program_main(uint64_t argument);
u32_t sys_now(void);

static struct netif interface;
static char ip_text[IP_TEXT_MAX] = "0.0.0.0";
static struct config_state config;
static char manifest_text[MANIFEST_TEXT_MAX];
static char candidate_text[MANIFEST_TEXT_MAX];
static uint64_t reboot_instant;
static uint8_t image_buffer[PROGRAM_IMAGE_MAX];   /* a program file, while the kernel loads it */

static const char *const device_names[] = { "ethernet", "sdcard", "gpio" };

void netweb_fatal(const char *message)
{
    rtr_print("system: internal failure: ");
    rtr_print(message);
    rtr_print("\n");
    for (;;) {
        rtr_wait_period();
    }
}

/* Not a source of secrets: it only spreads ports and transaction identifiers. */
uint32_t netweb_random(void)
{
    static uint32_t state;

    state = (state * 1664525U) + 1013904223U + (uint32_t)rtr_counter();
    return state;
}

/* The stack clock, in milliseconds, taken from the system counter. */
u32_t sys_now(void)
{
    return (u32_t)((rtr_counter() * MILLIS) / rtr_counter_hz());
}

const char *system_ip_text(void)
{
    return ip_text;
}

const struct config_state *system_config(void)
{
    return &config;
}

/* "DEMO.BIN" -> "demo"; false if the base name does not fit a manifest name. */
static bool program_name_from_file(const char *file, char *name)
{
    size_t i = 0U;

    while ((file[i] != '\0') && (file[i] != '.')) {
        if (i >= (RTR_NAME_MAX - 1U)) {
            return false;
        }
        name[i] = (char)(((file[i] >= 'A') && (file[i] <= 'Z')) ? (file[i] + 32) : file[i]);
        i++;
    }
    name[i] = '\0';
    return i != 0U;
}

/* "demo" -> "DEMO.BIN"; false if the name does not fit a short file name. */
static bool program_file_from_name(const char *name, char *file)
{
    size_t length = strlen(name);

    if ((length == 0U) || (length > 8U)) {
        return false;
    }
    for (size_t i = 0U; i < length; i++) {
        file[i] = (char)(((name[i] >= 'a') && (name[i] <= 'z')) ? (name[i] - 32) : name[i]);
    }
    (void)memcpy(&file[length], "." PROGRAM_EXTENSION, sizeof("." PROGRAM_EXTENSION));
    return true;
}

uint32_t system_programs(struct program_info *out, uint32_t capacity)
{
    struct fat32_entry entries[SYSTEM_PROGRAMS_MAX];
    uint32_t found;
    uint32_t count = 0U;

    if (!config.card_present) {
        return 0U;
    }
    found = fat32_list(PROGRAM_EXTENSION, entries, SYSTEM_PROGRAMS_MAX);
    for (uint32_t i = 0U; (i < found) && (count < capacity); i++) {
        struct program_info *info = &out[count];
        struct rtr_program_header header;
        size_t size = 0U;

        (void)memset(info, 0, sizeof(*info));
        (void)memcpy(info->file, entries[i].name, sizeof(info->file));
        info->size = entries[i].size;
        if (!program_name_from_file(info->file, info->name)) {
            continue;
        }
        if (fat32_read_head(info->file, (uint8_t *)&header, sizeof(header), &size) &&
            (size == sizeof(header)) && (header.magic == RTR_PROGRAM_MAGIC)) {
            info->valid = true;
            info->realtime = (header.flags & RTR_PROGRAM_FLAG_REALTIME) != 0U;
            (void)memcpy(info->description, header.description, sizeof(info->description));
            info->description[sizeof(info->description) - 1U] = '\0';
        }
        count++;
    }
    return count;
}

/*
 * Reads the program file named by the manifest entry and asks the kernel to
 * create the process. Whether the process is real-time is not a choice: it
 * comes from the program's own header.
 */
static int64_t create_from_card(struct rtr_process_spec *spec)
{
    struct rtr_program_header header;
    char file[13];
    size_t size = 0U;

    if (!program_file_from_name(spec->program, file)) {
        return RTR_ERR_ARGUMENT;
    }
    if (!fat32_read(file, image_buffer, sizeof(image_buffer), &size)) {
        return RTR_ERR_NOT_FOUND;
    }
    if (size < sizeof(header)) {
        return RTR_ERR_REJECTED;
    }
    (void)memcpy(&header, image_buffer, sizeof(header));
    spec->realtime = ((header.flags & RTR_PROGRAM_FLAG_REALTIME) != 0U) ? 1U : 0U;
    if (spec->realtime != 0U) {
        /*
         * Provisional, until dedicated cores exist: on core 0 a real-time
         * process runs ahead of every standard one, limited to half its
         * period so that the system process keeps running.
         */
        spec->priority = 255U;
        spec->system = 0U;
        spec->floor_us = 0U;
        spec->limit_us = spec->period_us / 2U;
        if (spec->core == 0U) {
            spec->core = 1U;
        }
    }
    return rtr_process_create(spec, image_buffer, size);
}

const char *const *system_device_names(size_t *count)
{
    *count = sizeof(device_names) / sizeof(device_names[0]);
    return device_names;
}

/* ---- installation ---------------------------------------------------- */

static void load_manifest_text(const char *text, size_t size)
{
    if (size >= sizeof(manifest_text)) {
        size = sizeof(manifest_text) - 1U;
    }
    (void)memcpy(manifest_text, text, size);
    manifest_text[size] = '\0';
    config.text = manifest_text;
    config.text_size = size;
}

/* Reads MANIFEST.TXT, creating it with the defaults when the card has none. */
static void read_manifest(uint64_t sd_registers)
{
    size_t size = 0U;
    const char *defaults = manifest_default_text();

    config.source = CONFIG_DEFAULT;
    config.writable = false;
    config.card_present = false;

    if (!sdcard_init(sd_registers)) {
        rtr_print("system: no SD card, using the built-in manifest\n");
        load_manifest_text(defaults, strlen(defaults));
        return;
    }
    if (!fat32_mount()) {
        rtr_print("system: SD card without a FAT32 volume, using the built-in manifest\n");
        load_manifest_text(defaults, strlen(defaults));
        return;
    }
    config.writable = true;
    config.card_present = true;

    if (fat32_read(MANIFEST_FILE, (uint8_t *)manifest_text, sizeof(manifest_text) - 1U, &size)) {
        manifest_text[size] = '\0';
        config.text = manifest_text;
        config.text_size = size;
        config.source = CONFIG_FROM_CARD;
        rtr_print("system: manifest read from the SD card\n");
        return;
    }

    load_manifest_text(defaults, strlen(defaults));
    if (fat32_create(MANIFEST_FILE) &&
        fat32_write(MANIFEST_FILE, (const uint8_t *)manifest_text, config.text_size)) {
        config.source = CONFIG_CREATED;
        rtr_print("system: no manifest on the card, the default one was written\n");
    } else {
        config.writable = false;
        rtr_print("system: no manifest on the card, and it could not be written\n");
    }
}

/* Asks the kernel to create what the manifest describes, then seals the installation. */
static void apply_manifest(void)
{
    config.valid = manifest_parse(config.text, config.text_size, &config.manifest,
                                  config.error, sizeof(config.error), &config.error_line);
    if (!config.valid) {
        rtr_print("system: manifest rejected at line ");
        rtr_print_dec(config.error_line);
        rtr_print(": ");
        rtr_print(config.error);
        rtr_print("\nsystem: the installation will not start; fix it in the web interface\n");
    } else {
        for (uint32_t i = 0U; i < config.manifest.count; i++) {
            struct rtr_process_spec *spec = &config.manifest.processes[i];
            int64_t result;

            if (strcmp(spec->name, "system") == 0) {
                result = rtr_task_configure(spec);
            } else {
                result = create_from_card(spec);
            }
            if (result != RTR_OK) {
                rtr_print("system: process ");
                rtr_print(spec->name);
                rtr_print((result == RTR_ERR_NOT_FOUND) ? ": program not found on the card\n"
                                                        : " was not created\n");
            }
        }
    }
    (void)rtr_install_seal();
}

static void set_error(char *error, size_t capacity, const char *prefix, const char *name, const char *suffix)
{
    size_t n = 0U;
    const char *parts[3] = { prefix, name, suffix };

    for (uint32_t p = 0U; p < 3U; p++) {
        for (size_t i = 0U; (parts[p][i] != '\0') && ((n + 1U) < capacity); i++) {
            error[n] = parts[p][i];
            n++;
        }
    }
    error[n] = '\0';
}

/* Checks that every program the manifest names is on the card and allows the class asked for. */
static bool check_programs(const struct manifest *parsed, char *error, size_t error_capacity)
{
    for (uint32_t i = 0U; i < parsed->count; i++) {
        const struct rtr_process_spec *spec = &parsed->processes[i];
        struct rtr_program_header header;
        char file[13];
        size_t size = 0U;

        if (strcmp(spec->name, "system") == 0) {
            continue;
        }
        if (!program_file_from_name(spec->program, file) ||
            !fat32_read_head(file, (uint8_t *)&header, sizeof(header), &size) ||
            (size != sizeof(header)) || (header.magic != RTR_PROGRAM_MAGIC)) {
            set_error(error, error_capacity, "program ", spec->program, " is not on the card");
            return false;
        }
    }
    return true;
}

/* ---- application packages ------------------------------------------- */

#define RPKG_MAGIC          "RPKG1\0\0\0"
#define RPKG_HEADER_SIZE    16U
#define RPKG_ENTRY_SIZE     24U
#define RPKG_NAME_SIZE      16U
#define RPKG_ENTRIES_MAX    32U

uint8_t *system_upload_buffer(size_t *capacity)
{
    *capacity = sizeof(image_buffer);
    return image_buffer;
}

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

/* An 8.3 file name in the root directory: letters, digits, - and _, upper case, no path. */
static bool package_name_valid(const char *name)
{
    size_t base = 0U;
    size_t ext = 0U;
    bool dot = false;

    for (size_t i = 0U; i < RPKG_NAME_SIZE; i++) {
        char c = name[i];

        if (c == '\0') {
            return (base != 0U) && (!dot || (ext != 0U));
        }
        if (c == '.') {
            if (dot || (base == 0U)) {
                return false;
            }
            dot = true;
            continue;
        }
        if (!(((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) || (c == '_') || (c == '-'))) {
            return false;
        }
        if (dot) {
            ext++;
        } else {
            base++;
        }
        if ((base > 8U) || (ext > 3U)) {
            return false;
        }
    }
    return false;                       /* not terminated */
}

bool system_package_install(size_t size, char *files, size_t files_capacity,
                            char *error, size_t error_capacity)
{
    uint32_t count;
    size_t listed = 0U;

    files[0] = '\0';
    if (!config.writable) {
        set_error(error, error_capacity, "no writable SD card", "", "");
        return false;
    }
    if ((size < RPKG_HEADER_SIZE) || (memcmp(image_buffer, RPKG_MAGIC, 8U) != 0)) {
        set_error(error, error_capacity, "not an RTR-OS package (.rpkg)", "", "");
        return false;
    }
    count = read_u32(&image_buffer[8]);
    if ((count == 0U) || (count > RPKG_ENTRIES_MAX) ||
        ((RPKG_HEADER_SIZE + ((size_t)count * RPKG_ENTRY_SIZE)) > size)) {
        set_error(error, error_capacity, "package entry table is invalid", "", "");
        return false;
    }

    /* Validate everything before writing anything. */
    for (uint32_t i = 0U; i < count; i++) {
        const uint8_t *entry = &image_buffer[RPKG_HEADER_SIZE + (i * RPKG_ENTRY_SIZE)];
        const char *name = (const char *)entry;
        uint32_t offset = read_u32(&entry[16]);
        uint32_t length = read_u32(&entry[20]);

        if (!package_name_valid(name)) {
            set_error(error, error_capacity, "invalid file name in package", "", "");
            return false;
        }
        if (strcmp(name, MANIFEST_FILE) == 0) {
            set_error(error, error_capacity, "a package may not replace ", MANIFEST_FILE, "");
            return false;
        }
        if ((offset > size) || (length > (size - offset))) {
            set_error(error, error_capacity, "package entry out of bounds: ", name, "");
            return false;
        }
    }

    for (uint32_t i = 0U; i < count; i++) {
        const uint8_t *entry = &image_buffer[RPKG_HEADER_SIZE + (i * RPKG_ENTRY_SIZE)];
        const char *name = (const char *)entry;
        uint32_t offset = read_u32(&entry[16]);
        uint32_t length = read_u32(&entry[20]);
        size_t name_length = strlen(name);

        if (!fat32_create(name) || !fat32_write(name, &image_buffer[offset], length)) {
            set_error(error, error_capacity, "write to the SD card failed: ", name, "");
            return false;
        }
        if ((listed + name_length + 2U) < files_capacity) {
            if (listed != 0U) {
                files[listed] = ',';
                listed++;
            }
            (void)memcpy(&files[listed], name, name_length);
            listed += name_length;
            files[listed] = '\0';
        }
        rtr_print("system: package installed ");
        rtr_print(name);
        rtr_print("\n");
    }
    return true;
}

bool system_config_save(const char *text, size_t size, char *error, size_t error_capacity,
                        uint32_t *line)
{
    struct manifest parsed;

    if ((text == NULL) || (size == 0U) || (size >= sizeof(candidate_text))) {
        (void)memcpy(error, "manifest too large", 19U);
        *line = 0U;
        return false;
    }
    if (!manifest_parse(text, size, &parsed, error, error_capacity, line)) {
        return false;
    }
    if (!check_programs(&parsed, error, error_capacity)) {
        *line = 0U;
        return false;
    }
    if (!config.writable) {
        (void)memcpy(error, "no writable SD card", 20U);
        *line = 0U;
        return false;
    }
    if (config.reboot_pending) {
        (void)memcpy(error, "reboot already pending", 23U);
        *line = 0U;
        return false;
    }

    (void)memcpy(candidate_text, text, size);
    candidate_text[size] = '\0';
    if (!fat32_write(MANIFEST_FILE, (const uint8_t *)candidate_text, size)) {
        (void)memcpy(error, "write to the SD card failed", 28U);
        *line = 0U;
        return false;
    }

    rtr_print("system: manifest saved, rebooting\n");
    config.reboot_pending = true;
    reboot_instant = rtr_counter() + ((REBOOT_DELAY_MS * rtr_counter_hz()) / MILLIS);
    return true;
}

/* ---- network --------------------------------------------------------- */

static err_t link_output(struct netif *netif, struct pbuf *p)
{
    uint8_t *frame = genet_send_begin();

    (void)netif;
    if ((frame == NULL) || (p->tot_len > GENET_FRAME_MAX)) {
        return ERR_BUF;
    }
    (void)pbuf_copy_partial(p, frame, p->tot_len, 0U);
    return genet_send_commit(p->tot_len) ? ERR_OK : ERR_BUF;
}

static err_t interface_init(struct netif *netif)
{
    netif->name[0] = 'e';
    netif->name[1] = 'n';
    netif->hostname = "rtr-os";
    netif->mtu = 1500U;
    netif->hwaddr_len = GENET_MAC_SIZE;
    (void)memcpy(netif->hwaddr, genet_mac(), GENET_MAC_SIZE);
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    netif->output = etharp_output;
    netif->linkoutput = link_output;
    return ERR_OK;
}

static void on_status(struct netif *netif)
{
    const char *text = ip4addr_ntoa(netif_ip4_addr(netif));
    size_t i = 0U;

    while ((i < (IP_TEXT_MAX - 1U)) && (text[i] != '\0')) {
        ip_text[i] = text[i];
        i++;
    }
    ip_text[i] = '\0';

    if (!ip4_addr_isany(netif_ip4_addr(netif))) {
        rtr_print("net: address ");
        rtr_print(ip_text);
        rtr_print(", web interface at http://");
        rtr_print(ip_text);
        rtr_print("/\n");
    }
}

static void receive_frames(void)
{
    for (uint32_t i = 0U; i < RECEIVE_BURST; i++) {
        const uint8_t *frame = NULL;
        size_t size = 0U;
        struct pbuf *p;

        if (!genet_receive(&frame, &size)) {
            break;
        }
        p = pbuf_alloc(PBUF_RAW, (u16_t)size, PBUF_POOL);
        if (p != NULL) {
            (void)pbuf_take(p, frame, (u16_t)size);
            if (interface.input(p, &interface) != ERR_OK) {
                pbuf_free(p);
            }
        }
        genet_receive_done();
    }
}

static void link_check(void)
{
    bool changed = false;
    bool up = genet_link_poll(&changed);

    if (!changed) {
        return;
    }
    if (up) {
        rtr_print("net: link up at ");
        rtr_print_dec(genet_link_speed());
        rtr_print(" Mbps\n");
        netif_set_link_up(&interface);
    } else {
        rtr_print("net: link down\n");
        netif_set_link_down(&interface);
    }
}

static bool find_region(uint32_t kind, const char *name, struct rtr_region *out)
{
    for (uint64_t index = 0U; rtr_region_info(index, out) == RTR_OK; index++) {
        if ((out->kind == kind) && ((name == NULL) || (strcmp(out->name, name) == 0))) {
            return true;
        }
    }
    return false;
}

int program_main(uint64_t argument)
{
    struct rtr_region ethernet;
    struct rtr_region sdcard;
    struct rtr_region dma;
    uint32_t periods = 0U;

    (void)argument;

    if (!find_region(RTR_REGION_DEVICE, "ethernet", &ethernet) ||
        !find_region(RTR_REGION_DEVICE, "sdcard", &sdcard) ||
        !find_region(RTR_REGION_DMA, NULL, &dma)) {
        netweb_fatal("the process was not given its devices or DMA memory");
    }

    read_manifest(sdcard.address);
    apply_manifest();

    if (!genet_init(ethernet.address, dma.address, dma.physical, dma.size)) {
        netweb_fatal("Ethernet controller did not initialize");
    }

    lwip_init();
    if (netif_add(&interface, NULL, NULL, NULL, NULL, interface_init, ethernet_input) == NULL) {
        netweb_fatal("network interface was not created");
    }
    netif_set_status_callback(&interface, on_status);
    netif_set_default(&interface);
    netif_set_up(&interface);
    (void)dhcp_start(&interface);

    if (!http_init()) {
        netweb_fatal("web server could not open port 80");
    }
    rtr_print("net: controller ready, waiting for link\n");

    for (;;) {
        if ((periods % LINK_POLL_PERIODS) == 0U) {
            link_check();
        }
        periods++;

        receive_frames();
        sys_check_timeouts();

        if (config.reboot_pending && (rtr_counter() >= reboot_instant)) {
            rtr_reboot();
        }
        rtr_wait_period();
    }
}
