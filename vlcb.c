/**
 * @copyright Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */
/*
  This work is licensed under the:
      Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
   To view a copy of this license, visit:
      http://creativecommons.org/licenses/by-nc-sa/4.0/
   or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.

   License summary:
    You are free to:
      Share, copy and redistribute the material in any medium or format
      Adapt, remix, transform, and build upon the material

    The licensor cannot revoke these freedoms as long as you follow the license terms.

    Attribution : You must give appropriate credit, provide a link to the license,
                   and indicate if changes were made. You may do so in any reasonable manner,
                   but not in any way that suggests the licensor endorses you or your use.

    NonCommercial : You may not use the material for commercial purposes. **(see note below)

    ShareAlike : If you remix, transform, or build upon the material, you must distribute
                  your contributions under the same license as the original.

    No additional restrictions : You may not apply legal terms or technological measures that
                                  legally restrict others from doing anything the license permits.

   ** For commercial use, please contact the original copyright holder(s) to agree licensing terms

    This software is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE
*/
/**
 * @author Ian Hogg 
 * @date Dec 2022
 * 
 */ 
#include <xc.h>
#include "vlcb.h"
#include "module.h"
#include "nvm.h"
#include "hardware.h"
#include "ticktime.h"
#include "timedResponse.h"
#include "statusLeds.h"
#include "mns.h"

