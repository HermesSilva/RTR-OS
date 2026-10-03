/*
 * RTR-OS - driver for the Raspberry Pi 4 Ethernet controller (GENET v5 with a BCM54213PE PHY).
 *
 * The manufacturer does not publish documentation for this controller. The
 * register map used here is the one known from the existing open drivers.
 *
 * Receive: the hardware delivers frames with no specific filter to ring 16;
 * the qemu-pi4 emulator delivers them to ring 0. The driver sets up and
 * polls both, each with half of the descriptors. Transmit: ring 16.
 *
 * With the status block enabled in both directions, every received frame is
 * preceded by 64 bytes of status plus 2 of alignment, and every sent frame
 * is preceded by 64 bytes of status.
 */
#include "genet.h"

#include <string.h>

#include <rtr/user.h>

/* Register blocks */
#define SYS_PORT_CTRL           0x0004U
#define SYS_RBUF_FLUSH_CTRL     0x0008U
#define EXT_RGMII_OOB_CTRL      0x008CU
#define RBUF_CTRL               0x0300U
#define RBUF_TBUF_SIZE_CTRL     0x03B4U
#define TBUF_CTRL               0x0600U
#define UMAC_CMD                0x0808U
#define UMAC_MAC0               0x080CU
#define UMAC_MAC1               0x0810U
#define UMAC_MAX_FRAME_LEN      0x0814U
#define UMAC_TX_FLUSH           0x0B34U
#define UMAC_MIB_CTRL           0x0D80U
#define UMAC_MDIO_CMD           0x0E14U

#define PORT_MODE_EXT_GPHY      3U
#define RGMII_LINK              (1U << 4U)
#define OOB_DISABLE             (1U << 5U)
#define RGMII_MODE_EN           (1U << 6U)
#define ID_MODE_DIS             (1U << 16U)
#define BUF_64B_EN              (1U << 0U)
#define RBUF_ALIGN_2B           (1U << 1U)

#define CMD_TX_EN               (1U << 0U)
#define CMD_RX_EN               (1U << 1U)
#define CMD_SPEED_SHIFT         2U
#define CMD_SPEED_MASK          (3U << CMD_SPEED_SHIFT)
#define CMD_SW_RESET            (1U << 13U)
#define CMD_LCL_LOOP_EN         (1U << 15U)
#define MIB_RESET_ALL           7U
#define MAX_FRAME_LEN           1536U

#define MDIO_START_BUSY         (1U << 29U)
#define MDIO_READ_FAIL          (1U << 28U)
#define MDIO_READ               (2U << 26U)
#define MDIO_WRITE              (1U << 26U)
#define MDIO_PHY_SHIFT          21U
#define MDIO_REG_SHIFT          16U

/* DMA descriptors and rings */
#define DESC_COUNT              256U
#define DESC_WORDS              3U
#define DESC_SIZE               12U
#define DESC_LENGTH_STATUS      0x00U
#define DESC_ADDRESS_LO         0x04U
#define DESC_ADDRESS_HI         0x08U

#define RDMA_DESC_BASE          0x2000U
#define TDMA_DESC_BASE          0x4000U
#define RDMA_RING_BASE          0x2C00U
#define TDMA_RING_BASE          0x4C00U
#define RING_SIZE               0x40U
#define RDMA_COMMON_BASE        0x3040U
#define TDMA_COMMON_BASE        0x5040U

/* Ring registers. Producer and consumer swap places between receive and transmit. */
#define RING_POINTER_A          0x00U   /* RX: write pointer; TX: read pointer */
#define RING_INDEX_A            0x08U   /* RX: producer (hardware); TX: consumer (hardware) */
#define RING_INDEX_B            0x0CU   /* RX: consumer (driver); TX: producer (driver) */
#define RING_BUF_SIZE           0x10U
#define RING_START_ADDR         0x14U
#define RING_END_ADDR           0x1CU
#define RING_MBUF_DONE_THRESH   0x24U
#define RING_XON_XOFF_THRESH    0x28U   /* TX: flow period */
#define RING_POINTER_B          0x2CU   /* RX: read pointer; TX: write pointer */

