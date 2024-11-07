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
#include "boot.h"
#include "mns.h"
/**
 * @file
 * @brief
 * Implementation of the VLCB BOOT service, supports the FCU and CBUS (PIC based)
 * bootloading protocol.
 * @details
 * The service definition object is called bootService.
 * In order to be compatible with the FCU bootloader there are additional
 * requirements for the parameter block which are actually supported by the MNS.
 *
 * Application code packed with the bootloader must be compiled with options:
 *  - XC8 linker options -> Additional options --CODEOFFSET=0x800 
 *  - This generates an error ::: warning: (2044) unrecognized option "--CODEOFFSET=0x800"
 * but this can be ignored as the option works.
 * 
 * Then the Application project must be made dependent on the Bootloader 
 * project by adding the Bootloader to project properties->Conf:->Loading
 * The project hex fill will have the .unified.hex extension.
 */

// forward declarations
static void bootPowerUp(void);
static Processed bootProcessMessage(Message * m);
static uint8_t bootEsdData(uint8_t id);

/**
 * The service descriptor for the BOOT service. The application must include this
 * descriptor within the application's const Service * const services[] array and include the
 * necessary settings within module.h in order to make use of the PIC bootloading
 * service.
 */
const Service bootService = {
    SERVICE_ID_BOOT,    // id
    2,                  // version 2 supporting versioning
    NULL,               // factoryReset
    bootPowerUp,        // powerUp
    bootProcessMessage, // processMessage
    NULL,               // poll
#if defined(_18F66K80_FAMILY_)
    NULL,               // highIsr
    NULL,               // lowIsr
#endif
#ifdef VLCB_SERVICE
    bootEsdData,        // ESD data
#endif
#ifdef VLCB_DIAG
    NULL                // getDiagnostic
#endif
};

// Set the EEPROM_BOOT_FLAG to 0 to ensure the application is entered
// after loading using the PicKit.
// Bootflag resides at top of EEPROM.
/** @private */
asm("PSECT eeprom_data,class=EEDATA");
/** @private */
asm("ORG " ___mkstr(EE_TOP));
/** @private */
asm("db 0");
/** Used internally for calculating checksum.  */
#define PRM_CKSUM1 PARAM_MANU+PARAM_MINOR_VERSION+PARAM_MODULE_ID+PARAM_NUM_EVENTS\
    +PARAM_NUM_EV_EVENT+PARAM_NUM_NV+PARAM_MAJOR_VERSION+(8)\
    +(8)+CPUM_MICROCHIP+PARAM_BUILD_VERSION\
    +(20)+(0x48)+(0x08)

#ifdef CONSUMED_EVENTS
/** Used internally for calculating checksum.  */
#define PRM_CKSUM2      PRM_CKSUM1+1
#else
/** Used internally for calculating checksum.  */
#define PRM_CKSUM2      PRM_CKSUM1
#endif
#ifdef PRODUCED_EVENTS
/** Used internally for calculating checksum.  */
#define PRM_CKSUM3      PRM_CKSUM2+2
#else
/** Used internally for calculating checksum.  */
#define PRM_CKSUM3      PRM_CKSUM2
#endif
#ifdef CANID_ADDRESS
/** Used internally for calculating checksum.  */
#define PRM_CKSUM4      PRM_CKSUM3+PB_CAN
#else
/** Used internally for calculating checksum.  */
#define PRM_CKSUM4      PRM_CKSUM3
#endif
#if defined(_18F66K80_FAMILY_)
#define    PRM_CKSUM5  PRM_CKSUM4+P18F26K80
#elif defined(_18FXXQ83_FAMILY_)
#define    PRM_CKSUM5  PRM_CKSUM4+P18F27Q83
#else
    Error unrecognised CPU type,
#endif 

/**
 * @private
 * Create the parameter block located at 0x820. This is done entirely by the 
 * preprocessor.
 */
