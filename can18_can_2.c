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
 * Implementation of the VLCB CAN Service for the CAN2 peripheral on the Q series PIC family.
 * @details
 * Uses Controller Area Network to carry VLCB messages.
 * This implementation works with the PIC18 CAN_2.0 peripheral.
 * The service definition object is called canService.
 * The transport interface is called canTransport.
 * Performs self enumeration and CANID collision detection and re-enumeration.
 * Performs loopback of events for Consumes Own Event behaviour.
 * Collects diagnostic data to aid communications fault finding.
 * 
 * The CAN_2.0 peripheral has a TXQ and a TX FIFO. Here we don't use the TXQ since
 * frames are sent in ID (CANID) order which is not what is wanted for VLCB. We do
 * use the FIFO as frames are sent in order. 
 * We actually use 3 TX FIFOs. Two high priority FIFOs for sending self enumeration 
 * frames and a low priority FIFO for sending normal VLCB data frames.
 * 
 * We set up 1 RX Filters: One filter for all Normal (non extended frames) to 
 * put these frames into a RX FIFO.
 */
#if defined(_18FXXQ83_FAMILY_)
#include <xc.h>
#include <string.h> // for memcpy
#include "vlcb.h"
#include "module.h"
#include "can.h"
#include "mns.h"

#include "nvm.h"
#include "ticktime.h"
#include "messageQueue.h"

/* The VLCB CAN service (can_service.c) on the Q83 controller layer
 * (can_hw_q83.c), or on the application's own controller layer when module.h
 * defines CAN_HW_APP. See can_hw.h. */
#ifndef CAN_HW_APP
#include "can_hw_q83.c"
#endif
#include "can_service.c"
#endif
