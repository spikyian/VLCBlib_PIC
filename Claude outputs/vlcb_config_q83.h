/**
 * @file
 * @brief Template configuration words for a VLCB module on a PIC18F27/47/57Q83.
 *
 * @details
 * The library itself sets no configuration words. Every application includes this file
 * once, from one of its own source files (for example main.c):
 * @code
 * #include "vlcb_config_q83.h"
 * @endcode
 * The values are those of the CBUS_PIC_Bootloader for the Q83 (hwsettings.c), so a normal
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
 * - CAN download (FCU or MMC): never on the Q83. The bootloader sets WRTC = ON, which
 *   write-protects the configuration words, so they stay as programmed with the PICkit even
 *   when the CONFIG box is ticked.
 *
 * @par Changing a bit for one application
 * Only bits the bootloader does not depend on (for example CLKOUTEN or ZCD) may differ from
 * the bootloader. Because a download cannot change them on the Q83, such an application must
 * be programmed with a PICkit, from a combined image in which the application's
 * configuration words take priority. Use a copy of this file in the application, and merge
 * with hexmate's override prefix (section 5.2) restricted to the configuration range
 * (section 5.1), so that any other overlap with the bootloader is still reported. For
 * example (check the exact syntax against your hexmate version):
 * @code
 * hexmate bootloader.hex +r300000-300023,application.hex -ocombined.hex
 * @endcode
 * A change the bootloader itself needs belongs in hwsettings.c and this file together.
 */
#ifndef VLCB_CONFIG_Q83_H
#define VLCB_CONFIG_Q83_H
#if !defined(_18FXXQ83_FAMILY_)
#error "vlcb_config_q83.h is for the PIC18FxxQ83 family"
#endif


//CONFIG1
#pragma config FEXTOSC = HS     // External Oscillator Selection->HS (crystal oscillator) above 4 MHz (datasheet FEXTOSC table)
//#pragma config RSTOSC = HFINTOSC_64MHZ     // Reset Oscillator Selection->HFINTOSC with HFFRQ = 64 MHz and CDIV = 1:1
#pragma config RSTOSC = EXTOSC     // External oscillator as per FEXTOSC

//CONFIG2
#pragma config CLKOUTEN = OFF     // Clock out Enable bit->CLKOUT function is disabled
#pragma config PR1WAY =  ON     // PRLOCKED One-Way Set Enable bit->PRLOCKED bit can be cleared and set only once
#pragma config CSWEN =   ON     // Clock Switch Enable bit->Writing to NOSC and NDIV is allowed
#pragma config JTAGEN =  OFF     // JTAG Enable bit->Disable JTAG Boundary Scan mode, JTAG pins revert to user functions
#pragma config FCMEN =   ON     // Fail-Safe Clock Monitor Enable bit->Fail-Safe Clock Monitor enabled
#pragma config FCMENP =  ON     // Fail-Safe Clock Monitor -Primary XTAL Enable bit->FSCM timer will set FSCMP bit and OSFIF interrupt on Primary XTAL failure
#pragma config FCMENS =  ON     // Fail-Safe Clock Monitor -Secondary XTAL Enable bit->FSCM timer will set FSCMS bit and OSFIF interrupt on Secondary XTAL failure

//CONFIG3
#pragma config MCLRE =   EXTMCLR     // MCLR Enable bit->If LVP = 0, MCLR pin is MCLR; If LVP = 1, RE3 pin function is MCLR
#pragma config PWRTS =   PWRT_64     // Power-up timer selection bits->PWRT is disabled
#pragma config MVECEN =  ON     // Multi-vector enable bit->Interrupt contoller uses vector table to prioritze interrupts
#pragma config IVT1WAY = ON     // IVTLOCK bit One-way set enable bit->IVTLOCKED bit can be cleared and set only once
#pragma config LPBOREN = OFF     // Low Power BOR Enable bit->Low-Power BOR disabled
#pragma config BOREN =   SBORDIS     // Brown-out Reset Enable bits->Brown-out Reset enabled , SBOREN bit is ignored

