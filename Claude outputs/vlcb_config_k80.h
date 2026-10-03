/**
 * @file
 * @brief Template configuration words for a VLCB module on a PIC18F25K80/26K80 (K80 family).
 *
 * @details
 * The library itself sets no configuration words. Every application includes this file
 * once, from one of its own source files (for example main.c):
 * @code
 * #include "vlcb_config_k80.h"
 * @endcode
 * The values are those of the CBUS_PIC_Bootloader for the K80 (hwsettings.c), so a normal
 * application needs no changes.

 * @par Rules
 * - Include exactly one template, from one source file, and no other \#pragma config
 *   lines.
 * - Give complete configuration words. XC8 fills any bit of a word that is not specified
 *   with the device default, so a partial copy silently changes bits you did not mention.
 * - XINST must stay OFF: XC8 C code (application and bootloader) needs it off.
 * - Keep the values identical to the bootloader's (CBUS_PIC_Bootloader hwsettings.c) and
 *   change the two together. In a combined bootloader + application image both sets land
 *   at 0x300000, and hexmate, which MPLAB X uses to merge loadables, stops with an error if
 *   two inputs hold different data at the same address (Hexmate User's Guide, DS50003033,
 *   section 5.2). Identical data merges without complaint.
 *
 * @par Without a template
 * An application with no \#pragma config puts no configuration words in its hex only if
 * "Program the device with default config words" (-mdefault-config-bits) is off. With it on,
 * XC8 writes the device defaults, which then conflict with the bootloader in a combined
 * image.
 *
 * @par How the words reach the module
 * - PICkit: from the hex that is programmed, normally the combined bootloader + application
 *   image.
 * - CAN download (FCU or MMC): on the K80 WRTC is OFF and the bootloader writes the
 *   configuration bytes it receives, so the application's words ARE written to the module
 *   when the CONFIG box is ticked, and left alone when it is not.
 *
 * @par Changing a bit for one application
 * Only bits the bootloader does not depend on (for example MSSPMSK) may differ from the
 * bootloader. Use a copy of this file in the application, then either:
 * - download the application with the CONFIG box ticked (at least the first time, and again
 *   whenever a different application has been loaded since), or
 * - for a PICkit image, merge with hexmate's override prefix (section 5.2) restricted to the
 *   configuration range (section 5.1), so that any other overlap with the bootloader is
 *   still reported. For example (check the exact syntax against your hexmate version):
 * @code
 * hexmate bootloader.hex +r300000-30000D,application.hex -ocombined.hex
 * @endcode
 * A change the bootloader itself needs belongs in hwsettings.c and this file together.
 */
#ifndef VLCB_CONFIG_K80_H
#define VLCB_CONFIG_K80_H
#if !defined(_18F66K80_FAMILY_)
#error "vlcb_config_k80.h is for the PIC18F25K80/26K80 family"
#endif

// CONFIG1L
#pragma config RETEN =     OFF      // VREG Sleep Enable bit (Ultra low-power regulator is Disabled (Controlled by REGSLP bit))
#pragma config INTOSCSEL = HIGH // LF-INTOSC Low-power Enable bit (LF-INTOSC in High-power mode during Sleep)
#pragma config SOSCSEL =   DIG    // SOSC Power Selection and mode Configuration bits (Digital (SCLKI) mode)
#pragma config XINST =     OFF      // Extended Instruction Set (Disabled)

// CONFIG1H
#pragma config FOSC =      HS1       // Oscillator (HS oscillator (Medium power, 4 MHz - 16 MHz))
#pragma config PLLCFG =    OFF      // PLL x4 Enable bit (Disabled)
#pragma config FCMEN =     OFF      // Fail-Safe Clock Monitor (Disabled)
#pragma config IESO =      OFF       // Internal External Oscillator Switch Over Mode (Disabled)

// CONFIG2L
#pragma config PWRTEN =    ON      // Power Up Timer (Enabled)
#pragma config BOREN =     SBORDIS      // Brown Out Detect (Disabled in hardware, SBOREN disabled)
#pragma config BORV =      0         // Brown-out Reset Voltage bits (3.0V)
#pragma config BORPWR =    ZPBORMV // BORMV Power level (ZPBORMV instead of BORMV is selected)

// CONFIG2H
#pragma config WDTEN =     OFF      // Watchdog Timer (WDT disabled in hardware; SWDTEN bit disabled)
#pragma config WDTPS =     1048576      // Watchdog Postscaler (1:1048576)

// CONFIG3H
#pragma config CANMX =     PORTB    // ECAN Mux bit (ECAN TX and RX pins are located on RB2 and RB3, respectively)
#pragma config MSSPMSK =   MSK7   // MSSP address masking (7 Bit address masking mode)
#pragma config MCLRE =     ON       // Master Clear Enable (MCLR Enabled, RE3 Disabled)

// CONFIG4L
#pragma config STVREN =    ON      // Stack Overflow Reset (Enabled)
#pragma config BBSIZ =     BB1K     // Boot Block Size (1K word Boot Block size)

// CONFIG5L
#pragma config CP0 =       OFF        // Code Protect 00800-01FFF (Disabled)
#pragma config CP1 =       OFF        // Code Protect 02000-03FFF (Disabled)
#pragma config CP2 =       OFF        // Code Protect 04000-05FFF (Disabled)
#pragma config CP3 =       OFF        // Code Protect 06000-07FFF (Disabled)

// CONFIG5H
#pragma config CPB =       OFF        // Code Protect Boot (Disabled)
#pragma config CPD =       OFF        // Data EE Read Protect (Disabled)

// CONFIG6L
#pragma config WRT0 =      OFF       // Table Write Protect 00800-01FFF (Disabled)
#pragma config WRT1 =      OFF       // Table Write Protect 02000-03FFF (Disabled)
#pragma config WRT2 =      OFF       // Table Write Protect 04000-05FFF (Disabled)
#pragma config WRT3 =      OFF       // Table Write Protect 06000-07FFF (Disabled)

// CONFIG6H
#pragma config WRTC =      OFF       // Config. Write Protect (Disabled)
#pragma config WRTB =      OFF       // Table Write Protect Boot (Disabled)
#pragma config WRTD =      OFF       // Data EE Write Protect (Disabled)

// CONFIG7L
#pragma config EBTR0 =     OFF      // Table Read Protect 00800-01FFF (Disabled)
#pragma config EBTR1 =     OFF      // Table Read Protect 02000-03FFF (Disabled)
#pragma config EBTR2 =     OFF      // Table Read Protect 04000-05FFF (Disabled)
#pragma config EBTR3 =     OFF      // Table Read Protect 06000-07FFF (Disabled)

// CONFIG7H
#pragma config EBTRB =     OFF      // Table Read Protect Boot (Disabled)

#endif /* VLCB_CONFIG_K80_H */