/** @mainpage VLCB
 * *************************************************************************
 * C library for VLCB. Written for XC8.
 * 
 * # The structure of the library
 * <!--
 * This is an ascii art version of the HTML table below.
 * <pre>
 * -----------
 * |   APP   |
 * |     ------------------------------------------------------------------
 * |     |                            VLCB                                |
 * |     |                                                                |
 * |     ----------------------- ------------------------------------------
 * |         |  |   Service Interface      | |    Transport Interface     |
 * |         |  ---------------------------- ------------------------------
 * |         |  | service | | service | |          Transport service      |
 * -----------  |         | |         | |                                 |
 *              ----------- ----------- -----------------------------------
 * </pre>
 * -->
 * 
 * <table style="border-collapse: collapse;">
    <tr>
        <td colspan="2" style="border-top:2px solid; border-left:2px solid;border-right:2px solid;width:5em;background-color:lightblue;">App</td>
        <td colspan="8">&nbsp;</td>
    </tr>
    <tr>
        <td style="border-left:2px solid;background-color:lightblue;">&nbsp;</td>
        <td colspan="9" style="border:2px solid;background-color:lightgreen;text-align: center;">VLCB</td>
    </tr>
    <tr>
        <td style="border-left:2px solid;background-color:lightblue;width:5em;">&nbsp;</td>
        <td style="border:1px solid;border-right:2px solid;background-color:lightblue;">APP callbacks</td>
        <td style="width:2em;">&nbsp;</td>
        <td colspan="4" style="border-left:2px solid;border-top:2px solid;border-bottom:2px solid;background-color:yellow;text-align: center;">Service interface</td>
        <td style="border-right:2px solid;border-top:2px solid;border-bottom:2px solid;width:3em;background-color:yellow;">&nbsp;</td>
        <td style="width:2em;">&nbsp;</td>
        <td style="border:2px solid;background-color:bisque;">Transport interface</td>
    </tr>
    <tr>
        <td style="border-left:2px solid;border-bottom:2px solid;background-color:lightblue;">&nbsp;</td>
        <td style="border-right:2px solid;border-bottom:2px solid;background-color:lightblue;">&nbsp;</td>
        <td>&nbsp;</td>
        <td style="border:2px solid;background-color:mediumOrchid;">Service 1</td>
        <td>&nbsp;</td>
        <td style="border:2px solid;background-color:hotPink;">Service 2</td>
        <td>&nbsp;</td>
        <td colspan="3" style="border:2px solid;background-color:lightCoral;text-align:center;">Transport Service</td>
    </tr>
    <tr>
        <td style="text-align:center;">&uarr;&uarr;&uarr;&uarr;</td>
        <td style="text-align:center;">&darr;&darr;&darr;&darr;</td>
        <td>&nbsp;</td>
        <td>&nbsp;</td>
        <td>&nbsp;</td>
        <td>&nbsp;</td>
        <td>&nbsp;</td>
        <td colspan="3" style="text-align:center;">&uarr;&darr;</td>
    </tr>
    <tr>
        <td colspan="2" style="text-align:center;">Module I/O</td>
        <td>&nbsp;</td>
        <td>&nbsp;</td>
        <td>&nbsp;</td>
        <td>&nbsp;</td>
        <td>&nbsp;</td>
        <td colspan="3" style="text-align:center;">BUS</td>
    </tr>
</table> 
 * 
 * 
 * 
 * All services must support the Service interface.
 * Transport services must also support the Transport interface.
 * The APP makes use of VLCB functionality and also provides functionality to
 * VLCB. 
 * 
 * # Application
 * The application provides the module specific functionality. VLCB provides
 * much of the standard functionality of VLCB modules. Different VLCB capabilities
 * are enabled through the use of VLCB services. For example CAN bus support
 * can be enabled by including the CAN service.
 * 
 * ## Application functionality
 * An VLCB module using this library needs to consider the following 
 * application responsibilities:
 * 
 * ### Inputs
 *    - Monitoring input pins, 
 *    - Perform behaviour based upon module NV settings,
 *    - Save input state according to type of input in RAM
 *    - Send events to the transport layer according to behaviour requirements
 * 
 * ### Outputs
 *    - Receive messages from the transport layer or receive actions from the action queue
 *    - Use the NV settings to determine the behaviour
 *    - Save state into non volatile memory e.g. EEPROM
 *    - Make changes to the output pin state
 * 
 * ### On power on
 *    - Restore output state using the information stored in non volatile memory
 *    - Initialise input state based upon current input pin state
 * 
 * ### Regular poll
 *    - Update and perform time based behaviour
 * 
 * ## Module design
 * The designer of a module needs to:
 *  1. Determine which services the module will use.
 *  2. Write a modules.h which is used by the VLCBlib to control its operation.
 *  3. If the module will use NVs then:
 *      - Define the NV usage and allocation,
 *      - Define the memory allocation (type and address) for the NVs,
 *      - Define the default value (factory reset settings) of the NVs,
 *      - Determine whether any validation of NV settings is required.
 *  4. If the module has a CAN interface:
 *      - Decide where in NVM the CANID is to be stored,
 *      - Decide how much memory can be used for transmit and receive buffers.
 *  5. If the module is to support event teaching:
 *      - Decide the number of events and number of EVs per event,
 *      - Define the event EV usage and allocation,
 *      - Define the memory allocation (type and address) for the EVs,
 *  6. If the module also supports produced events then:
 *      - If the concept of Happenings is to be used then defined the size of the Happening identifier,
 *      - Provide a function to provide the current event state given a Happening
 *  7. If the module also supports consumed events then:
 *      - If Actions concept is to be used then the size of the Action queue should be defined.
 *  8. All modules also need:
 *      - The address and type of NVM where the module's node number is to be stored,
 *      - The address and type of NVM where the mode is to be stored,
 *      - Define the module type name, module ID and version,
 *      - The number of LEDs used to display module state,
 *      - A macro to obtain the push button state,
 *      - A macro to set up the ports for the LEDs and push button.
 * 
 * ## Other information
 *    - The module mode is available using the uint8_t mode global variable.
 *    - The module submodes are available using the uint8_t mode_flags global variable.
 *    - The module's node number is available as Word nn global variable.
 *    - A module should include either statusLeds1.c or statusLeds2.c depending whether it has one or two LEDs.
 *    - A module may define a function to process VLCB messages before being handled by the library.
 *    - A module may also define a function to process VLCB messages if not handled by the library. 
 *   
 * ## Application source
 * The application source file must provide:
 * 1. An array of Service definitions
 *      const Service * const services[]
 *    which must be initialised with pointers to each service definition for the
 *    services required by the application.
 *    For example
 * @code
 * const Service * const services[] = {
 *     &canService,
 *     &mnsService,
 *     &nvService,
 *     &bootService,
 *     &eventTeachService,
 *     &eventConsumerService,
 *     &eventProducerService,
 *     &eventAckService
 * };
 * @endcode
 * 2. A void init(void) function which sets the transport status pointer e.g.
 *     transport = \&canTransport;
 *    This function must also do any additional, application specific initialisation.
 * 3. A void loop(void) function to perform any regular processing required by
 *    the application.
 * 4. Callback functions required by the VLCB library depending upon the services used:
 *    + uint8_t APP_nvDefault(uint8_t index)
 * function to provide a default value for an NV.
 *    + void APP_nvValueChanged(uint8_t index, uint8_t value, uint8_t oldValue) 
 * function called when an NV has its value changed.
 *    + NvValidation APP_nvValidate(uint8_t index, uint8_t value) 
 * function to allow the application to validate a new NV value.
 *    + uint8_t APP_addEvent(uint16_t nodeNumber, uint16_t eventNumber, uint8_t evNum, uint8_t evVal)*
 * function to be provided by the application and is called when an event is added. 
 * Typically this will just call the VLCB library addEvent(uint16_t nodeNumber, 
 * uint16_t eventNumber, uint8_t evNum, uint8_t evVal) function.
 *    + ValidTime APP_isSuitableTimeToWriteFlash(void) 
 * called by the library to check whether the application is currently 
 * performing any time critical operations or whether processing can be 
 * temporarily suspended to write to Flash memory.
 *    + Processed APP_preProcessMessage(Message * m)
 * called before a received message is to be processed by the VLCB library.
 * This allows and processing provided by the library to be replaced by 
 * application specific handling.
 *    + Processed APP_postProcessMessage(Message * m)
 * called after a received message to checked by the VLCB library if the library
 * does not handle the message.
 *    + void APP_factoryReset(void)
 * called when the module is performing a factory reset and allows the 
 * application to set up any non volatile memory required.
 *    + void APP_testMode(void)
 * called by the library if the button was held down at power up to allow the
 * application to perform special test facilities.
 * 
 * ## Module.h
 * The developer of an application must provide a module.h which has 
 * module/application specific definitions which influence the behaviour of VLCB.
 * 
 * Each service documents any requirements it may add for the module.h file.
 * 
 */

/**
 * @file
 * @brief
 * Baseline functionality required by VLCB and entry points into the application.
 * @details
 * Provides the functionality for main() and interrupt processing. 
 * Also contains the functionality to handle the list of services.
 * Contains the PIC CONFIG settings.
 */

#if defined(_18F66K80_FAMILY_) 
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

#endif
#if defined(_18FXXQ83_FAMILY_)
// Configuration bits: selected in the GUI

