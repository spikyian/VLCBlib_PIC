#ifndef _EVENT_ACKNOWLEDGE_H_
#define _EVENT_ACKNOWLEDGE_H_
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


/**
 * @file
 * @brief
 * Implementation of the VLCB EventAcknowledge service.
 * @details
 * This service will send a ENACK message if the module has been taught to
 * consume the received event. The module must be in EVENT ACK mode. 
 * The service definition object is called eventAckService.
 * 
 * # Dependencies on other Services
 * The Event Acknowledge service depends upon the Event Consumer service. The Event
 * Consumer service MUST be included if the Event Acknowledge Service is 
 * included by the module.
 * 
 * # Module.h definitions required for the Event Acknowledge service
 * none
 */

extern const Service eventAckService;

#define NUM_ACK_DIAGNOSTICS 1      ///< The number of diagnostic values associated with this service
#define ACK_DIAG_COUNT              0x00 ///< Count of the number of ACK diagnostics
#define ACK_DIAG_NUM_ACKED          0x01 ///< Number of ACK counter

#endif
