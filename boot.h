#ifndef _BOOT_H_
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
#define _BOOT_H_
#include "vlcb.h"

/**
 * @file
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
 * but this can be ignored as the option works
 * 
 * Then the Application project must be made dependent on the Bootloader 
 * project by adding the Bootloader to project properties->Conf:->Loading
 * The project hex fill will have the .unified.hex extension.
 * 
 * # Dependencies on other Services
 * Although the Boot service does not depend upon any other services all modules
 * must include the MNS service.
 * 
 * # Module.h definitions required for the Boot service
 * - #define BOOT_FLAG_ADDRESS This should be set to where the module's bootloader
 *                      places the bootflag.
 * - #define BOOT_FLAG_NVM_TYPE This should be set to be the type of NVM where the
 *                      bootloader stores the boot flag. This can be set to be 
 *                      either EEPROM_NVM_TYPE or FLASH_NVM_TYPE. The PIC
 *                      modules normally have this set to EEPROM_NVM_TYPE.
 * - #define BOOTLOADER_PRESENT The module should define, as opposed to undefine, 
 *                      this to indicate that the application should be 
 *                      compiled to start at 0x800 to allow room for the bootloader 
 *                      between 0x000 and 0x7FF.
 * 
 */

extern const Service bootService;

#endif
