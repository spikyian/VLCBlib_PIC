#ifndef _CAN_H_
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
#define _CAN_H_
#include <xc.h>
#include "vlcb.h"
#include "ticktime.h"
/**
 * @file
 * Implementation of the VLCB CAN service. 
 * @details
 * Uses Controller Area Network to carry VLCB messages.
 * This implementation works with the PIC18 ECAN.
 * 
 * The service definition object is called canService.
 * The transport interface is called canTransport.
 * 
 * Performs self enumeration and CANID collision detection and re-enumeration.
 * Performs loopback of events for Consumes Own Event behaviour.
 * Collects diagnostic data to aid communications fault finding.
 * 
 * # Dependencies on other Services
 * Although the CAN service does not depend upon any other services all modules
 * must include the MNS service.
 * 
 * # Module.h definitions required for the CAN service
 * - #define CANID_ADDRESS The address of the CANID. PIC modules normally stored 
 *                      this at TOP-1.
 * - #define CANID_NVM_TYPE The type of NVM where to store the CANID. This can be
 *                      set to be either EEPROM_NVM_TYPE or FLASH_NVM_TYPE. The
 *                      PIC modules normally have this set to EEPROM_NVM_TYPE.
 * - #define CAN_INTERRUPT_PRIORITY 0 for low priority, 1 for high priotity
 * - #define CAN_NUM_RXBUFFERS the number of receive message buffers to be created. A
 *                      larger number of buffers will reduce the chance of missing
 *                      messages but will need to be balanced with the amount of 
 *                      RAM available.
 * - #define CAN_NUM_TXBUFFERS the number of transmit buffers to be created. Fewer 
 *                      transmit buffers will be needed then receive buffers, 
 *                      the timedResponse mechanism means that 4 or fewer buffers
 *                      should be sufficient.
 * 
 * 
 */

extern const Service canService;
extern const Transport canTransport;

#define NUM_CAN_DIAGNOSTICS 16      ///< The number of diagnostic values associated with this service
#define CAN_DIAG_RX_ERRORS          0x00 ///< CAN RX error counter
#define CAN_DIAG_TX_ERRORS          0x01 ///< CAN TX error counter
#define CAN_DIAG_STATUS             0x02 ///< last CAN status byte (TBA) 
#define CAN_DIAG_TX_BUFFER_USAGE    0x03 ///< Tx buffer usage count
#define CAN_DIAG_TX_BUFFER_OVERRUN  0x04 ///< Tx buffer overrun count
#define CAN_DIAG_TX_MESSAGES        0x05 ///< TX message count
#define CAN_DIAG_RX_BUFFER_USAGE    0x06 ///< RX buffer usage count 
#define CAN_DIAG_RX_BUFFER_OVERRUN  0x07 ///< RX buffer overrun count
#define CAN_DIAG_RX_MESSAGES        0x08 ///< RX message counter 
#define CAN_DIAG_ERROR_FRAMES_DET   0x09 ///< CAN error frames detected 
#define CAN_DIAG_ERROR_FRAMES_GEN   0x0A ///< CAN error frames generated (both active and passive ?)
#define CAN_DIAG_LOST_ARRBITARTAION 0x0B ///< Number of times CAN arbitration was lost
#define CAN_DIAG_CANID_ENUMS        0x0C ///< number of CANID enumerations 
#define CAN_DIAG_CANID_CONFLICTS    0x0D ///< number of CANID conflicts detected
#define CAN_DIAG_CANID_CHANGES      0x0E ///< Number of CANID changes
#define CAN_DIAG_CANID_ENUMS_FAIL   0x0F ///< Number of CANID enumeration failures


/**
 * The default value of the CANID.
 */
#define CANID_DEFAULT       1
#define ENUMERATION_TIMEOUT HUNDRED_MILI_SECOND     ///< Wait time for enumeration responses before setting canid
#define ENUMERATION_HOLDOFF 2 * HUNDRED_MILI_SECOND ///< Delay afer receiving conflict before initiating our own self enumeration
#define MAX_CANID           0x7F
#define ENUM_ARRAY_SIZE     (MAX_CANID/8)+1         // Size of array for enumeration results
#define LARB_RETRIES    10                          // Number of retries for lost arbitration
#define CAN_TX_TIMEOUT  ONE_SECOND                  ///< Time for CAN transmit timeout (will resolve to one second intervals due to timer interrupt period)

typedef enum CanidResult {
    CANID_FAIL,
    CANID_OK
} CanidResult;

#ifdef _PIC18
    #define TXBnIE      PIE5bits.TXBnIE
    #define TXBnIF      PIR5bits.TXBnIF
    #define ERRIE       PIE5bits.ERRIE
    #define ERRIF       PIR5bits.ERRIF
    #define FIFOWMIE    PIE5bits.FIFOWMIE
    #define FIFOWMIF    PIR5bits.FIFOWMIF
    #define RXBnIF      PIR5bits.RXBnIF
    #define IRXIF       PIR5bits.IRXIF
    #define RXBnOVFL    COMSTATbits.RXB1OVFL
#else
    #define TXBnIE      PIE3bits.TXBnIE
    #define TXBnIF      PIR3bits.TXBnIF
    #define ERRIE       PIE3bits.ERRIE
    #define ERRIF       PIR3bits.ERRIF
    #define FIFOWMIE    PIE3bits.FIFOWMIE
    #define FIFOWMIF    PIR3bits.FIFOWMIF
    #define RXBnIF      PIR3bits.RXBnIF
    #define IRXIF       PIR3bits.IRXIF
    #define RXBnOVFL    COMSTATbits.RXBnOVFL
#endif
#endif