//CONFIG4
#pragma config BORV =    VBOR_2P7     // Brown-out Reset Voltage Selection bits->Brown-out Reset Voltage (VBOR) set to 2.7V
#pragma config ZCD =     OFF     // ZCD Disable bit->ZCD module is disabled. ZCD can be enabled by setting the ZCDSEN bit of ZCDCON
#pragma config PPS1WAY = ON     // PPSLOCK bit One-Way Set Enable bit->PPSLOCKED bit can be cleared and set only once; PPS registers remain locked after one clear/set cycle
#pragma config STVREN =  ON     // Stack Full/Underflow Reset Enable bit->Stack full/underflow will cause Reset
#pragma config LVP =     ON     // Low Voltage Programming Enable bit->Low voltage programming enabled. MCLR/VPP pin function is MCLR. MCLRE configuration bit is ignored
#pragma config XINST =   OFF     // Extended Instruction Set Enable bit->Extended Instruction Set and Indexed Addressing Mode disabled

//CONFIG5
#pragma config WDTCPS =  WDTCPS_31     // WDT Period selection bits->Divider ratio 1:65536; software control of WDTPS
#pragma config WDTE =    SWDTEN  // WDT operating mode->enabled/disabled by WDTCON0.SEN (resets to 0 = off), as the bootloader

//CONFIG6
#pragma config WDTCWS =  WDTCWS_7     // WDT Window Select bits->window always open (100%); software control; keyed access not required
#pragma config WDTCCS =  SC     // WDT input clock selector->Software Control

//CONFIG7
#pragma config BBSIZE =  BBSIZE_1024    // Boot Block Size selection bits->1024 words = 0x000-0x7FF, the whole bootloader
#pragma config BBEN =    ON     // Boot Block enable bit->Boot block enabled
#pragma config SAFEN =   OFF     // Storage Area Flash enable bit->SAF disabled

//CONFIG8
#pragma config WRTB =    ON     // Boot Block Write Protection bit->Boot Block Write protected
#pragma config WRTC =    ON     // Configuration Register Write Protection bit->Configuration registers Write protected
#pragma config WRTD =    OFF     // Data EEPROM Write Protection bit->Data EEPROM not Write protected
#pragma config WRTSAF =  OFF     // SAF Write protection bit->SAF not Write Protected
#pragma config WRTAPP =  OFF     // Application Block write protection bit->Application Block not write protected

//CONFIG9
#pragma config BOOTPINSEL = RC5     // CRC on boot output pin selection->CRC on boot output pin is RC5
#pragma config BPEN =    OFF     // CRC on boot output pin enable bit->CRC on boot output pin disabled
#pragma config ODCON =   OFF     // CRC on boot output pin open drain bit->Pin drives both high-going and low-going signals

//CONFIG10
#pragma config CP =      OFF     // PFM and Data EEPROM Code Protection bit->PFM and Data EEPROM code protection disabled

//CONFIG11
#pragma config BOOTSCEN = OFF     // CRC on boot scan enable for boot area->CRC on boot will not include the boot area of program memory in its calculation
#pragma config BOOTCOE = HALT     // CRC on boot Continue on Error for boot areas bit->CRC on boot will stop device if error is detected in boot areas
#pragma config APPSCEN = OFF     // CRC on boot application code scan enable->CRC on boot will not include the application area of program memory in its calculation
#pragma config SAFSCEN = OFF     // CRC on boot SAF area scan enable->CRC on boot will not include the SAF area of program memory in its calculation
#pragma config DATASCEN = OFF     // CRC on boot Data EEPROM scan enable->CRC on boot will not include data EEPROM in its calculation
#pragma config CFGSCEN = OFF     // CRC on boot Config fuses scan enable->CRC on boot will not include the configuration fuses in its calculation
#pragma config COE = HALT     // CRC on boot Continue on Error for non-boot areas bit->CRC on boot will stop device if error is detected in non-boot areas
#pragma config BOOTPOR = OFF     // Boot on CRC Enable bit->CRC on boot will not run

//CONFIG12
#pragma config BCRCPOLT = hFF     // Boot Sector Polynomial for CRC on boot bits 31-24->Bits 31:24 of BCRCPOL are 0xFF

//CONFIG13
#pragma config BCRCPOLU = hFF     // Boot Sector Polynomial for CRC on boot bits 23-16->Bits 23:16 of BCRCPOL are 0xFF

