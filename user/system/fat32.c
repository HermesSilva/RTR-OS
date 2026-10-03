/* RTR-OS - FAT32 volume on the SD card: files in the root directory only. */
#include "fat32.h"

#include <string.h>

#include "sdcard.h"

#define SECTOR_SIZE         512U
#define ENTRY_SIZE          32U
#define ENTRIES_PER_SECTOR  (SECTOR_SIZE / ENTRY_SIZE)
#define FAT_ENTRY_MASK      0x0FFFFFFFU
#define FAT_END             0x0FFFFFF8U
#define FAT_FREE            0U
#define CLUSTER_FIRST       2U
#define CHAIN_MAX           65536U      /* clusters followed at most: bounds every loop */
#define DIR_SECTORS_MAX     256U        /* root directory sectors examined at most */
#define SHORT_NAME_SIZE     11U

#define ATTR_READ_ONLY      0x01U
#define ATTR_DIRECTORY      0x10U
#define ATTR_VOLUME         0x08U
#define ATTR_LONG_NAME      0x0FU
#define ENTRY_FREE          0x00U
#define ENTRY_DELETED       0xE5U

/* Byte offsets in the boot sector */
#define BPB_BYTES_PER_SECTOR    11U
#define BPB_SECTORS_PER_CLUSTER 13U
#define BPB_RESERVED_SECTORS    14U
#define BPB_FAT_COUNT           16U
#define BPB_FAT_SIZE_32         36U
#define BPB_ROOT_CLUSTER        44U
#define BPB_SIGNATURE           510U
#define MBR_PARTITION           446U
#define MBR_PART_TYPE           4U
#define MBR_PART_START          8U

/* Byte offsets in a directory entry */
#define DIR_ATTR                11U
#define DIR_CLUSTER_HIGH        20U
#define DIR_CLUSTER_LOW         26U
#define DIR_SIZE                28U

struct volume {
    bool mounted;
    uint64_t start;                     /* first sector of the partition */
    uint32_t sectors_per_cluster;
    uint32_t fat_count;
    uint32_t fat_size;                  /* sectors per FAT */
    uint64_t fat_start;                 /* first sector of the first FAT */
    uint64_t data_start;                /* first sector of cluster 2 */
    uint32_t root_cluster;
    uint32_t cluster_count;
};

struct file_entry {
    uint64_t sector;                    /* directory sector holding the entry */
    uint32_t offset;                    /* byte offset of the entry in it */
    uint32_t first_cluster;
    uint32_t size;
};

static struct volume volume;
static uint8_t sector[SECTOR_SIZE];     /* single sector buffer */
static uint64_t sector_number = UINT64_MAX;

static uint16_t read16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8U));
}

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static void write16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
}

static void write32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
    p[2] = (uint8_t)(value >> 16U);
    p[3] = (uint8_t)(value >> 24U);
}

static bool load_sector(uint64_t number)
{
    if (sector_number == number) {
        return true;
    }
    sector_number = UINT64_MAX;
    if (!sdcard_read(number, sector)) {
        return false;
    }
    sector_number = number;
    return true;
}

static bool store_sector(uint64_t number)
{
    sector_number = UINT64_MAX;
    if (!sdcard_write(number, sector)) {
        return false;
    }
    sector_number = number;
    return true;
}

static uint64_t cluster_sector(uint32_t cluster)
{
    return volume.data_start + ((uint64_t)(cluster - CLUSTER_FIRST) * volume.sectors_per_cluster);
}

static bool cluster_valid(uint32_t cluster)
{
    return (cluster >= CLUSTER_FIRST) && (cluster < (CLUSTER_FIRST + volume.cluster_count));
}

static bool fat_read(uint32_t cluster, uint32_t *next)
{
    uint64_t number = volume.fat_start + ((cluster * 4U) / SECTOR_SIZE);

    if (!load_sector(number)) {
        return false;
    }
    *next = read32(&sector[(cluster * 4U) % SECTOR_SIZE]) & FAT_ENTRY_MASK;
    return true;
}

/* Writes one FAT entry in every copy of the FAT. */
static bool fat_write(uint32_t cluster, uint32_t value)
{
    for (uint32_t copy = 0U; copy < volume.fat_count; copy++) {
        uint64_t number = volume.fat_start + ((uint64_t)copy * volume.fat_size) +
                          ((cluster * 4U) / SECTOR_SIZE);
        uint32_t offset = (cluster * 4U) % SECTOR_SIZE;

        if (!load_sector(number)) {
            return false;
        }
        write32(&sector[offset], (read32(&sector[offset]) & ~FAT_ENTRY_MASK) | (value & FAT_ENTRY_MASK));
        if (!store_sector(number)) {
            return false;
        }
    }
    return true;
}