//CONFIG1
#pragma config FEXTOSC = HS     // External Oscillator Selection->HS (crystal oscillator) above 8 MHz
#pragma config RSTOSC = HFINTOSC_64MHZ     // Reset Oscillator Selection->HFINTOSC with HFFRQ = 64 MHz and CDIV = 1:1

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
#pragma config PWRTS =   PWRT_OFF     // Power-up timer selection bits->PWRT is disabled
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
#pragma config WDTE =    OFF     // WDT operating mode->WDT Disabled; SWDTEN is ignored

//CONFIG6
#pragma config WDTCWS =  WDTCWS_7     // WDT Window Select bits->window always open (100%); software control; keyed access not required
#pragma config WDTCCS =  SC     // WDT input clock selector->Software Control

//CONFIG7
#pragma config BBSIZE =  BBSIZE_512     // Boot Block Size selection bits->Boot Block size is 512 words
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

#endif

/**
 * The list of the priorities for each opcode.
 */
const Priority priorities[256] = {
    pNORMAL,   // OPC_ACK=0x00,
    pNORMAL,   // OPC_NAK=0x01,
    pHIGH,   // OPC_HLT=0x02,
    pABOVE,   // OPC_BON=0x03,
    pABOVE,   // OPC_TOF=0x04,
    pABOVE,   // OPC_TON=0x05,
    pABOVE,   // OPC_ESTOP=0x06,
    pHIGH,   // OPC_ARST=0x07,
    pABOVE,   // OPC_RTOF=0x08,
    pABOVE,   // OPC_RTON=0x09,
    pHIGH,   // OPC_RESTP=0x0A,
    pNORMAL,   // OPC_RSTAT=0x0C,
    pLOW,   // OPC_QNN=0x0D,
    pLOW,   // OPC_RQNP=0x10,
    pNORMAL,   // OPC_RQMN=0x11,
            pNORMAL,    // 0x12
            pNORMAL,    // 0x13
            pNORMAL,    // 0x14
            pNORMAL,    // 0x15
            pNORMAL,    // 0x16
            pNORMAL,    // 0x17
            pNORMAL,    // 0x18
            pNORMAL,    // 0x19
            pNORMAL,    // 0x1A
            pNORMAL,    // 0x1B
            pNORMAL,    // 0x1C
            pNORMAL,    // 0x1D
            pNORMAL,    // 0x1E
            pNORMAL,    // 0x1F
            pNORMAL,    // 0x20
    pNORMAL,   // OPC_KLOC=0x21,
    pNORMAL,   // OPC_QLOC=0x22,
    pNORMAL,   // OPC_DKEEP=0x23,
            pNORMAL,    // 0x24
            pNORMAL,    // 0x25
            pNORMAL,    // 0x26
            pNORMAL,    // 0x27
            pNORMAL,    // 0x28
            pNORMAL,    // 0x29
            pNORMAL,    // 0x2A
            pNORMAL,    // 0x2B
            pNORMAL,    // 0x2C
            pNORMAL,    // 0x2D
            pNORMAL,    // 0x2E
            pNORMAL,    // 0x2F
    pNORMAL,   // OPC_DBG1=0x30,
            pNORMAL,    // 0x31
            pNORMAL,    // 0x32
            pNORMAL,    // 0x33
            pNORMAL,    // 0x34
            pNORMAL,    // 0x35
            pNORMAL,    // 0x36
            pNORMAL,    // 0x37
            pNORMAL,    // 0x38
            pNORMAL,    // 0x39
            pNORMAL,    // 0x3A
            pNORMAL,    // 0x3B
            pNORMAL,    // 0x3C
            pNORMAL,    // 0x3D
            pNORMAL,    // 0x3E
    pNORMAL,    // OPC_EXTC=0x3F
    pNORMAL,   // OPC_RLOC=0x40,
    pNORMAL,   // OPC_QCON=0x41,
    pLOW,   // OPC_SNN=0x42,
    pNORMAL,   // OPC_ALOC=0x43,
    pNORMAL,   // OPC_STMOD=0x44,
    pNORMAL,   // OPC_PCON=0x45,
    pNORMAL,   // OPC_KCON=0x46,
    pNORMAL,   // OPC_DSPD=0x47,
    pNORMAL,   // OPC_DFLG=0x48,
    pNORMAL,   // OPC_DFNON=0x49,
    pNORMAL,   // OPC_DFNOF=0x4A,
            pNORMAL,    // 0x4B
    pLOW,   // OPC_SSTAT=0x4C,
            pNORMAL,    // 0x4D
            pNORMAL,    // 0x4E
    pLOW,   // OPC_NNRSM=0x4F,
    pLOW,   // OPC_RQNN=0x50,
    pLOW,   // OPC_NNREL=0x51,
    pLOW,   // OPC_NNACK=0x52,
    pLOW,   // OPC_NNLRN=0x53,
    pLOW,   // OPC_NNULN=0x54,
    pLOW,   // OPC_NNCLR=0x55,
    pLOW,   // OPC_NNEVN=0x56,
    pLOW,   // OPC_NERD=0x57,
    pLOW,   // OPC_RQEVN=0x58,
    pLOW,   // OPC_WRACK=0x59,
    pLOW,   // OPC_RQDAT=0x5A,
    pLOW,   // OPC_RQDDS=0x5B,
    pLOW,   // OPC_BOOT=0x5C,
    pLOW,   // OPC_ENUM=0x5D,
    pLOW,   // OPC_NNRST=0x5E,
    pLOW,   // OPC_EXTC1=0x5F,
    pNORMAL,   // OPC_DFUN=0x60,
    pNORMAL,   // OPC_GLOC=0x61,
    pNORMAL,   // OPC_ERR=0x63,
            pNORMAL,    // 0x64
            pNORMAL,    // 0x65
    pHIGH,   // OPC_SQU=0x66,
            pNORMAL,    // 0x67
            pNORMAL,    // 0x68
            pNORMAL,    // 0x69
            pNORMAL,    // 0x6A
            pNORMAL,    // 0x6B
            pNORMAL,    // 0x6C
            pNORMAL,    // 0x6D
            pNORMAL,    // 0x6E
    pLOW,   // OPC_CMDERR=0x6F,
    pLOW,   // OPC_EVNLF=0x70,
    pLOW,   // OPC_NVRD=0x71,
    pLOW,   // OPC_NENRD=0x72,
    pLOW,   // OPC_RQNPN=0x73,
    pLOW,   // OPC_NUMEV=0x74,
    pLOW,   // OPC_CANID=0x75,
    pLOW,   // OPC_MODE=0x76,
            pNORMAL,    // 0x77
    pLOW,   // OPC_RQSD=0x78,
            pNORMAL,    // 0x79
            pNORMAL,    // 0x7A
            pNORMAL,    // 0x7B
            pNORMAL,    // 0x7C
            pNORMAL,    // 0x7D
            pNORMAL,    // 0x7E
    pLOW,   // OPC_EXTC2=0x7F,
    pNORMAL,   // OPC_RDCC3=0x80,
            pNORMAL,    // 0x81
    pNORMAL,   // OPC_WCVO=0x82,
    pNORMAL,   // OPC_WCVB=0x83,
    pNORMAL,   // OPC_QCVS=0x84,
    pNORMAL,   // OPC_PCVS=0x85,
            pNORMAL,    // 0x86
    pLOW,   // OPC_RDGN=0x87,
            pNORMAL,    // 0x88
            pNORMAL,    // 0x89
            pNORMAL,    // 0x8A
            pNORMAL,    // 0x8B
            pNORMAL,    // 0x8C
            pNORMAL,    // 0x8D
    pLOW,   // OPC_NVSETRD=0x8E,
            pNORMAL,    // 0x8F
    pLOW,   // OPC_ACON=0x90,
    pLOW,   // OPC_ACOF=0x91,
    pLOW,   // OPC_AREQ=0x92,
    pLOW,   // OPC_ARON=0x93,
    pLOW,   // OPC_AROF=0x94,
    pLOW,   // OPC_EVULN=0x95,
    pLOW,   // OPC_NVSET=0x96,
    pLOW,   // OPC_NVANS=0x97,
    pLOW,   // OPC_ASON=0x98,
    pLOW,   // OPC_ASOF=0x99,
    pLOW,   // OPC_ASRQ=0x9A,
    pLOW,   // OPC_PARAN=0x9B,
    pLOW,   // OPC_REVAL=0x9C,
    pLOW,   // OPC_ARSON=0x9D,
    pLOW,   // OPC_ARSOF=0x9E,
    pLOW,   // OPC_EXTC3=0x9F
    pNORMAL,   // OPC_RDCC4=0xA0,
            pNORMAL,    // 0xA1
    pNORMAL,   // OPC_WCVS=0xA2,
            pNORMAL,    // 0xA3
            pNORMAL,    // 0xA4
            pNORMAL,    // 0xA5
            pNORMAL,    // 0xA6
            pNORMAL,    // 0xA7
            pNORMAL,    // 0xA8
            pNORMAL,    // 0xA9
            pNORMAL,    // 0xAA
    pLOW,   // OPC_HEARTB=0xAB,
    pLOW,   // OPC_SD=0xAC,
            pNORMAL,    // 0xAD
            pNORMAL,    // 0xAE
    pLOW,   // OPC_GRSP=0xAF,
    pLOW,   // OPC_ACON1=0xB0,
    pLOW,   // OPC_ACOF1=0xB1,
    pLOW,   // OPC_REQEV=0xB2,
    pLOW,   // OPC_ARON1=0xB3,
    pLOW,   // OPC_AROF1=0xB4,
    pLOW,   // OPC_NEVAL=0xB5,
    pLOW,   // OPC_PNN=0xB6,
           pNORMAL,    // 0xB7
    pLOW,   // OPC_ASON1=0xB8,
    pLOW,   // OPC_ASOF1=0xB9,
           pNORMAL,    // 0xBA
           pNORMAL,    // 0xBB
           pNORMAL,    // 0xBC
    pLOW,   // OPC_ARSON1=0xBD,
    pLOW,   // OPC_ARSOF1=0xBE,
    pLOW,   // OPC_EXTC4=0xBF,
    pNORMAL,   // OPC_RDCC5=0xC0,
    pNORMAL,   // OPC_WCVOA=0xC1,
    pNORMAL,   // OPC_CABDAT=0xC2,
           pNORMAL,    // 0xC3
           pNORMAL,    // 0xC4
           pNORMAL,    // 0xC5
           pNORMAL,    // 0xC6
    pLOW,   // OPC_DGN=0xC7,
           pNORMAL,    // 0xC8
           pNORMAL,    // 0xC9
           pNORMAL,    // 0xCA
           pNORMAL,    // 0xCB
           pNORMAL,    // 0xCC
           pNORMAL,    // 0xCD
           pNORMAL,    // 0xCE
    pNORMAL,   // OPC_FCLK=0xCF,
    pLOW,   // OPC_ACON2=0xD0,
    pLOW,   // OPC_ACOF2=0xD1,
    pLOW,   // OPC_EVLRN=0xD2,
    pLOW,   // OPC_EVANS=0xD3,
    pLOW,   // OPC_ARON2=0xD4,
    pLOW,   // OPC_AROF2=0xD5,
           pNORMAL,    // 0xD6
           pNORMAL,    // 0xD7
    pLOW,   // OPC_ASON2=0xD8,
    pLOW,   // OPC_ASOF2=0xD9,
           pNORMAL,    // 0xDA
           pNORMAL,    // 0xDB
           pNORMAL,    // 0xDC
    pLOW,   // OPC_ARSON2=0xDD,
    pLOW,   // OPC_ARSOF2=0xDE,
    pLOW,   // OPC_EXTC5=0xDF,
    pNORMAL,   // OPC_RDCC6=0xE0,
    pNORMAL,   // OPC_PLOC=0xE1,
    pLOW,   // OPC_NAME=0xE2,
    pNORMAL,   // OPC_STAT=0xE3,
           pNORMAL,    // 0xE4
           pNORMAL,    // 0xE5
    pLOW,   // OPC_ENACK=0xE6,
    pLOW,   // OPC_ESD=0xE7,
           pNORMAL,    // 0xE8
    pLOW,   // OPC_DTXC=0xE9,
           pNORMAL,    // 0xEA
           pNORMAL,    // 0xEB
           pNORMAL,    // 0xEC
           pNORMAL,    // 0xED
           pNORMAL,    // 0xEE
    pLOW,   // OPC_PARAMS=0xEF,
    pLOW,   // OPC_ACON3=0xF0,
    pLOW,   // OPC_ACOF3=0xF1,
    pLOW,   // OPC_ENRSP=0xF2,
    pLOW,   // OPC_ARON3=0xF3,
    pLOW,   // OPC_AROF3=0xF4,
    pLOW,   // OPC_EVLRNI=0xF5,
    pLOW,   // OPC_ACDAT=0xF6,
    pLOW,   // OPC_ARDAT=0xF7,
    pLOW,   // OPC_ASON3=0xF8,
    pLOW,   // OPC_ASOF3=0xF9,
    pLOW,   // OPC_DDES=0xFA,
    pLOW,   // OPC_DDRS=0xFB,
    pLOW,   // OPC_ARSON3=0xFD,
    pLOW,   // OPC_ARSOF3=0xFE
           pNORMAL,    // 0xFF
};

