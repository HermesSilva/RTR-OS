/*
 * RTR-OS - driver for the Raspberry Pi 4 Ethernet controller (GENET v5 with a BCM54213PE PHY).
 *
 * Runs inside the process, by periodic polling: it does not use interrupts.
 */
#ifndef NETWEB_GENET_H
#define NETWEB_GENET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GENET_MAC_SIZE      6U
#define GENET_FRAME_MAX     1518U       /* Ethernet frame without the CRC */

struct genet_counters {
    uint64_t rx_frames;
    uint64_t rx_bytes;
    uint64_t rx_errors;
    uint64_t tx_frames;
    uint64_t tx_bytes;
    uint64_t tx_dropped;                /* dropped because the transmit ring was full */
};

/*
 * Initializes the controller. `registers` is the address of the registers in
 * the process space; `dma` and `dma_physical` are the address and physical
 * address of an uncached memory region of at least GENET_DMA_SIZE bytes.
 */
#define GENET_DMA_SIZE      0x100000UL

bool genet_init(uint64_t registers, uint64_t dma, uint64_t dma_physical, uint64_t dma_size);

const uint8_t *genet_mac(void);

/* Polls the PHY. Returns true if the link is up; `changed` tells whether it changed since the last poll. */
bool genet_link_poll(bool *changed);
uint32_t genet_link_speed(void);        /* Mbps, 0 while the link is down */

/*
 * Hands over the next received frame, if any. `frame` points into the
 * driver memory and is valid until genet_receive_done is called.
 */
bool genet_receive(const uint8_t **frame, size_t *size);
void genet_receive_done(void);

/* Sends a frame, copying it into the driver memory. Returns false if there is no room. */
bool genet_send(const uint8_t *frame, size_t size);

/* To build the frame directly in the driver memory: ask for the space, write, then commit. */
uint8_t *genet_send_begin(void);
bool genet_send_commit(size_t size);

const struct genet_counters *genet_counters(void);

#endif