static bool fat_alloc(uint32_t *cluster)
{
    for (uint32_t c = CLUSTER_FIRST; c < (CLUSTER_FIRST + volume.cluster_count); c++) {
        uint32_t value;

        if (!fat_read(c, &value)) {
            return false;
        }
        if (value == FAT_FREE) {
            *cluster = c;
            return fat_write(c, FAT_END);
        }
    }
    return false;                       /* volume full */
}

bool fat32_mount(void)
{
    uint64_t start = 0U;
    uint32_t bytes_per_sector;
    uint32_t total_sectors;

    volume.mounted = false;
    sector_number = UINT64_MAX;

    if (!load_sector(0U)) {
        return false;
    }
    if (read16(&sector[BPB_SIGNATURE]) != 0xAA55U) {
        return false;
    }

    /* A partition table has no BPB: the bytes-per-sector field is not a sane value. */
    bytes_per_sector = read16(&sector[BPB_BYTES_PER_SECTOR]);
    if (bytes_per_sector != SECTOR_SIZE) {
        const uint8_t *partition = &sector[MBR_PARTITION];
        uint8_t type = partition[MBR_PART_TYPE];

        if ((type != 0x0BU) && (type != 0x0CU)) {
            return false;               /* the first partition is not FAT32 */
        }
        start = read32(&partition[MBR_PART_START]);
        if (!load_sector(start) || (read16(&sector[BPB_SIGNATURE]) != 0xAA55U) ||
            (read16(&sector[BPB_BYTES_PER_SECTOR]) != SECTOR_SIZE)) {
            return false;
        }
    }

    volume.start = start;
    volume.sectors_per_cluster = sector[BPB_SECTORS_PER_CLUSTER];
    volume.fat_count = sector[BPB_FAT_COUNT];
    volume.fat_size = read32(&sector[BPB_FAT_SIZE_32]);
    volume.root_cluster = read32(&sector[BPB_ROOT_CLUSTER]);
    volume.fat_start = start + read16(&sector[BPB_RESERVED_SECTORS]);
    volume.data_start = volume.fat_start + ((uint64_t)volume.fat_count * volume.fat_size);
    total_sectors = read32(&sector[32]);

    if ((volume.sectors_per_cluster == 0U) || (volume.fat_count == 0U) || (volume.fat_size == 0U) ||
        (total_sectors == 0U) || (volume.data_start >= (start + total_sectors))) {
        return false;
    }
    volume.cluster_count = (uint32_t)((start + total_sectors - volume.data_start) / volume.sectors_per_cluster);
    volume.mounted = cluster_valid(volume.root_cluster);
    return volume.mounted;
}

/* Converts "MANIFEST.TXT" into the 11-byte padded directory form. */
static bool short_name(const char *name, uint8_t *out)
{
    size_t i = 0U;
    size_t position = 0U;

    (void)memset(out, ' ', SHORT_NAME_SIZE);
    while ((name[i] != '\0') && (name[i] != '.')) {
        if (position >= 8U) {
            return false;
        }
        out[position] = (uint8_t)((name[i] >= 'a') && (name[i] <= 'z') ? (name[i] - 32) : name[i]);
        position++;
        i++;
    }
    if (name[i] == '.') {
        i++;
        position = 8U;
        while (name[i] != '\0') {
            if (position >= SHORT_NAME_SIZE) {
                return false;
            }
            out[position] = (uint8_t)((name[i] >= 'a') && (name[i] <= 'z') ? (name[i] - 32) : name[i]);
            position++;
            i++;
        }
    }
    return true;
}

