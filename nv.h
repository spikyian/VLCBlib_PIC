#ifndef _NV_H_
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
#define _NV_H_
#include "vlcb.h"

/**
 * @file
 * @brief
 * Implementation of the VLCB NV service.
 * @details
 * The service definition object is called nvService.
 *
 * The NV service implements the VLCB Node Variable Service. This supports 
 * the NVSET, NVRD and NVSETRD opcodes.
 * 
 * If NV_CACHE is defined in module.h then the NV service implements a cache
 * of NV values in RAM. This can be used to speed up obtaining NV values used 
 * within the application at the expense of additional RAM usage.
 * 
 * # Dependencies on other Services
 * Although the NV service does not depend upon any other services all modules
 * must include the MNS service.
 * 
 * # Module.h definitions required for the NV service
 * - \#define NV_NUM       The number of NVs. Will be returned in the parameter block.
 * - \#define NV_CACHE     Defined, as opposed to undefined, to enable a cache of
 *                      NVs in RAM. Uses more RAM but speeds up access to NVs when
 *                      in normal operation processing events. 
 * - \#define NV_ADDRESS   the address in non volatile memory to place the NVM version 
 *                      number and NVs if the NV service is included.
 * - \#define NV_NVM_TYPE  the type of NVM memory to be used for NVs. Can be either
 *                      EEPROM_NVM_TYPE or FLASH_NVM_TYPE.
 * - Function uint8_t APP_nvDefault(uint8_t index) The application must implement this 
 *                      function to provide factory default values for NVs.
 * - Function NvValidation APP_nvValidate(uint8_t index, uint8_t value) The application
 *                      must implement this function in order to validate that
 *                      the value being written to an NV is valid.
 * - Function void APP_nvValueChanged(uint8_t index, uint8_t newValue, uint8_t oldValue)
 *                      The application must implement this function in order to
 *                      perform any needed functionality to be performed when an 
 *                      NV value is changed.
 * 
 */

/**
 * Expose the service descriptor for the NV service.
 */
extern const Service nvService;

/**
 * Type definition used to indicate whether an NV accepts the value attempting to be set.
 */
typedef enum NvValidation {
    INVALID=0,
    VALID=1
} NvValidation;

/**
 * The module's application must provide a function to validate the value being
 * written to a NV.
 * @param index the NV index
 * @param value the proposed NV value
 * @return whether the proposed value is acceptable
 */
extern NvValidation APP_nvValidate(uint8_t index, uint8_t value);

/* The list of the diagnostics supported */
#define NUM_NV_DIAGNOSTICS 2    ///< The number of diagnostics supported by this service
#define NV_DIAGNOSTICS_NUM_ACCESS  0x00    ///< return Global status Byte.
#define NV_DIAGNOSTICS_NUM_FAIL    0x01    ///< return uptime upper word.

extern int16_t getNV(uint8_t index);
extern void saveNV(uint8_t index, uint8_t value);
extern uint8_t setNV(uint8_t index, uint8_t value);
/**
 * Load the NV cache using values stored in non volatile memory.
 */
extern void loadNvCache(void);

#endif