#define DMA_RING_CFG            0x00U
#define DMA_CTRL                0x04U
#define DMA_SCB_BURST_SIZE      0x0CU
#define DMA_EN                  (1U << 0U)
#define DMA_MAX_BURST           8U

#define DMA_BUFLENGTH_SHIFT     16U
#define DMA_BUFLENGTH_MASK      0x0FFFU
#define DMA_OWN                 0x8000U
#define DMA_EOP                 0x4000U
#define DMA_SOP                 0x2000U
#define DMA_TX_APPEND_CRC       0x0040U
#define DMA_TX_QTAG_ALL         (0x3FU << 7U)
#define INDEX_MASK              0xFFFFU

/* Ring layout */
#define RING_DEFAULT            16U
#define RX_RINGS                2U
#define RX_RING_DESCS           128U    /* per receive ring */
#define TX_RING_DESCS           64U
#define BUFFER_SIZE             2048U
#define STATUS_BLOCK            64U
#define RX_PREFIX               (STATUS_BLOCK + 2U)
#define FRAME_MIN               60U

/* PHY */
#define PHY_ADDRESS             1U
#define MII_BMCR                0x00U
#define MII_BMSR                0x01U
#define MII_ADVERTISE           0x04U
#define MII_LPA                 0x05U
#define MII_STAT1000            0x0AU
#define MII_AUXCTL              0x18U
#define MII_SHADOW              0x1CU
#define BMCR_ANRESTART          0x0200U
#define BMCR_ANENABLE           0x1000U
#define BMSR_LINK               0x0004U
#define BMSR_ANCOMPLETE         0x0020U
#define LPA_100                 0x0180U
#define STAT1000_PARTNER        0x0C00U
#define AUXCTL_SHADOW_MISC      7U
#define AUXCTL_MISC_WRITE_EN    0x8000U
#define AUXCTL_MISC_RX_SKEW     0x0100U
#define SHADOW_CLOCK_CTRL       (3U << 10U)
#define SHADOW_WRITE_EN         0x8000U
#define SHADOW_GTXCLK_EN        0x0200U

#define MDIO_TIMEOUT_US         2000U

struct rx_ring {
    uint32_t ring;                      /* ring number in the hardware */
    uint32_t first;                     /* first descriptor */
    uint32_t consumer;                  /* index of the next frame to read */
};

static volatile uint32_t *regs;
static uint8_t *dma_base;
static uint64_t dma_base_physical;

static struct rx_ring rx_rings[RX_RINGS] = {
    { .ring = 0U, .first = 0U },
    { .ring = RING_DEFAULT, .first = RX_RING_DESCS },
};
static struct rx_ring *rx_pending_ring;     /* ring of the frame handed over and not yet released */
static uint32_t tx_producer;

static uint8_t mac[GENET_MAC_SIZE];
static bool link_up;
static uint32_t link_speed;
static struct genet_counters counters;

static uint32_t reg_read(uint32_t offset)
{
    return regs[offset / 4U];
}

static void reg_write(uint32_t offset, uint32_t value)
{
    regs[offset / 4U] = value;
}

static void delay_us(uint64_t micros)
{
    uint64_t end = rtr_counter() + ((micros * rtr_counter_hz()) / 1000000U) + 1U;

    while (rtr_counter() < end) {
    }
}

/* Receive buffers take the start of the DMA region; transmit buffers come after. */
static uint32_t rx_buffer_offset(uint32_t descriptor)
{
    return descriptor * BUFFER_SIZE;
}

static uint32_t tx_buffer_offset(uint32_t descriptor)
{
    return (DESC_COUNT + descriptor) * BUFFER_SIZE;
}

static bool mdio_wait(void)
{
    uint64_t end = rtr_counter() + ((MDIO_TIMEOUT_US * rtr_counter_hz()) / 1000000U) + 1U;

    while ((reg_read(UMAC_MDIO_CMD) & MDIO_START_BUSY) != 0U) {
        if (rtr_counter() > end) {
            return false;
        }
    }
    return true;
}