/* Looks for `name` in the root directory. With `want_free`, returns the first free entry instead. */
static bool find_entry(const char *name, bool want_free, struct file_entry *entry)
{
    uint8_t wanted[SHORT_NAME_SIZE];
    uint32_t cluster = volume.root_cluster;

    if (!short_name(name, wanted)) {
        return false;
    }

    for (uint32_t step = 0U; (step < DIR_SECTORS_MAX) && cluster_valid(cluster); step++) {
        for (uint32_t s = 0U; s < volume.sectors_per_cluster; s++) {
            uint64_t number = cluster_sector(cluster) + s;

            if (!load_sector(number)) {
                return false;
            }
            for (uint32_t e = 0U; e < ENTRIES_PER_SECTOR; e++) {
                const uint8_t *d = &sector[e * ENTRY_SIZE];

                if (d[0] == ENTRY_FREE) {
                    if (want_free) {
                        entry->sector = number;
                        entry->offset = e * ENTRY_SIZE;
                        return true;
                    }
                    return false;       /* end of the directory */
                }
                if (want_free && (d[0] == ENTRY_DELETED)) {
                    entry->sector = number;
                    entry->offset = e * ENTRY_SIZE;
                    return true;
                }
                if (want_free || (d[0] == ENTRY_DELETED) || (d[DIR_ATTR] == ATTR_LONG_NAME) ||
                    ((d[DIR_ATTR] & (ATTR_DIRECTORY | ATTR_VOLUME)) != 0U)) {
                    continue;
                }
                if (memcmp(d, wanted, SHORT_NAME_SIZE) == 0) {
                    entry->sector = number;
                    entry->offset = e * ENTRY_SIZE;
                    entry->first_cluster = ((uint32_t)read16(&d[DIR_CLUSTER_HIGH]) << 16U) |
                                           read16(&d[DIR_CLUSTER_LOW]);
                    entry->size = read32(&d[DIR_SIZE]);
                    return true;
                }
            }
        }
        if (!fat_read(cluster, &cluster)) {
            return false;
        }
    }
    return false;
}

/* Reads the file into `buffer`, stopping at `capacity` bytes if `partial`, failing otherwise. */
static bool read_file(const char *name, uint8_t *buffer, size_t capacity, size_t *size, bool partial)
{
    struct file_entry entry;
    uint32_t cluster;
    size_t wanted;
    size_t done = 0U;

    if (!volume.mounted || (buffer == NULL) || (size == NULL) || !find_entry(name, false, &entry)) {
        return false;
    }
    if ((entry.size > capacity) && !partial) {
        return false;
    }
    wanted = (entry.size < capacity) ? entry.size : capacity;

    cluster = entry.first_cluster;
    for (uint32_t step = 0U; (step < CHAIN_MAX) && (done < wanted); step++) {
        if (!cluster_valid(cluster)) {
            return false;
        }
        for (uint32_t s = 0U; (s < volume.sectors_per_cluster) && (done < wanted); s++) {
            size_t chunk = wanted - done;

            if (!load_sector(cluster_sector(cluster) + s)) {
                return false;
            }
            if (chunk > SECTOR_SIZE) {
                chunk = SECTOR_SIZE;
            }
            (void)memcpy(&buffer[done], sector, chunk);
            done += chunk;
        }
        if (!fat_read(cluster, &cluster)) {
            return false;
        }
    }
    *size = done;
    return done == wanted;
}

bool fat32_read(const char *name, uint8_t *buffer, size_t capacity, size_t *size)
{
    return read_file(name, buffer, capacity, size, false);
}

bool fat32_read_head(const char *name, uint8_t *buffer, size_t capacity, size_t *size)
{
    return read_file(name, buffer, capacity, size, true);
}

/* "NAME    EXT" in directory form -> "NAME.EXT". */
static void long_name(const uint8_t *entry, char *out)
{
    size_t n = 0U;

    for (size_t i = 0U; (i < 8U) && (entry[i] != ' '); i++) {
        out[n++] = (char)entry[i];
    }
    if (entry[8] != ' ') {
        out[n++] = '.';
        for (size_t i = 8U; (i < SHORT_NAME_SIZE) && (entry[i] != ' '); i++) {
            out[n++] = (char)entry[i];
        }
    }
    out[n] = '\0';
}