#ifdef BOOTLOADER_PRESENT
// ensure that the bootflag is zeroed
#pragma romdata BOOTFLAG
uint8_t eeBootFlag = 0;
#endif


/**
 * The module's transport interface.
 */
const Transport * transport;            // pointer to the Transport interface
static Message tmpMessage;

/**
 * Used to control the rate at which timedResponse messages are sent.
 */
static TickValue timedResponseTime;
static uint8_t timedResponseDelay;


/**
 * Used to control how often it is checked whether any of the flash buffer
 * needs to be flushed out to permanent storage.
 */
static TickValue flashFlushTime;

/** 
 * Function that must be provided by the application. 
 * Called when a message is received and before the message is processed 
 * by the VLCB library. 
 * Allows the application to override VLCB functionality.
 */
extern Processed APP_preProcessMessage(Message * m);
/** 
 * Function that must be provided by the application. 
 * Called when a message is received and after the message has been checked 
 * by the VLCB library. Called only if the message is not processed by the 
 * VLCB library.
 * Allows the application to handle messages, including events.
 */
extern Processed APP_postProcessMessage(Message * m);
/** 
 * Function that must be provided by the application. 
 * Called when the module is being reset back to manufacturer defaults.
 * Allows the application to initialise its non volatile memory.
 */