static bool mdio_read(uint32_t reg, uint16_t *value)
{
    uint32_t result;

    reg_write(UMAC_MDIO_CMD, MDIO_START_BUSY | MDIO_READ |
                             (PHY_ADDRESS << MDIO_PHY_SHIFT) | (reg << MDIO_REG_SHIFT));
    if (!mdio_wait()) {
        return false;
    }
    result = reg_read(UMAC_MDIO_CMD);
    if ((result & MDIO_READ_FAIL) != 0U) {
        return false;
    }
    *value = (uint16_t)(result & 0xFFFFU);
    return true;
}

static bool mdio_write(uint32_t reg, uint16_t value)
{
    reg_write(UMAC_MDIO_CMD, MDIO_START_BUSY | MDIO_WRITE |
                             (PHY_ADDRESS << MDIO_PHY_SHIFT) | (reg << MDIO_REG_SHIFT) | value);
    return mdio_wait();
}

static void umac_reset(void)
{
    reg_write(SYS_RBUF_FLUSH_CTRL, reg_read(SYS_RBUF_FLUSH_CTRL) | 2U);
    delay_us(10U);
    reg_write(SYS_RBUF_FLUSH_CTRL, reg_read(SYS_RBUF_FLUSH_CTRL) & ~2U);
    delay_us(10U);
    reg_write(SYS_RBUF_FLUSH_CTRL, 0U);
    delay_us(10U);

    reg_write(UMAC_CMD, 0U);
    reg_write(UMAC_CMD, CMD_SW_RESET | CMD_LCL_LOOP_EN);
    delay_us(2U);
    reg_write(UMAC_CMD, 0U);

    reg_write(UMAC_MIB_CTRL, MIB_RESET_ALL);
    reg_write(UMAC_MIB_CTRL, 0U);
    reg_write(UMAC_MAX_FRAME_LEN, MAX_FRAME_LEN);

    reg_write(RBUF_CTRL, reg_read(RBUF_CTRL) | RBUF_ALIGN_2B | BUF_64B_EN);
    reg_write(RBUF_TBUF_SIZE_CTRL, 1U);
    reg_write(TBUF_CTRL, reg_read(TBUF_CTRL) | BUF_64B_EN);

    reg_write(SYS_PORT_CTRL, PORT_MODE_EXT_GPHY);
}

static void mac_load(void)
{
    uint32_t high = reg_read(UMAC_MAC0);
    uint32_t low = reg_read(UMAC_MAC1);

    /* If the firmware left no address in the controller, use a locally administered one. */
    if ((high == 0U) && (low == 0U)) {
        high = 0x02525452U;             /* 02:52:54:52:4F:53 */
        low = 0x4F53U;
    }
    mac[0] = (uint8_t)(high >> 24U);
    mac[1] = (uint8_t)(high >> 16U);
    mac[2] = (uint8_t)(high >> 8U);
    mac[3] = (uint8_t)high;
    mac[4] = (uint8_t)(low >> 8U);
    mac[5] = (uint8_t)low;
}

static void mac_store(void)
{
    reg_write(UMAC_MAC0, ((uint32_t)mac[0] << 24U) | ((uint32_t)mac[1] << 16U) |
                         ((uint32_t)mac[2] << 8U) | mac[3]);
    reg_write(UMAC_MAC1, ((uint32_t)mac[4] << 8U) | mac[5]);
}

static void dma_disable(void)
{
    reg_write(TDMA_COMMON_BASE + DMA_CTRL, 0U);
    reg_write(RDMA_COMMON_BASE + DMA_CTRL, 0U);
    reg_write(UMAC_TX_FLUSH, 1U);
    delay_us(10U);
    reg_write(UMAC_TX_FLUSH, 0U);
}

