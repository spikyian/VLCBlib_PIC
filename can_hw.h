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
#ifndef _CAN_HW_H_
#define _CAN_HW_H_

/**
 * @file
 * @brief
 * The interface between the shared VLCB CAN service (can_service.c) and a CAN
 * controller (can_hw_q83.c, can_hw_k80.c, or one the application supplies).
 * @details
 * Opt-in: used only when module.h defines CAN_SHARED_SERVICE. Without it,
 * can18_can_2.c and can18_ecan.c compile exactly as before and nothing here
 * is used.
 *
 * Everything VLCB says about CAN - the CANID, self-enumeration, conflict
 * detection, ENUM and CANID, the diagnostics, the ESD and the Transport - is
 * in can_service.c, once, for every controller. A controller file only moves
 * frames and reports what the hardware counts. Fixes and enhancements to the
 * protocol are therefore made once and reach every platform.
 *
 * The controller file is included by the family's existing driver file
 * (can18_can_2.c for the Q83, can18_ecan.c for the K80), so an application
 * opts in with one #define and no change to its project. An application with
 * a controller of its own (a bridge, say) also defines CAN_HW_APP and provides
 * these functions itself; can18_can_2.c then builds only the service.
 *
 * All functions are called from the main loop only, except where noted.
 */

#include "vlcb.h"
#include "can.h"

/** A received frame, as the controller presents it to the service. */
typedef struct CanRxFrame {
    uint8_t canId;                  ///< CANID: bits 0-6 of the standard identifier
    uint8_t flags;                  ///< CAN_RX_RTR, CAN_RX_EXT
    uint8_t dlc;                    ///< data length, 0..8
    const volatile uint8_t * data;  ///< dlc bytes; valid until canHwRxRelease()
} CanRxFrame;

#define CAN_RX_RTR  0x01            ///< remote transmission request (another node is enumerating)
#define CAN_RX_EXT  0x02            ///< extended (29-bit) identifier - not VLCB, discarded

/**
 * Start the controller: pins, bit timing, buffers, filters and interrupts.
 * The reply to another node's RTR is prepared with canId.
 */
void canHwInit(uint8_t canId);

/**
 * Look at the oldest received frame without removing it.
 * @return TRUE and fills f if a frame is waiting, FALSE if none
 */
Boolean canHwRxPeek(CanRxFrame * f);

/** Remove the frame canHwRxPeek() returned. */
void canHwRxRelease(void);

/** @return TRUE if a data frame can be queued for transmission. */
Boolean canHwTxSpace(void);

/**
 * Queue a VLCB data frame. The controller maps the opcode's priority
 * (priorities[]) to its identifier format.
 * @param m the message
 * @param canId the CANID to send it with
 * @param hold TRUE: queue it but do not start transmission (the service has
 *        no CANID yet); canHwTxRelease() starts it
 */
void canHwTxData(const Message * m, uint8_t canId, Boolean hold);

/** Stamp every held frame with canId and start transmitting them. */
void canHwTxRelease(uint8_t canId);

/** @return TRUE if frames are queued and not yet started (held). */
Boolean canHwTxHeld(void);

/** The RTR and RTR reply frames carry this CANID from now on. */
void canHwSetCanId(uint8_t canId);

/** Send an RTR with canId: start a self-enumeration. */
void canHwSendRtr(uint8_t canId);

/** Send the zero-length reply to another node's RTR. */
void canHwSendRtrReply(void);

/** Wait until every queued data frame has gone (Transport waitForTxQueueToDrain). */
TxDrainResult canHwWaitTxDrain(void);

#ifdef VLCB_SERVICE
/** The ESD hardware type byte, e.g. CAN_HW_PIC_CAN_2_0. */
uint8_t canHwEsdType(void);
#endif

#ifdef VLCB_DIAG
/**
 * The service's diagnostics. The controller counts the events only it can see
 * (overruns, lost arbitration, bus errors) directly into this array.
 */
extern DiagnosticVal canServiceDiagnostics[NUM_CAN_DIAGNOSTICS+1];
/** @return the number of transmit buffers in use */
uint8_t canHwTxInUse(void);
/** @return the number of receive buffers in use */
uint8_t canHwRxInUse(void);
/**
 * Bring a hardware-sourced diagnostic up to date before it is reported:
 * CAN_DIAG_STATUS, CAN_DIAG_TX_ERRORS, CAN_DIAG_RX_ERRORS.
 */
void canHwDiagRefresh(uint8_t index);
#endif

#if defined(_18F66K80_FAMILY_)
/** The controller's interrupt handler (K80: called from the service's ISR hooks). */
void canHwIsr(void);
#endif

#endif /* _CAN_HW_H_ */
