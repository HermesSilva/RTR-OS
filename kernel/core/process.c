/* RTR-OS - process creation from a program image. */
#include "process.h"

#include <stddef.h>

#include "board.h"
#include "console.h"
#include "memory.h"
#include "mmu.h"
#include "page.h"

#define PROGRAM_SIZE_MAX    0x04000000UL    /* 64 MB */
#define STACK_PAGES_MAX     256U
#define REGION_SIZE_MAX     0x01000000UL    /* 16 MB */
#define DEVICE_CLAIMS_MAX   16U
#define MICROS_PER_SECOND   1000000UL

struct device_claim {
    uint64_t start;
    uint64_t size;
};

/* Devices already assigned: each one belongs to a single process (D12). */
static struct device_claim claims[DEVICE_CLAIMS_MAX];
static uint32_t claim_count;

static bool fail(const struct process_config *config, const char *reason)
{
    console_put_text("process ");
    console_put_text(config->name);
    console_put_text(" rejected: ");
    console_put_text(reason);
    console_put_char('\n');
    return false;
}

static bool page_aligned(uint64_t value)
{
    return (value % MMU_PAGE_SIZE) == 0U;
}

static bool header_valid(const struct rtr_program_header *header, uint64_t image_size)
{
    return (header->magic == RTR_PROGRAM_MAGIC) &&
           (header->entry >= RTR_USER_BASE) && (header->entry < header->text_end) &&
           (header->text_end <= header->rodata_end) &&
           (header->rodata_end <= header->data_end) &&
           (header->data_end <= header->bss_end) &&
           (header->bss_end <= (RTR_USER_BASE + PROGRAM_SIZE_MAX)) &&
           page_aligned(header->text_end) && page_aligned(header->rodata_end) &&
           (image_size <= (header->data_end - RTR_USER_BASE));
}

/* Copies the image into fresh pages and maps them with each section's permission. */
static bool load_image(struct task *task, const struct process_config *config,
                       const struct rtr_program_header *header)
{
    for (uint64_t address = RTR_USER_BASE; address < header->bss_end; address += MMU_PAGE_SIZE) {
        uint64_t offset = address - RTR_USER_BASE;
        uint64_t page = page_alloc(1U);
        enum mmu_user_page kind = MMU_USER_DATA;

        if (page == 0U) {
            return false;
        }
        if (offset < config->image_size) {
            uint64_t length = config->image_size - offset;

            if (length > MMU_PAGE_SIZE) {
                length = MMU_PAGE_SIZE;
            }
            (void)memcpy(arch_physical_pointer(page), &config->image[offset], length);
            arch_cache_flush(page, MMU_PAGE_SIZE);
        }

        if (address < header->text_end) {
            kind = MMU_USER_CODE;
        } else if (address < header->rodata_end) {
            kind = MMU_USER_CONST;
        } else {
            /* data */
        }
        if (!mmu_space_map(task->space, address, page, kind)) {
            return false;
        }
        task->memory_bytes += MMU_PAGE_SIZE;
    }
    arch_instruction_cache_flush();
    return true;
}

static bool map_stack(struct task *task, uint64_t pages)
{
    for (uint64_t i = 1U; i <= pages; i++) {
        uint64_t page = page_alloc(1U);

        if ((page == 0U) ||
            !mmu_space_map(task->space, RTR_USER_STACK_TOP - (i * MMU_PAGE_SIZE), page, MMU_USER_DATA)) {
            return false;
        }
        task->memory_bytes += MMU_PAGE_SIZE;
    }
    return true;
}

static bool device_claim(uint64_t start, uint64_t size)
{
    if ((start < BOARD_DEVICE_START) || (size > (BOARD_DEVICE_END - start)) ||
        (claim_count >= DEVICE_CLAIMS_MAX)) {
        return false;
    }
    for (uint32_t i = 0U; i < claim_count; i++) {
        if ((start < (claims[i].start + claims[i].size)) && (claims[i].start < (start + size))) {
            return false;               /* already belongs to another process */
        }
    }
    claims[claim_count].start = start;
    claims[claim_count].size = size;
    claim_count++;
    return true;
}

static void copy_name(char *to, const char *from)
{
    uint32_t i = 0U;

    while ((i < (RTR_NAME_MAX - 1U)) && (from[i] != '\0')) {
        to[i] = from[i];
        i++;
    }
    to[i] = '\0';
}

/* Maps the regions assigned to the process and records them for RTR_SYS_REGION_INFO. */
static bool map_regions(struct task *task, const struct process_config *config)
{
    uint64_t device_cursor = RTR_USER_DEVICE_BASE;
    uint64_t dma_cursor = RTR_USER_DMA_BASE;

    if (config->region_count > SCHED_REGIONS_MAX) {
        return false;
    }

    for (uint32_t i = 0U; i < config->region_count; i++) {
        const struct process_region *source = &config->regions[i];
        struct rtr_region *region = &task->regions[i];
        bool device = (source->kind == RTR_REGION_DEVICE);
        uint64_t physical = source->physical;
        uint64_t *cursor = device ? &device_cursor : &dma_cursor;

        if ((source->size == 0U) || (source->size > REGION_SIZE_MAX) ||
            !page_aligned(source->size) || !page_aligned(physical)) {
            return false;
        }

        if (device) {
            if (!device_claim(physical, source->size)) {
                return false;
            }
        } else if (source->kind == RTR_REGION_DMA) {
            physical = page_alloc(source->size / MMU_PAGE_SIZE);
            if (physical == 0U) {
                return false;
            }
            /* The process sees this memory uncached: nothing may linger in the kernel cache. */
            arch_cache_flush(physical, source->size);
            task->memory_bytes += source->size;
        } else {
            return false;
        }

        for (uint64_t offset = 0U; offset < source->size; offset += MMU_PAGE_SIZE) {
            if (!mmu_space_map(task->space, *cursor + offset, physical + offset,
                               device ? MMU_USER_DEVICE : MMU_USER_DMA)) {
                return false;
            }
        }

        region->kind = source->kind;
        region->address = *cursor;
        region->physical = physical;
        region->size = source->size;
        copy_name(region->name, source->name);
        *cursor += source->size;
        task->region_count++;
    }
    return true;
}