uint32_t fat32_list(const char *extension, struct fat32_entry *out, uint32_t capacity)
{
    uint32_t cluster = volume.root_cluster;
    uint32_t count = 0U;
    uint8_t wanted[3] = { ' ', ' ', ' ' };

    if (!volume.mounted || (out == NULL)) {
        return 0U;
    }
    for (size_t i = 0U; (i < 3U) && (extension[i] != '\0'); i++) {
        wanted[i] = (uint8_t)((extension[i] >= 'a') && (extension[i] <= 'z') ? (extension[i] - 32) : extension[i]);
    }

    for (uint32_t step = 0U; (step < DIR_SECTORS_MAX) && cluster_valid(cluster); step++) {
        for (uint32_t s = 0U; s < volume.sectors_per_cluster; s++) {
            if (!load_sector(cluster_sector(cluster) + s)) {
                return count;
            }
            for (uint32_t e = 0U; e < ENTRIES_PER_SECTOR; e++) {
                const uint8_t *d = &sector[e * ENTRY_SIZE];

                if (d[0] == ENTRY_FREE) {
                    return count;
                }
                if ((d[0] == ENTRY_DELETED) || (d[DIR_ATTR] == ATTR_LONG_NAME) ||
                    ((d[DIR_ATTR] & (ATTR_DIRECTORY | ATTR_VOLUME)) != 0U) ||
                    (memcmp(&d[8], wanted, 3U) != 0)) {
                    continue;
                }
                if (count >= capacity) {
                    return count;
                }
                long_name(d, out[count].name);
                out[count].size = read32(&d[DIR_SIZE]);
                count++;
            }
        }
        if (!fat_read(cluster, &cluster)) {
            return count;
        }
    }
    return count;
}

/* Rewrites the directory entry's first cluster and size. */
static bool update_entry(const struct file_entry *entry, uint32_t first_cluster, uint32_t size)
{
    uint8_t *d;

    if (!load_sector(entry->sector)) {
        return false;
    }
    d = &sector[entry->offset];
    write16(&d[DIR_CLUSTER_HIGH], (uint16_t)(first_cluster >> 16U));
    write16(&d[DIR_CLUSTER_LOW], (uint16_t)first_cluster);
    write32(&d[DIR_SIZE], size);
    return store_sector(entry->sector);
}

bool fat32_write(const char *name, const uint8_t *data, size_t size)
{
    struct file_entry entry;
    uint32_t cluster_bytes = volume.sectors_per_cluster * SECTOR_SIZE;
    uint32_t needed;
    uint32_t cluster;
    uint32_t previous = 0U;
    uint32_t first = 0U;
    size_t done = 0U;

    if (!volume.mounted || (data == NULL) || (size > UINT32_MAX) || !find_entry(name, false, &entry)) {
        return false;
    }
    needed = (uint32_t)((size + cluster_bytes - 1U) / cluster_bytes);
    cluster = entry.first_cluster;

    /* Reuse the existing chain cluster by cluster, extending it when it runs out. */
    for (uint32_t i = 0U; i < needed; i++) {
        uint32_t next;

        if (!cluster_valid(cluster)) {
            if (!fat_alloc(&cluster)) {
                return false;
            }
            if (previous != 0U) {
                if (!fat_write(previous, cluster)) {
                    return false;
                }
            }
        }
        if (first == 0U) {
            first = cluster;
        }

        for (uint32_t s = 0U; s < volume.sectors_per_cluster; s++) {
            size_t chunk = (done < size) ? (size - done) : 0U;

            if (chunk > SECTOR_SIZE) {
                chunk = SECTOR_SIZE;
            }
            (void)memset(sector, 0, SECTOR_SIZE);
            (void)memcpy(sector, &data[done], chunk);
            done += chunk;
            if (!store_sector(cluster_sector(cluster) + s)) {
                return false;
            }
        }

        if (!fat_read(cluster, &next)) {
            return false;
        }
        previous = cluster;
        cluster = next;
    }

    /* Terminate the chain and free whatever the old file had beyond it. */
    if (previous != 0U) {
        if (!fat_write(previous, FAT_END)) {
            return false;
        }
    }
    for (uint32_t step = 0U; (step < CHAIN_MAX) && cluster_valid(cluster); step++) {
        uint32_t next;

        if (!fat_read(cluster, &next) || !fat_write(cluster, FAT_FREE)) {
            return false;
        }
        cluster = next;
    }

    return update_entry(&entry, first, (uint32_t)size);
}

bool fat32_create(const char *name)
{
    struct file_entry entry;
    uint8_t short_form[SHORT_NAME_SIZE];
    uint8_t *d;

    if (!volume.mounted || !short_name(name, short_form)) {
        return false;
    }
    if (find_entry(name, false, &entry)) {
        return true;                    /* already exists */
    }
    if (!find_entry(name, true, &entry) || !load_sector(entry.sector)) {
        return false;
    }
    d = &sector[entry.offset];
    (void)memset(d, 0, ENTRY_SIZE);
    (void)memcpy(d, short_form, SHORT_NAME_SIZE);
    return store_sector(entry.sector);
}