static void rx_ring_setup(const struct rx_ring *ring)
{
    uint32_t base = RDMA_RING_BASE + (ring->ring * RING_SIZE);
    uint32_t start = ring->first * DESC_WORDS;

    for (uint32_t i = 0U; i < RX_RING_DESCS; i++) {
        uint32_t descriptor = ring->first + i;
        uint32_t offset = RDMA_DESC_BASE + (descriptor * DESC_SIZE);
        uint64_t physical = dma_base_physical + rx_buffer_offset(descriptor);

        reg_write(offset + DESC_ADDRESS_LO, (uint32_t)physical);
        reg_write(offset + DESC_ADDRESS_HI, (uint32_t)(physical >> 32U));
        reg_write(offset + DESC_LENGTH_STATUS, (BUFFER_SIZE << DMA_BUFLENGTH_SHIFT) | DMA_OWN);
    }

    reg_write(base + RING_START_ADDR, start);
    reg_write(base + RING_POINTER_B, start);
    reg_write(base + RING_POINTER_A, start);
    reg_write(base + RING_END_ADDR, start + (RX_RING_DESCS * DESC_WORDS) - 1U);
    reg_write(base + RING_INDEX_A, 0U);
    reg_write(base + RING_INDEX_B, 0U);
    reg_write(base + RING_BUF_SIZE, (RX_RING_DESCS << 16U) | BUFFER_SIZE);
    reg_write(base + RING_XON_XOFF_THRESH, (5U << 16U) | (RX_RING_DESCS >> 4U));
}

static void rings_setup(void)
{
    uint32_t tx_base = TDMA_RING_BASE + (RING_DEFAULT * RING_SIZE);
    uint32_t rx_bits = 0U;

    for (uint32_t i = 0U; i < RX_RINGS; i++) {
        rx_rings[i].consumer = 0U;
        rx_ring_setup(&rx_rings[i]);
        rx_bits |= 1U << rx_rings[i].ring;
    }
    reg_write(RDMA_COMMON_BASE + DMA_SCB_BURST_SIZE, DMA_MAX_BURST);
    reg_write(RDMA_COMMON_BASE + DMA_RING_CFG, rx_bits);
    reg_write(RDMA_COMMON_BASE + DMA_CTRL, DMA_EN | (rx_bits << 1U));

    tx_producer = 0U;
    reg_write(tx_base + RING_START_ADDR, 0U);
    reg_write(tx_base + RING_POINTER_A, 0U);
    reg_write(tx_base + RING_POINTER_B, 0U);
    reg_write(tx_base + RING_END_ADDR, (TX_RING_DESCS * DESC_WORDS) - 1U);
    reg_write(tx_base + RING_INDEX_A, 0U);
    reg_write(tx_base + RING_INDEX_B, 0U);
    reg_write(tx_base + RING_BUF_SIZE, (TX_RING_DESCS << 16U) | BUFFER_SIZE);
    reg_write(tx_base + RING_MBUF_DONE_THRESH, 1U);
    reg_write(tx_base + RING_XON_XOFF_THRESH, 0U);

    reg_write(TDMA_COMMON_BASE + DMA_SCB_BURST_SIZE, DMA_MAX_BURST);
    reg_write(TDMA_COMMON_BASE + DMA_RING_CFG, 1U << RING_DEFAULT);
    reg_write(TDMA_COMMON_BASE + DMA_CTRL, DMA_EN | (1U << (RING_DEFAULT + 1U)));
}

/*
 * The board wires the PHY in RGMII with the receive clock delay done by the
 * PHY. Sets that up and restarts link negotiation.
 */
static void phy_setup(void)
{
    uint16_t value = 0U;

    if (mdio_write(MII_AUXCTL, (uint16_t)((AUXCTL_SHADOW_MISC << 12U) | AUXCTL_SHADOW_MISC)) &&
        mdio_read(MII_AUXCTL, &value)) {
        value = (uint16_t)(value | AUXCTL_MISC_RX_SKEW | AUXCTL_MISC_WRITE_EN | AUXCTL_SHADOW_MISC);
        (void)mdio_write(MII_AUXCTL, value);
    }

    if (mdio_write(MII_SHADOW, (uint16_t)SHADOW_CLOCK_CTRL) && mdio_read(MII_SHADOW, &value)) {
        value = (uint16_t)((value & ~SHADOW_GTXCLK_EN) | SHADOW_WRITE_EN | SHADOW_CLOCK_CTRL);
        (void)mdio_write(MII_SHADOW, value);
    }

    (void)mdio_write(MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);
}

