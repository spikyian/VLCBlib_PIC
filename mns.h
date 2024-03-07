#ifndef _MNS_H_
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
#define _MNS_H_
#include "vlcb.h"
#include "ticktime.h"
#include "module.h"
/**
 *
 * @file
 * Implementation of the VLCB Minimum Module Service.
 * @details
 * MNS provides functionality required by all VLCB modules.
 *
 * The service definition object is called mnsService.
 * This is a large service as it handles mode transitions, including setting of 
 * node number. The module's LEDs and push button are also supported.
 * Service discovery and Diagnostics are also processed from this service.
 * 
 * # Dependencies on other Services
 * None.
 * 
 * MNS is a mandatory service for all VLCB modules and is therefore to be
 * included by all module applications.
 * 
 * # Module.h definitions required for the MNS service
 * - #define NUM_SERVICES to be the number of services implemented by the module.
 *                      The application must put the services into the services array.
 * - #define APP_NVM_VERSION the version number of the data structures stored in NVM
 *                      this is located where NV#0 is stored therefore NV_ADDRESS
 *                      and NV_NVM_TYPE must be defined even without the NV service.
 * - #define clkMHz       Must be set to the clock speed of the module. Typically 
 *                      this would be 4 or 16.
 * - #define NN_ADDRESS   This must be set to the address in non volatile memory
 *                      at which the node number is to be stored.
 * - #define NN_NVM_TYPE  This must be set to the type of the NVM where the node
 *                      number is to be stored.
 * - #define MODE_ADDRESS This must be set to the address in non volatile memory
 *                      at which the mode variable is to be stored.
 * - #define MODE_NVM_TYPE This must be set to the type of the NVM where the mode
 *                      variable is to be stored.
 * - #define APP_setPortDirections() This macro must be set to configure the 
 *                      processor's pins for output to the LEDs and input from the
 *                      push button. It should also enable digital I/O if required 
 *                      by the processor.
 * - #define APP_writeLED1(state) This macro must be defined to set LED1 (normally
 *                      yellow) to the state specified. 1 is LED on.
 * - #define APP_writeLED2(state) This macro must be defined to set LED1 (normally
 *                      green) to the state specified. 1 is LED on.
 * - #define APP_pbPressed() This macro must be defined to read the push button
 *                      input, returning true when the push button is held down.
 * - #define NAME         The name of the module must be defined. Must be exactly 
 *                      7 characters. Shorter names should be padded on the right 
 *                      with spaces. The name must leave off the communications 
 *                      protocol e.g. the CANMIO module would be set to "MIO    ".
 * 
 * The following parameter values are required to be defined for use by MNS:
 * - #define PARAM_MANU              See the manufacturer settings in vlcb.h
 * - #define PARAM_MAJOR_VERSION     The major version number
 * - #define PARAM_MINOR_VERSION     The minor version character. E.g. 'a'
 * - #define PARAM_BUILD_VERSION     The build version number
 * - #define PARAM_MODULE_ID         The module ID. Normally set to MTYP_VLCB
 * - #define PARAM_NUM_NV            The number of NVs. Normally set to NV_NUM
 * - #define PARAM_NUM_EVENTS        The number of events.
 * - #define PARAM_NUM_EV_EVENT      The number of EVs per event
 * 
 */


/*
 * Expose the service descriptor for MNS.
 */
extern const Service mnsService;

/* The list of the diagnostics supported */
#define NUM_MNS_DIAGNOSTICS 6   ///< The number of diagnostic values for this service
#define MNS_DIAGNOSTICS_ALL         0x00    ///< The a series of DGN messages for each services? supported data.
#define MNS_DIAGNOSTICS_STATUS      0x00    ///< The Global status Byte.
#define MNS_DIAGNOSTICS_UPTIMEH     0x01    ///< The uptime upper word.
#define MNS_DIAGNOSTICS_UPTIMEL     0x02    ///< The uptime lower word.
#define MNS_DIAGNOSTICS_MEMERRS     0x03    ///< The memory status.
#define MNS_DIAGNOSTICS_NNCHANGE    0x04    ///< The number of Node Number changes.
#define MNS_DIAGNOSTICS_RXMESS      0x05    ///< The number of received messages acted upon.

/*
 * The module's node number.
 */
extern Word nn;
/*
 * the module's operational mode.
 */
extern uint8_t mode_state;

/**
 * Module operating mode flags.
 */
extern uint8_t mode_flags;

/*
 * MNS diagnostics
 */
extern DiagnosticVal mnsDiagnostics[NUM_MNS_DIAGNOSTICS];

extern void updateModuleErrorStatus(void);

extern TickValue pbTimer;

#endif
