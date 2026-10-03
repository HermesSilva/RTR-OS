/* RTR-OS - lwIP adaptation to the compiler and the program environment. */
#ifndef NETWEB_ARCH_CC_H
#define NETWEB_ARCH_CC_H

#include <stdint.h>

#define BYTE_ORDER              LITTLE_ENDIAN

/* The environment has no inttypes.h, ctype.h or unistd.h. */
#define LWIP_NO_INTTYPES_H      1
#define LWIP_NO_CTYPE_H         1
#define LWIP_NO_UNISTD_H        1

#define X8_F                    "02x"
#define U16_F                   "u"
#define S16_F                   "d"
#define X16_F                   "x"
#define U32_F                   "u"
#define S32_F                   "d"
#define X32_F                   "x"
#define SZT_F                   "lu"

void netweb_fatal(const char *message) __attribute__((noreturn));
uint32_t netweb_random(void);

#define LWIP_PLATFORM_DIAG(x)   do { } while (0)
#define LWIP_PLATFORM_ASSERT(x) netweb_fatal(x)
#define LWIP_RAND()             netweb_random()

#endif