bool genet_init(uint64_t registers, uint64_t dma, uint64_t dma_physical, uint64_t dma_size)
{
    if ((registers == 0U) || (dma == 0U) || (dma_size < GENET_DMA_SIZE) ||
        ((dma_physical >> 32U) != 0U)) {
        return false;
    }

    regs = (volatile uint32_t *)(uintptr_t)registers;
    dma_base = (uint8_t *)(uintptr_t)dma;
    dma_base_physical = dma_physical;

    mac_load();
    umac_reset();
    mac_store();
    dma_disable();
    rings_setup();
    phy_setup();
    return true;
}

const uint8_t *genet_mac(void)
{
    return mac;
}

static void link_apply(uint32_t speed)
{
    uint32_t speed_bits = (speed == 1000U) ? 2U : ((speed == 100U) ? 1U : 0U);
    uint32_t oob = reg_read(EXT_RGMII_OOB_CTRL);
    uint32_t command = reg_read(UMAC_CMD);

    oob &= ~OOB_DISABLE;
    oob |= RGMII_LINK | RGMII_MODE_EN | ID_MODE_DIS;
    reg_write(EXT_RGMII_OOB_CTRL, oob);

    command &= ~CMD_SPEED_MASK;
    command |= (speed_bits << CMD_SPEED_SHIFT) | CMD_TX_EN | CMD_RX_EN;
    reg_write(UMAC_CMD, command);
}

bool genet_link_poll(bool *changed)
{
    uint16_t status = 0U;
    uint16_t partner = 0U;
    uint16_t gigabit = 0U;
    bool up;

    /* The link bit latches "down" until read: read twice to get the current state. */
    (void)mdio_read(MII_BMSR, &status);
    up = mdio_read(MII_BMSR, &status) &&
         ((status & BMSR_LINK) != 0U) && ((status & BMSR_ANCOMPLETE) != 0U);

    if (changed != NULL) {
        *changed = (up != link_up);
    }

    if (up && !link_up) {
        link_speed = 10U;
        if (mdio_read(MII_STAT1000, &gigabit) && ((gigabit & STAT1000_PARTNER) != 0U)) {
            link_speed = 1000U;
        } else if (mdio_read(MII_LPA, &partner) && ((partner & LPA_100) != 0U)) {
            link_speed = 100U;
        } else {
            /* 10 Mbps */
        }
        link_apply(link_speed);
    } else if (!up && link_up) {
        reg_write(UMAC_CMD, reg_read(UMAC_CMD) & ~(CMD_TX_EN | CMD_RX_EN));
        link_speed = 0U;
    } else {
        /* no change */
    }

    link_up = up;
    return up;
}

uint32_t genet_link_speed(void)
{
    return link_speed;
}

bool genet_receive(const uint8_t **frame, size_t *size)
{
    if ((frame == NULL) || (size == NULL) || (rx_pending_ring != NULL)) {
        return false;
    }

    for (uint32_t i = 0U; i < RX_RINGS; i++) {
        struct rx_ring *ring = &rx_rings[i];
        uint32_t base = RDMA_RING_BASE + (ring->ring * RING_SIZE);
        uint32_t producer = reg_read(base + RING_INDEX_A) & INDEX_MASK;

        /* At most one whole ring of bad frames is discarded per call. */
        for (uint32_t tries = 0U; (tries < RX_RING_DESCS) && (ring->consumer != producer); tries++) {
            uint32_t descriptor = ring->first + (ring->consumer % RX_RING_DESCS);
            const uint8_t *buffer = &dma_base[rx_buffer_offset(descriptor)];
            uint32_t status;
            uint32_t length;

            (void)memcpy(&status, buffer, sizeof(status));
            length = (status >> DMA_BUFLENGTH_SHIFT) & DMA_BUFLENGTH_MASK;

            if (((status & (DMA_SOP | DMA_EOP)) == (DMA_SOP | DMA_EOP)) &&
                (length > RX_PREFIX) && (length <= BUFFER_SIZE)) {
                *frame = &buffer[RX_PREFIX];
                *size = length - RX_PREFIX;
                rx_pending_ring = ring;
                counters.rx_frames++;
                counters.rx_bytes += *size;
                return true;
            }

            /* Truncated or fragmented frame: discard and move on. */
            counters.rx_errors++;
            ring->consumer = (ring->consumer + 1U) & INDEX_MASK;
            reg_write(base + RING_INDEX_B, ring->consumer);
        }
    }
    return false;
}

