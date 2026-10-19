/* Host spellings of the XC8 extensions the library uses - test only. */
#ifndef PIC_HOST_H
#define PIC_HOST_H
#include <stdint.h>
#include <stddef.h>
typedef uint32_t uint24_t;
typedef int32_t int24_t;
#define __reentrant
#define __at(x)
#define __section(x)
#define __persistent
#define __far
#define __near
#define __rom
#define __ram
#define __interrupt(...)
#define irq(x)
#define base(x)
#define low_priority
#define high_priority
#define NOP()          do { } while (0)
#define RESET()        do { } while (0)
#define CLRWDT()       do { } while (0)
#define __delay_us(n)  do { } while (0)
#define __delay_ms(n)  do { } while (0)
#define di()           do { } while (0)
#define ei()           do { } while (0)
typedef unsigned char __bit;
#ifndef PIC_HOST_NO_ASM
#define asm(...)
#define __asm(...)
#endif
#define ___mkstr(x) ""
#endif