extern void APP_factoryReset(void);
/**
 * Function that must be provided by the application.
 * Called if the push button is held down during power up. 
 * Allows the application to provide test functionality during construction or setup.
 */
extern void APP_testMode(void);

// forward declarations
/**
 * Function that must be provided by the application.
 * Called by the VLCB library upon power up after the library has initialised itself.
 * Allows the application to perform its own initialisation.
 */
void setup(void);
/**
 * Function that must be provided by the application.
 * Called by the VLCB library repeatedly to allow the application to perform 
 * processing during normal operation.
 */
void loop(void);


/////////////////////////////////////////////
// SERVICE CHECKING FUNCTIONS
/////////////////////////////////////////////
/**
 * Find a service and obtain its service descriptor if it has been used by the
 * module.
 * @param id the service type id
 * @return the service descriptor or NULL if the service is not used by the module.
 */
const Service * findService(uint8_t id) {
    uint8_t i;
    for (i=0; i<NUM_SERVICES; i++) {
        if ((services[i] != NULL) && (services[i]->serviceNo == id)) {
            return services[i];
        }
    }
    return NULL;
}

/**
 * Obtain the index int the services array of the specified service.
 * @param serviceType the service type id
 * @return the index into the services array or -1 if the service is not used by the module.
 */
uint8_t findServiceIndex(uint8_t serviceType) {
    uint8_t i;
    for (i=0; i<NUM_SERVICES; i++) {
        if ((services[i] != NULL) && (services[i]->serviceNo == serviceType)) {
            return i;
        }
    }
    return SERVICE_ID_NOT_FOUND;
}