const uint8_t paramBlock[] __at(0x820) = {
    PARAM_MANU,         //0x820
    PARAM_MINOR_VERSION,//0x821
    PARAM_MODULE_ID,    //0x822
    PARAM_NUM_EVENTS,   //0x823
    PARAM_NUM_EV_EVENT, //0x824
    PARAM_NUM_NV,       //0x825
    PARAM_MAJOR_VERSION,//0x826
    0x08                // definitely BOOTABLE 
#ifdef CONSUMED_EVENTS
            |0x01
#endif
#ifdef PRODUCED_EVENTS
            |0x02
#endif
        ,
#if defined(_18F66K80_FAMILY_)
    P18F26K80,          //0x828
#elif defined(_18FXXQ83_FAMILY_)
    P18F27Q83,          //0x828
#else
    Error unrecognised CPU type,
#endif 
    
#ifdef CANID_ADDRESS
    PB_CAN,             //0x829 Protocol 1=
#endif
    0,8,0,0,            //0x82a load address
    0,0,0,0,            //0x82e processor code
    CPUM_MICROCHIP,     //0x832
    PARAM_BUILD_VERSION,//0x833
    0,                  //0x834 reserved
    0,                  //0x835 reserved
    0,                  //0x836 reserved
    0,                  //0x837 reserved
    20,                 //0x838 num parameters lo
    0,                  //0x839 num parameters hi
    0x48,               //0x83a address of NAME lo
    0x08,               //0x83b address of NAME hi
    0,                  //0x83c address of NAME upper lo
    0,                  //0x83d address of NAME upper hi
    ((PRM_CKSUM5)&0xFF),  //0x83e checksum lo
    ((PRM_CKSUM5)>>8)     //0x83f checksum hi
};


const char bl_version[] = { 'B','L','_','V','E','R','S','I','O','N','='};
static uint8_t bootloaderType;
static uint8_t bootloaderVersion;

/**
 * Power up the BOOT service. Search and extract the bootloader's version.
 */
void bootPowerUp(void) {
    uint24_t a;
    uint8_t i;
    uint8_t b;
    uint8_t found;
    
    bootloaderType = BL_TYPE_Unknown;
    bootloaderVersion = 0;
    
    // attempt to find the bl_version string within the bootloader
    for (a=0; a<0x7FF; a++) {
        found = 1;
        for (i=0; i<11; i++) {
            b = (uint8_t)readNVM(FLASH_NVM_TYPE, a+i);
            if (b != bl_version[i]) {
                found = 0;
                break;
            }
        }
        if (found) {
            bootloaderType = (uint8_t)readNVM(FLASH_NVM_TYPE, a+11);
            bootloaderVersion = (uint8_t)readNVM(FLASH_NVM_TYPE, a+12);
            return;
        } 
    }
}
/**
 * Process the bootloader specific messages. The only messages which need to be 
 * processed is BOOTM (also called BOOT) and MODE.
 * @param m the VLCB message
 * @return PROCESSED to indicate that the message has been processed, NOT_PROCESSED otherwise
 */
static Processed bootProcessMessage(Message * m) {
    // check NN matches us
    if (m->bytes[0] != nn.bytes.hi) return NOT_PROCESSED;
    if (m->bytes[1] != nn.bytes.lo) return NOT_PROCESSED;
    
    switch (m->opc) {
#ifdef VLCB_MODE
        case OPC_MODE:
            if (m->bytes[2] != MODE_BOOT) return NOT_PROCESSED;
            // FALL THROUGH
#endif
        case OPC_BOOT:
            // Set the bootloader flag to be picked up by the bootloader
            writeNVM(BOOT_FLAG_NVM_TYPE, BOOT_FLAG_ADDRESS, 0xFF); 
            RESET();     // will enter the bootloader
            return PROCESSED;
        default:
            return NOT_PROCESSED;   // message not processed
    }
}

/**
 * The BOOT service ESD bytes. Return the bootloader type and version.
 * @param id the identifier for the ESD requested
 * @return  the ESD value
 */
uint8_t bootEsdData(uint8_t id) {
    switch (id) {
        case 1:
            // The bootloader type
            return bootloaderType;
        case 2:
            // The bootloader version
            return bootloaderVersion;
        default:
            return 0;
    }
} 