bool process_name_valid(const char *name)
{
    for (uint32_t i = 0U; i < RTR_NAME_MAX; i++) {
        if (name[i] == '\0') {
            return i != 0U;
        }
    }
    return false;
}

static bool names_equal(const char *left, const char *right)
{
    for (uint32_t i = 0U; i < RTR_NAME_MAX; i++) {
        if (left[i] != right[i]) {
            return false;
        }
        if (left[i] == '\0') {
            return true;
        }
    }
    return true;
}


/* Devices the manifest may assign, by name. */
struct device_entry {
    const char *name;
    uint64_t physical;
    uint64_t size;
};

static const struct device_entry devices[] = {
    { "ethernet", BOARD_GENET_BASE, BOARD_GENET_SIZE },
    { "sdcard", BOARD_EMMC2_BASE, BOARD_EMMC2_SIZE },
    { "gpio", BOARD_GPIO_BASE, BOARD_GPIO_SIZE },   /* the kernel only touches it before the processes start */
};

bool process_create_from_spec(const struct rtr_process_spec *spec, const uint8_t *image, uint64_t image_size)
{
    struct process_config config = { 0 };

    if ((spec == NULL) || (image == NULL) || !process_name_valid(spec->name) ||
        !process_name_valid(spec->program)) {
        console_put_text("process rejected: invalid name\n");
        return false;
    }

    config.name = spec->name;
    if (spec->region_count > SCHED_REGIONS_MAX) {
        return fail(&config, "too many regions");
    }

    config.image = image;
    config.image_size = image_size;
    config.realtime = (spec->realtime != 0U);
    config.argument = spec->argument;
    config.priority = spec->priority;
    config.system = (spec->system != 0U);
    config.period_us = spec->period_us;
    config.limit_us = spec->limit_us;
    config.floor_us = spec->floor_us;
    config.stack_pages = spec->stack_pages;
    config.region_count = spec->region_count;

    for (uint32_t i = 0U; i < spec->region_count; i++) {
        const struct rtr_region_spec *source = &spec->regions[i];
        struct process_region *region = &config.regions[i];
        const struct device_entry *device = NULL;

        if (!process_name_valid(source->name)) {
            return fail(&config, "invalid region name");
        }
        region->name = source->name;
        region->kind = source->kind;
        if (source->kind == RTR_REGION_DEVICE) {
            for (uint32_t d = 0U; d < (sizeof(devices) / sizeof(devices[0])); d++) {
                if (names_equal(devices[d].name, source->name)) {
                    device = &devices[d];
                }
            }
            if (device == NULL) {
                return fail(&config, "unknown device");
            }
            region->physical = device->physical;
            region->size = device->size;
        } else {
            region->size = source->size;
        }
    }

    return process_create(&config);
}

bool process_create(const struct process_config *config)
{
    struct rtr_program_header header;
    uint64_t hz = arch_counter_hz();
    struct task *task;

    if ((config == NULL) || (config->image == NULL) || (config->name == NULL)) {
        return false;
    }
    if (config->image_size < sizeof(header)) {
        return fail(config, "image smaller than its header");
    }
    (void)memcpy(&header, config->image, sizeof(header));
    if (!header_valid(&header, config->image_size)) {
        return fail(config, "invalid image header");
    }
    if (config->realtime && ((header.flags & RTR_PROGRAM_FLAG_REALTIME) == 0U)) {
        return fail(config, "the program is not real-time capable");
    }
    if ((config->period_us == 0U) || (config->limit_us > config->period_us) ||
        (config->stack_pages == 0U) || (config->stack_pages > STACK_PAGES_MAX)) {
        return fail(config, "period, limit or stack out of range");
    }

    task = sched_task_alloc();
    if (task == NULL) {
        return fail(config, "task table full");
    }

    task->space = mmu_space_create();
    if (task->space == 0U) {
        task->state = RTR_TASK_FAULTED;
        return fail(config, "out of memory for the address space");
    }
    if (!load_image(task, config, &header) || !map_stack(task, config->stack_pages)) {
        task->state = RTR_TASK_FAULTED;
        return fail(config, "out of memory for the program");
    }
    if (!map_regions(task, config)) {
        task->state = RTR_TASK_FAULTED;
        return fail(config, "invalid region or device already assigned to another process");
    }

    copy_name(task->name, config->name);
    task->priority = config->priority;
    task->system = config->system;
    task->realtime = config->realtime;
    task->period = (config->period_us * hz) / MICROS_PER_SECOND;
    task->limit_declared = (config->limit_us != 0U)
                               ? ((config->limit_us * hz) / MICROS_PER_SECOND)
                               : ((task->period * SCHED_DEFAULT_LIMIT_PPM) / MICROS_PER_SECOND);
    task->limit_floor = (config->floor_us * hz) / MICROS_PER_SECOND;

    if ((task->period == 0U) || (task->limit_declared == 0U)) {
        task->state = RTR_TASK_FAULTED;
        return fail(config, "period or limit below the counter resolution");
    }

    task->frame.pc = header.entry;
    task->frame.pstate = ARCH_PSTATE_EL0;
    task->frame.sp = RTR_USER_STACK_TOP;
    task->frame.x[0] = config->argument;
    return true;
}