/**
 * Tests whether the module used the specified service.
 * @param id the service type id
 * @return PRESENT if the service is present NOT_PRESENT otherwise
 */
ServicePresent have(uint8_t id) {
    uint8_t i;
    for (i=0; i<NUM_SERVICES; i++) {
        if ((services[i] != NULL) && (services[i]->serviceNo == id)) {
            return PRESENT;
        }
    }
    return NOT_PRESENT;
}

/////////////////////////////////////////////
//FUNCTIONS TO CALL SERVICES
/////////////////////////////////////////////
/**
 * Perform the factory reset for all services and VLCB base.
 * VLCB function to perform necessary factory reset functionality and also
 * call the factoryReset function for each service followed by the Application
 * factory reset APP_factoryReset().
 */
void factoryReset(void) {
    uint8_t i;
 
    for (i=0; i<NUM_SERVICES; i++) {
        if ((services[i] != NULL) && (services[i]->factoryReset != NULL)) {
            services[i]->factoryReset();
        }
    }
    // now write the version number
    writeNVM(NV_NVM_TYPE, NV_ADDRESS, APP_NVM_VERSION);
    
    APP_factoryReset();
}

/**
 * Perform power up for all services and VLCB base.
 * VLCB function to perform necessary power up functionality and also
 * call the powerUp function for each service.
 */
static void powerUp(void) {
    uint8_t i;
    uint8_t divider;
    
    // Initialise the Tick timer. Uses low priority interrupts
    initTicker(0);
    initTimedResponse();
    leds_powerUp();
    timedResponseDelay = 5;
    
    for (i=0; i<NUM_SERVICES; i++) {
        if ((services[i] != NULL) && (services[i]->powerUp != NULL)) {
            services[i]->powerUp();
        }
    }
}

/*
 * Update the delay between each timedResponse transmission. By default a value 
 * of 5 is used for a delay of 5ms.
 * Typically the application would allocate an NV to hold the delay value. The 
 * Application can use its callback when the NV value is changed to call this 
 * function.
 * @param delay the delay in millisec
 */
void setTimedResponseDelay(uint8_t delay) {
    timedResponseDelay = delay;
}


/**
 * Time how long the pb is held down for, with a timeout.
 * 
 * @param timeout number of seconds to wait
 * @return seconds pb held down or 0 for a timeout
 */
uint8_t pbDownTimer(uint8_t timeout) {
    // determine how long the button is held for
    pbTimer.val = tickGet();
    while (APP_pbPressed()) {
        leds_poll();
        if (tickTimeSince(pbTimer) > timeout*ONE_SECOND) {
            return 0;   // timeout
        }
    }
    // no longer pressed
    return (uint8_t)(tickTimeSince(pbTimer)/ONE_SECOND);
}

/**
 * Time how long the pb is released down for, with a timeout.
 * 
 * @param timeout number of seconds to wait
 * @return seconds pb released or 0 for a timeout
 */
uint8_t pbUpTimer(uint8_t timeout) {
    // determine how long the button is released for
    pbTimer.val = tickGet();
    while (! (APP_pbPressed())) {
        leds_poll();
        if (tickTimeSince(pbTimer) > timeout*ONE_SECOND) {
            return 0;   // timeout
        }
    }
    // now pressed
    return (uint8_t)(tickTimeSince(pbTimer)/ONE_SECOND);
}

/**
 * Do checks to see if the push button was held down during power up. 
 * Then call back into the application to do the relevant work.
 * Test Mode
 *   The push button is held down at power up for between 2 and 6 seconds then released.
 *   If held down for more than 30 seconds then the module shall continue normal operation.
 * Factory reset
 *   The push button is held down at power up for between 10 and 30 seconds
 *   then released and then pressed again for between 2 and 4 seconds.
 */
static void checkPowerOnPb(void) {
    uint8_t i;
    
    // check for the push button being pressed at power up
    if (APP_pbPressed()) {
        // determine how long the button is held for
        i = pbDownTimer(30);
        if (i == 0) {
            //Timeout
            return;
        } else if ((i>=2) && (i < 6)) {
            APP_testMode();
        } else if (i >= 10) {
            showStatus(STATUS_RESET_WARNING);
            // wait for pb down max 5 seconds
            i = pbUpTimer(5);
            if (i == 0) {
                // Timeout
                return;
            }
            i = pbDownTimer(5);
            if ((i>=2) && (i < 4)) {
                factoryReset();
            }
        }
    }
}

/**
 * Poll each service.
 * VLCB function to perform necessary poll functionality and regularly 
 * poll each service.
 * 
 * Polling occurs as frequently as possible. It is the responsibility of the
 * service's poll function to ensure that any actions are performed at the 
 * correct rate, for example by using tickTimeSince(lastTime).
 * This also attempts to obtain a message from transport and call the services
 * to process the message. Will also call back into APP to process message.
 */