void genet_receive_done(void)
{
    if (rx_pending_ring != NULL) {
        struct rx_ring *ring = rx_pending_ring;
        uint32_t base = RDMA_RING_BASE + (ring->ring * RING_SIZE);

        ring->consumer = (ring->consumer + 1U) & INDEX_MASK;
        reg_write(base + RING_INDEX_B, ring->consumer);
        rx_pending_ring = NULL;
    }
}

static bool tx_has_room(void)
{
    uint32_t base = TDMA_RING_BASE + (RING_DEFAULT * RING_SIZE);
    uint32_t consumer = reg_read(base + RING_INDEX_A) & INDEX_MASK;

    return ((tx_producer - consumer) & INDEX_MASK) < TX_RING_DESCS;
}

uint8_t *genet_send_begin(void)
{
    uint32_t descriptor = tx_producer % TX_RING_DESCS;

    if (!link_up || !tx_has_room()) {
        counters.tx_dropped++;
        return NULL;
    }
    return &dma_base[tx_buffer_offset(descriptor) + STATUS_BLOCK];
}

bool genet_send_commit(size_t size)
{
    uint32_t base = TDMA_RING_BASE + (RING_DEFAULT * RING_SIZE);
    uint32_t descriptor = tx_producer % TX_RING_DESCS;
    uint32_t offset = TDMA_DESC_BASE + (descriptor * DESC_SIZE);
    uint8_t *buffer = &dma_base[tx_buffer_offset(descriptor)];
    uint64_t physical = dma_base_physical + tx_buffer_offset(descriptor);
    uint32_t length;

    if ((size == 0U) || (size > GENET_FRAME_MAX)) {
        counters.tx_dropped++;
        return false;
    }

    /* A frame shorter than the Ethernet minimum is padded with zeros. */
    if (size < FRAME_MIN) {
        (void)memset(&buffer[STATUS_BLOCK + size], 0, FRAME_MIN - size);
        size = FRAME_MIN;
    }
    (void)memset(buffer, 0, STATUS_BLOCK);
    length = (uint32_t)size + STATUS_BLOCK;

    reg_write(offset + DESC_ADDRESS_LO, (uint32_t)physical);
    reg_write(offset + DESC_ADDRESS_HI, (uint32_t)(physical >> 32U));
    reg_write(offset + DESC_LENGTH_STATUS, (length << DMA_BUFLENGTH_SHIFT) | DMA_TX_QTAG_ALL |
                                           DMA_TX_APPEND_CRC | DMA_SOP | DMA_EOP);

    tx_producer = (tx_producer + 1U) & INDEX_MASK;
    reg_write(base + RING_INDEX_B, tx_producer);

    counters.tx_frames++;
    counters.tx_bytes += size;
    return true;
}

bool genet_send(const uint8_t *frame, size_t size)
{
    uint8_t *buffer;

    if ((frame == NULL) || (size == 0U) || (size > GENET_FRAME_MAX)) {
        return false;
    }
    buffer = genet_send_begin();
    if (buffer == NULL) {
        return false;
    }
    (void)memcpy(buffer, frame, size);
    return genet_send_commit(size);
}

const struct genet_counters *genet_counters(void)
{
    return &counters;
}
