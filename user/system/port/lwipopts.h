/* RTR-OS - lwIP configuration for the network process. */
#ifndef NETWEB_LWIPOPTS_H
#define NETWEB_LWIPOPTS_H

/* No operating system underneath: the stack runs in the process loop, by polling. */
#define NO_SYS                      1
#define SYS_LIGHTWEIGHT_PROT        0
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0

/* All of the stack's memory is static, reserved at startup. */
#define MEM_ALIGNMENT               8
#define MEM_SIZE                    (256 * 1024)
#define MEMP_NUM_PBUF               32
#define MEMP_NUM_TCP_PCB            16
#define MEMP_NUM_TCP_PCB_LISTEN     4
#define MEMP_NUM_TCP_SEG            64
#define MEMP_NUM_SYS_TIMEOUT        16
#define PBUF_POOL_SIZE              64
#define PBUF_POOL_BUFSIZE           1600

#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    0
#define LWIP_UDP                    1
#define LWIP_DHCP                   1
#define LWIP_DNS                    0
#define LWIP_IGMP                   0
#define LWIP_AUTOIP                 0
#define LWIP_ACD                    0
#define LWIP_DHCP_DOES_ACD_CHECK    0

#define LWIP_TCP                    1
#define TCP_MSS                     1460
#define TCP_WND                     (8 * TCP_MSS)
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            32
#define TCP_LISTEN_BACKLOG          1

#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1

#define LWIP_STATS                  0
#define LWIP_PROVIDE_ERRNO          1

#endif