static void poll(void) {
    uint8_t i;
    Message m;
    Processed handled;
    
    /* handle any timed responses */
    if (tickTimeSince(timedResponseTime) > (long)timedResponseDelay*ONE_MILI_SECOND) {
        pollTimedResponse();
        timedResponseTime.val = tickGet();
    }
    if (tickTimeSince(flashFlushTime) > ONE_SECOND) {
        flushFlashBlock();
        flashFlushTime.val = tickGet();
    }
    /* call any service polls */
    for (i=0; i<NUM_SERVICES; i++) {
        if ((services[i] != NULL) && (services[i]->poll != NULL)) {
            services[i]->poll();
        }
    }
    
    leds_poll();
    
    // Handle any incoming messages from the transport
    handled = NOT_PROCESSED;
    if (transport != NULL) {
        if (transport->receiveMessage != NULL) {
            if (transport->receiveMessage(&m)) {
                if (m.len > 0) {
                    showStatus(STATUS_MESSAGE_RECEIVED);
                    handled = APP_preProcessMessage(&m); // Call App to check for any opcodes to be handled. 
                    if (handled == NOT_PROCESSED) {
                        for (i=0; i<NUM_SERVICES; i++) {
                            if ((services[i] != NULL) && (services[i]->processMessage != NULL)) {
                                if (services[i]->processMessage(&m) == PROCESSED) {
                                    handled = PROCESSED;
                                    break;
                                }
                            }
                        }
                        if (handled == NOT_PROCESSED) {     // Call App to check for any opcodes to be handled. 
                            handled = APP_postProcessMessage(&m);
                        }
                    }
                }
            }
        }
    }
    if (handled) {
        showStatus(STATUS_MESSAGE_ACTED);
    }
}

#if defined(_18F66K80_FAMILY_)
/**
 * Call each service's high priority interrupt service routine.
 * VLCB function to handle high priority interrupts. A service wanting 
 * to use interrupts should enable the interrupts in hardware and then provide 
 * a lowIsr function. Care must to taken to ensure that the cause of the 
 * interrupt is cleared within the ISR and that minimum time is spent in the 
 * ISR. It is preferable to set a flag within the ISR and then perform longer
 * processing within poll().
 */
static void highIsr(void) {
    uint8_t i;
    
    for (i=0; i<NUM_SERVICES; i++) {
        if ((services[i] != NULL) && (services[i]->highIsr != NULL)) {
            services[i]->highIsr();
        }
    }
    APP_highIsr();
}

/**
 * Call each service's low priority interrupt service routine.
 * VLCB function to handle low priority interrupts. A service wanting 
 * to use interrupts should enable the interrupts in hardware and then provide 
 * a lowIsr function. Care must to taken to ensure that the cause of the 
 * interrupt is cleared within the ISR and that minimum time is spent in the 
 * ISR. It is preferable to set a flag within the ISR and then perform longer
 * processing within poll().
 */
static void lowIsr(void) {
    uint8_t i;
    
    for (i=0; i<NUM_SERVICES; i++) {
        if ((services[i] != NULL) && (services[i]->lowIsr != NULL)) {
            services[i]->lowIsr();
        }
    }
    APP_lowIsr();
}
#endif

/*
 * Checks that the required number of message bytes are present.
 * @param m the message to be checked
 * @param needed the number of bytes within the message needed including the opc
 * @param service he service making the test
 * @return PROCESSED if it is an invalid message and should not be processed further
 */
Processed checkLen(Message * m, uint8_t needed, uint8_t service) {
    if (m->len < needed) {
#ifdef VLCB_GRSP
        if (m->len > 2) {
            if ((m->bytes[0] == nn.bytes.hi) && (m->bytes[1] == nn.bytes.lo)) {
                sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, m->opc, service, CMDERR_INV_CMD);
            }
        }
#endif
        return PROCESSED;
    }
    return NOT_PROCESSED;
}

/////////////////////////////////////////////
// Message handling functions
/////////////////////////////////////////////

/*
 * Test whether the given opc relates to an event.
 * @param opc the opcode
 */
Boolean isEvent(uint8_t opc) {
    return (((opc & EVENT_SET_MASK) == EVENT_SET_MASK) && ((~opc & EVENT_CLR_MASK)== EVENT_CLR_MASK)) ? TRUE : FALSE;
}

/*
 * Send a message with just OPC.
 * @param opc opcode
 */
void sendMessage0(VlcbOpCodes opc){
    sendMessage(opc, 1, 0,0,0,0,0,0,0);
}

/*
 * Send a message with OPC and 1 data byte.
 * @param opc opcode
 * @param data1 data byte
 */
void sendMessage1(VlcbOpCodes opc, uint8_t data1){
    sendMessage(opc, 2, data1, 0,0,0,0,0,0);
}

/*
 * Send a message with OPC and 2 data bytes.
 * @param opc opcode
 * @param data1 data byte 1
 * @param data2 data byte 2
 */
void sendMessage2(VlcbOpCodes opc, uint8_t data1, uint8_t data2){
    sendMessage(opc, 3, data1, data2, 0,0,0,0,0);
}

/*
 * Send a message with OPC and 3 data bytes.
 * @param opc opcode
 * @param data1 data byte 1
 * @param data2 data byte 2
 * @param data3 data byte 3
 */
void sendMessage3(VlcbOpCodes opc, uint8_t data1, uint8_t data2, uint8_t data3) {
    sendMessage(opc, 4, data1, data2, data3, 0,0,0,0);
}

/*
 * Send a message with OPC and 4 data bytes.
 * @param opc opcode
 * @param data1 data byte 1
 * @param data2 data byte 2
 * @param data3 data byte 3
 * @param data4 data byte 4
 */
void sendMessage4(VlcbOpCodes opc, uint8_t data1, uint8_t data2, uint8_t data3, uint8_t data4){
    sendMessage(opc, 5, data1, data2, data3, data4, 0,0,0);
}