//CONFIG14
#pragma config BCRCPOLH = hFF     // Boot Sector Polynomial for CRC on boot bits 15-8->Bits 15:8 of BCRCPOL are 0xFF

//CONFIG15
#pragma config BCRCPOLL = hFF     // Boot Sector Polynomial for CRC on boot bits 7-0->Bits 7:0 of BCRCPOL are 0xFF

//CONFIG16
#pragma config BCRCSEEDT = hFF     // Boot Sector Seed for CRC on boot bits 31-24->Bits 31:24 of BCRCSEED are 0xFF

//CONFIG17
#pragma config BCRCSEEDU = hFF     // Boot Sector Seed for CRC on boot bits 23-16->Bits 23:16 of BCRCSEED are 0xFF

//CONFIG18
#pragma config BCRCSEEDH = hFF     // Boot Sector Seed for CRC on boot bits 15-8->Bits 15:8 of BCRCSEED are 0xFF

//CONFIG19
#pragma config BCRCSEEDL = hFF     // Boot Sector Seed for CRC on boot bits 7-0->Bits 7:0 of BCRCSEED are 0xFF

//CONFIG20
#pragma config BCRCEREST = hFF     // Boot Sector Expected Result for CRC on boot bits 31-24->Bits 31:24 of BCRCERES are 0xFF

//CONFIG21
#pragma config BCRCERESU = hFF     // Boot Sector Expected Result for CRC on boot bits 23-16->Bits 23:16 of BCRCERES are 0xFF

//CONFIG22
#pragma config BCRCERESH = hFF     // Boot Sector Expected Result for CRC on boot bits 15-8->Bits 15:8 of BCRCERES are 0xFF

//CONFIG23
#pragma config BCRCERESL = hFF     // Boot Sector Expected Result for CRC on boot bits 7-0->Bits 7:0 of BCRCERES are 0xFF

//CONFIG24
#pragma config CRCPOLT = hFF     // Non-Boot Sector Polynomial for CRC on boot bits 31-24->Bits 31:24 of CRCPOL are 0xFF

//CONFIG25
#pragma config CRCPOLU = hFF     // Non-Boot Sector Polynomial for CRC on boot bits 23-16->Bits 23:16 of CRCPOL are 0xFF

//CONFIG26
#pragma config CRCPOLH = hFF     // Non-Boot Sector Polynomial for CRC on boot bits 15-8->Bits 15:8 of CRCPOL are 0xFF

//CONFIG27
#pragma config CRCPOLL = hFF     // Non-Boot Sector Polynomial for CRC on boot bits 7-0->Bits 7:0 of CRCPOL are 0xFF

//CONFIG28
#pragma config CRCSEEDT = hFF     // Non-Boot Sector Seed for CRC on boot bits 31-24->Bits 31:24 of CRCSEED are 0xFF

//CONFIG29
#pragma config CRCSEEDU = hFF     // Non-Boot Sector Seed for CRC on boot bits 23-16->Bits 23:16 of CRCSEED are 0xFF

//CONFIG30
#pragma config CRCSEEDH = hFF     // Non-Boot Sector Seed for CRC on boot bits 15-8->Bits 15:8 of CRCSEED are 0xFF

//CONFIG31
#pragma config CRCSEEDL = hFF     // Non-Boot Sector Seed for CRC on boot bits 7-0->Bits 7:0 of CRCSEED are 0xFF

//CONFIG32
#pragma config CRCEREST = hFF     // Non-Boot Sector Expected Result for CRC on boot bits 31-24->Bits 31:24 of CRCERES are 0xFF

//CONFIG33
#pragma config CRCERESU = hFF     // Non-Boot Sector Expected Result for CRC on boot bits 23-16->Bits 23:16 of CRCERES are 0xFF

//CONFIG34
#pragma config CRCERESH = hFF     // Non-Boot Sector Expected Result for CRC on boot bits 15-8->Bits 15:8 of CRCERES are 0xFF

//CONFIG35
#pragma config CRCERESL = hFF     // Non-Boot Sector Expected Result for CRC on boot bits 7-0->Bits 7:0 of CRCERES are 0xFF

#endif /* VLCB_CONFIG_Q83_H */