/*
 * Send a message with OPC and 5 data bytes.
 * @param opc opcode
 * @param data1 data byte 1
 * @param data2 data byte 2
 * @param data3 data byte 3
 * @param data4 data byte 4
 * @param data5 data byte 5
 */
void sendMessage5(VlcbOpCodes opc, uint8_t data1, uint8_t data2, uint8_t data3, uint8_t data4, uint8_t data5) {
    sendMessage(opc, 6, data1, data2, data3, data4, data5, 0,0);
}

/*
 * Send a message with OPC and 6 data bytes.
 * @param opc opcode
 * @param data1 data byte 1
 * @param data2 data byte 2
 * @param data3 data byte 3
 * @param data4 data byte 4
 * @param data5 data byte 5
 * @param data6 data byte 6
 */
void sendMessage6(VlcbOpCodes opc, uint8_t data1, uint8_t data2, uint8_t data3, uint8_t data4, uint8_t data5, uint8_t data6) {
    sendMessage(opc, 7, data1, data2, data3, data4, data5, data6,0);
}

/*
 * Send a message with OPC and 7 data bytes.
 * @param opc opcode
 * @param data1 data byte 1
 * @param data2 data byte 2
 * @param data3 data byte 3
 * @param data4 data byte 4
 * @param data5 data byte 5
 * @param data6 data byte 6
 * @param data7 data byte 7
 */
void sendMessage7(VlcbOpCodes opc, uint8_t data1, uint8_t data2, uint8_t data3, uint8_t data4, uint8_t data5, uint8_t data6, uint8_t data7) {
    sendMessage(opc, 8, data1, data2, data3, data4, data5, data6, data7);
}

/*
 * Send a message of variable length with OPC and up to 7 data bytes.
 * @param opc
 * @param len
 * @param data1
 * @param data2
 * @param data3
 * @param data4
 * @param data5
 * @param data6
 * @param data7
 */
void sendMessage(VlcbOpCodes opc, uint8_t len, uint8_t data1, uint8_t data2, uint8_t data3, uint8_t data4, uint8_t data5, uint8_t data6, uint8_t data7) {
    tmpMessage.opc = opc;
    tmpMessage.len = len;
    tmpMessage.bytes[0] = data1;
    tmpMessage.bytes[1] = data2;
    tmpMessage.bytes[2] = data3;
    tmpMessage.bytes[3] = data4;
    tmpMessage.bytes[4] = data5;
    tmpMessage.bytes[5] = data6;
    tmpMessage.bytes[6] = data7;
    if (transport != NULL) {
        if (transport->sendMessage != NULL) {
            transport->sendMessage(&tmpMessage);
        }
    }
}

/**
 * This is the VLCB application start.
 * Do some initialisation, call the factoryReset() if necessary, call the powerUp()
 * service routines. Then call the Application's setup() before entering the main loop
 * which calls the Application's loop() and each service poll().
 */
void main(void) {
    uint8_t i;
    
    /*
     * Set up the processor clock
     */
#if defined(_18F66K80_FAMILY_)
    OSCTUNEbits.PLLEN = 1;      // enable the Phase Locked Loop x4
#endif
#if defined(_18FXXQ83_FAMILY_)
    OSCCON1bits.NOSC = 2;   // EXTOSC and 4 x PLL
    OSCCON1bits.NDIV = 0;   // Divider = 1 (no divide)
        /* Note PLLEN is used by the PIC18F27Q83 to control whether peripherals
         * Use the Ext clock or the 4 x PLL.
         * Here we leave PLLEN=0 so peripherals do NOT use the PLL.
         */
#endif
    
    /*
     * Set up the interrupts
     */
#if defined(_18F66K80_FAMILY_)
    RCONbits.IPEN = 1;  // enable interrupt priority
#endif
#if defined(_18FXXQ83_FAMILY_)
    // Set up the interrupt vector table
    IVTBASEU = IVT_BASE_U;
    IVTBASEH = IVT_BASE_H;
    IVTBASEL = IVT_BASE_L;
    
    IVTLOCK = 0x55;
    IVTLOCK = 0xAA;
    IVTLOCKbits.IVTLOCKED = 0x01; // lock IVT
#endif
    
#if defined(_18FXXQ83_FAMILY_)
    /*
     * Set up the PPS
     */
    // defaults are fine but PPS is left unlocked to allow Services and App to change if necessary.
#endif

     
    // init the romops ready for flash writes
    initRomOps();
    
    if (readNVM(NV_NVM_TYPE, NV_ADDRESS) != APP_NVM_VERSION) {
        factoryReset();
    }
    
    // initialise the services
    powerUp();
    
    // Check for PB held down at power up
    bothEi();
    checkPowerOnPb();
    
    // call the application's init 
    bothDi();
    setup();
    
    // enable the interrupts and ready to go
    bothEi();
    while(1) {
        // poll the services as quickly as possible.
        // up to service to ignore the polls it doesn't need.
        poll();
        loop();
    }
}


#if defined(_18F66K80_FAMILY_)
/*
 * High priority Interrupt Service Routine.
 */ 
void __interrupt(high_priority) __section("mainSec") isrHigh() {
    highIsr();
}

/*
 * Low priority Interrupt Service Routine handler.
 */
void __interrupt(low_priority) __section("mainSec") isrLow() {
    lowIsr();
}
#endif

#if defined(_18FXXQ83_FAMILY_)
void __interrupt(irq(default), base(IVT_BASE)) DEFAULT_ISR(void)
{
// Unhandled interrupts go here
}
#endif