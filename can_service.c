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
 * @file
 * @brief
 * The VLCB CAN service and transport, shared by every CAN controller.
 * @details
 * Compiled by can18_can_2.c (Q83) or can18_ecan.c (K80), after their own
 * #includes. The
 * controller is reached only through can_hw.h, so this file has no registers
 * in it and behaves the same on every platform.
 *
 * Provides canService and canTransport exactly as the earlier drivers did, so
 * an application needs no change:
 *   - CANID held in NVM (CANID_NVM_TYPE, CANID_ADDRESS)
 *   - self-enumeration (RTR, collect replies for ENUMERATION_TIMEOUT, take the
 *     lowest free CANID 1..99), and the reply to other nodes' RTRs
 *   - CANID conflict detection (a frame with our CANID) and re-enumeration
 *     after ENUMERATION_HOLDOFF
 *   - CAN_ADDITIONAL_CANID_CHECKS: with no CANID, frames are held, never sent
 *     as CANID 0, and released with the CANID enumeration finds; with none
 *     free, retry after ENUMERATION_RETRY
 *   - ENUM and CANID opcodes (ignored, as before), the 18 CAN diagnostics and
 *     the ESD
 *   - CONSUMED_EVENTS: events this module sends are also received by it
 *
 * Every frame the controller receives passes through canReceiveMessage(), so
 * RTR replies, enumeration results and conflict detection never depend on
 * how the controller buffers frames.
 *
 * Other opt-ins:
 *   CAN_RX_ACCEPT(opc)  - module.h may define it as an expression; a received
 *                         data frame whose opcode it rejects is dropped before
 *                         it is copied or reaches the services. Enumeration
 *                         still sees the frame. It must accept every opcode
 *                         any service in services[] or the application handles
 *                         (MNS, NV, teach, events ...): a rejected opcode is
 *                         never processed.
 *   CAN_VIEW_SERVICE    - also provides canViewService: a read-only CAN service
 *                         answering ESD and diagnostics from the application
 *                         (APP_canViewEsdData, APP_canViewDiagnostic), for a
 *                         module that must report a CAN controller it does not
 *                         drive itself (CanCan's other segment, say).
 */
/* Compiled only through can18_can_2.c / can18_ecan.c;
 * empty if a project builds this file on its own. */
#if defined(_CAN_H_)
#include "can_hw.h"

#ifndef CAN_RX_CONTROL_PER_CALL
#define CAN_RX_CONTROL_PER_CALL 8   ///< RTR, zero-length or dropped frames handled per receive call
#endif

static void canFactoryReset(void);
static void canPowerUp(void);
static void canPoll(void);
static Processed canProcessMessage(Message * m) __reentrant;   /* off the compiled stack - XC8 case 01901775 */
#if defined(_18F66K80_FAMILY_)
static void canIsr(void);
#endif
#ifdef VLCB_SERVICE
static uint8_t canEsdData(uint8_t id);
#endif
#ifdef VLCB_DIAG
static DiagnosticVal * canGetDiagnostic(uint8_t index);
/**
 * The set of diagnostics for the CAN service. Not static: the controller
 * counts hardware events into it (can_hw.h).
 */
DiagnosticVal canServiceDiagnostics[NUM_CAN_DIAGNOSTICS+1];
#endif

/**
 * The service descriptor for the CAN service. The application must include this
 * descriptor within the const Service * const services[] array and include the
 * necessary settings within module.h in order to make use of the CAN
 * service.
 */
const Service canService = {
    SERVICE_ID_CAN,     // id
    2,                  // version
    canFactoryReset,    // factoryReset
    canPowerUp,         // powerUp
    canProcessMessage,  // processMessage
    canPoll,            // poll
#if defined(_18F66K80_FAMILY_)
    canIsr,             // highIsr
    canIsr,             // lowIsr
#endif
#ifdef VLCB_SERVICE
    canEsdData,         // get ESD data
#endif
#ifdef VLCB_DIAG
    canGetDiagnostic    // getDiagnostic
#endif
};

static SendResult canSendMessage(Message * mp);
static MessageReceived canReceiveMessage(Message * m);

/**
 * The transport descriptor for the CAN service. The application must set
 * const Transport * transport to the address of the CAN transport descriptor
 * thus: transport = &canTransport;
 */
const Transport canTransport = {
    canSendMessage,
    canReceiveMessage,
    canHwWaitTxDrain
};

/**
 * The CANID. This is persisted in non-volatile memory.
 */
static uint8_t canId;

#ifdef CONSUMED_EVENTS
/**
 *  Rx buffers used to store self consumed events
 */
static Message rxBuffers[CAN_NUM_RXBUFFERS];
static MessageQueue rxQueue;
#endif

/**
 * Variables for self enumeration of CANID
 */
enum EnumerationState {
    NO_ENUMERATION,
    ENUMERATION_REQUIRED,
    ENUMERATION_IN_PROGRESS,
    ENUMERATION_IN_PROGRESS_TX_WAITING
};
static TickValue  enumerationStartTime;
static enum EnumerationState enumerationState;
#ifdef CAN_ADDITIONAL_CANID_CHECKS
// The last enumeration found no free CANID, so the next one waits
// ENUMERATION_RETRY and a new frame does not start one early.
static Boolean enumerationBackoff;
#ifndef ENUMERATION_RETRY
#define ENUMERATION_RETRY   FIVE_SECOND
#endif
#endif
static uint8_t    enumerationResults[ENUM_ARRAY_SIZE];
#define arraySetBit( array, index ) ( array[index>>3] |= ( 1<<(index & 0x07) ) )

static CanidResult setNewCanId(uint8_t newCanId);
static void startEnumeration(Boolean txWaiting);
static void processEnumeration(void);
static void handleSelfEnumeration(uint8_t receivedCanId);

//
//CAN SERVICE
//
/**
 * Perform the CAN factory reset. Set the CANID to the default.
 */
static void canFactoryReset(void) {
    canId = CANID_DEFAULT;
    writeNVM(CANID_NVM_TYPE, CANID_ADDRESS, canId);
}

/**
 * Do the CAN power up. Get the saved CANID, start the controller.
 */
static void canPowerUp(void) {
    int temp;

#ifdef CONSUMED_EVENTS
    // initialise the RX buffers
    rxQueue.readIndex = 0;
    rxQueue.writeIndex = 0;
    rxQueue.messages = rxBuffers;
    rxQueue.size = CAN_NUM_RXBUFFERS;
#endif

    temp = readNVM(CANID_NVM_TYPE, CANID_ADDRESS);
    if (temp < 0) {
        canId = CANID_DEFAULT;
    } else {
        canId = (uint8_t)temp;
    }
#ifdef VLCB_DIAG
    // clear the diagnostic stats
    for (temp=1; temp <= NUM_CAN_DIAGNOSTICS; temp++) {
        canServiceDiagnostics[temp].asUint = 0;
    }
    canServiceDiagnostics[CAN_DIAG_COUNT].asUint = NUM_CAN_DIAGNOSTICS;
#endif

    canHwInit(canId);

    // Initialise enumeration control variables
    enumerationState = NO_ENUMERATION;
#ifdef CAN_ADDITIONAL_CANID_CHECKS
    enumerationBackoff = FALSE;
#endif
    enumerationStartTime.val = tickGet();
}

/**
 * Process the CAN specific VLCB messages. The VLCB CAN specification
 * describes two opcodes ENUM and CANID but this implementation does not
 * require these as both are handled automatically.
 * @param m the message to be processed
 * @return PROCESSED is the message is processed, NOT_PROCESSED otherwise
 */
static Processed canProcessMessage(Message * m) {
    // check NN matches us
    if (m->len < 3) return NOT_PROCESSED;
    if (m->bytes[0] != nn.bytes.hi) return NOT_PROCESSED;
    if (m->bytes[1] != nn.bytes.lo) return NOT_PROCESSED;

    // Handle any CAN specific OPCs
    switch (m->opc) {
        case OPC_ENUM:
            // ignore request
            return PROCESSED;
        case OPC_CANID:
            if (m->len < 4) {
#ifdef VLCB_GRSP
                sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_CANID, SERVICE_ID_MNS, CMDERR_INV_CMD);
#endif
                return PROCESSED;
            }
            // ignore request
            return PROCESSED;
        default:
            break;
    }
    return NOT_PROCESSED;
}

/**
 * The poll routine continues any self enumeration that is in progress.
 */
static void canPoll(void) {
    processEnumeration();
}

#if defined(_18F66K80_FAMILY_)
/**
 * The K80 controller's interrupts (the core calls every service's ISR hooks).
 */
static void canIsr(void) {
    canHwIsr();
}
#endif

#ifdef VLCB_SERVICE
/**
 * Return the service extended definition bytes.
 * @param id
 * @return the ESD data
 */
static uint8_t canEsdData(uint8_t id) {
    switch(id) {
        case 1:
            return canHwEsdType();
        default:
            return 0;
    }
}
#endif

#ifdef VLCB_DIAG
/**
 * Provide the means to return the diagnostic data.
 * @param index the diagnostic index 1..NUM_CAN_DIAGNOSTSICS
 * @return a pointer to the diagnostic data or NULL if the data isn't available
 */
static DiagnosticVal * canGetDiagnostic(uint8_t index) {
    if (index > NUM_CAN_DIAGNOSTICS) {
        return NULL;
    }
    switch (index) {
        case CAN_DIAG_STATUS:
        case CAN_DIAG_TX_ERRORS:
        case CAN_DIAG_RX_ERRORS:
            canHwDiagRefresh(index);
            break;
        case CAN_DIAG_TX_BUFFER_USAGE:
            canServiceDiagnostics[CAN_DIAG_TX_BUFFER_USAGE].asUint = canHwTxInUse();
            break;
        case CAN_DIAG_RX_BUFFER_USAGE:
            canServiceDiagnostics[CAN_DIAG_RX_BUFFER_USAGE].asUint = canHwRxInUse();
            break;
        default:
            break;
    }
    return &(canServiceDiagnostics[index]);
}
#endif

/*            TRANSPORT INTERFACE             */
/**
 * Send a message on the CAN interface.
 * @param mp the message to be sent
 * @return SEND_OK if the message was queued, SEND_FAILED if the controller was full
 */
static SendResult canSendMessage(Message * mp) {
#ifdef VLCB_DIAG
    uint8_t temp;
#endif
#ifdef CONSUMED_EVENTS
    Message * m;

    // If this is an event we are sending then put it onto the rx queue so
    // we can consume our own events.
    if (isEvent((uint8_t)(mp->opc))) {
        if (have(SERVICE_ID_CONSUME_OWN_EVENTS)) {
            m = getNextWriteMessage(&rxQueue);
            if (m == NULL) {
#ifdef VLCB_DIAG
                canServiceDiagnostics[CAN_DIAG_RX_BUFFER_OVERRUN].asUint++;
                updateModuleErrorStatus();
#endif
            } else {
                m->opc = mp->opc;
                m->len = mp->len;
                m->bytes[0] = mp->bytes[0];
                m->bytes[1] = mp->bytes[1];
                m->bytes[2] = mp->bytes[2];
                m->bytes[3] = mp->bytes[3];
                m->bytes[4] = mp->bytes[4];
                m->bytes[5] = mp->bytes[5];
                m->bytes[6] = mp->bytes[6];
            }
        }
    }
#endif
    // Done if the controller is full
    if (!canHwTxSpace()) {
#ifdef VLCB_DIAG
        canServiceDiagnostics[CAN_DIAG_TX_BUFFER_OVERRUN].asUint++;
        updateModuleErrorStatus();
#endif
        return SEND_FAILED;
    }

#ifndef CAN_ADDITIONAL_CANID_CHECKS
    // start an enumeration on first transmit if we are still using canId=0
    if ((canId == 0) && (enumerationState == NO_ENUMERATION)) {
        enumerationState = ENUMERATION_REQUIRED;
        canId = 1;
    }
#endif

    canHwTxData(mp, canId, (canId == 0) ? TRUE : FALSE);
#ifdef VLCB_DIAG
    canServiceDiagnostics[CAN_DIAG_TX_MESSAGES].asUint++;
    temp = canHwTxInUse();
    if (temp > canServiceDiagnostics[CAN_DIAG_TX_HIGH_WATERMARK].asUint) {
        canServiceDiagnostics[CAN_DIAG_TX_HIGH_WATERMARK].asUint = temp;
    }
#endif
    if (canId == 0) {
        // Not ready to send as we don't yet have a CANID: the frame is held
        // and processEnumeration() releases it with the CANID it finds.
#ifdef CAN_ADDITIONAL_CANID_CHECKS
        if ((enumerationState == NO_ENUMERATION) || ((enumerationState == ENUMERATION_REQUIRED) && !enumerationBackoff)) {
            startEnumeration(TRUE);     // also when a hold-off is pending; but not while backing off
                                        // after a failed enumeration: the frame goes out after the retry
        } else if (enumerationState == ENUMERATION_IN_PROGRESS) {
            enumerationState = ENUMERATION_IN_PROGRESS_TX_WAITING;
        }
#else
        startEnumeration(TRUE);
#endif
    }
    return SEND_OK;
}

/**
 * Return the next received message, if any.
 * Every frame from the controller passes through here: RTRs are answered,
 * enumeration replies collected and conflicts detected whatever the frame.
 * RTR, zero-length and dropped frames do not end the call; up to
 * CAN_RX_CONTROL_PER_CALL of them are handled before giving up.
 * @return RECEIVED if a message was copied to m, NOT_RECEIVED otherwise
 */
static MessageReceived canReceiveMessage(Message * m) {
    CanRxFrame f;
    uint8_t n;
#ifdef VLCB_DIAG
    uint8_t temp;
#endif
#ifdef CONSUMED_EVENTS
    Message * mp;

    // Check for any messages in the software fifo, which will be self-consumed events
    mp = pop(&rxQueue);
    if (mp != NULL) {
        memcpy(m, mp, sizeof(Message));
        return RECEIVED;
    }
#endif
    for (n = 0; n < CAN_RX_CONTROL_PER_CALL; n++) {
        if (!canHwRxPeek(&f)) {
            return NOT_RECEIVED;
        }
#ifdef VLCB_DIAG
        if (n == 0) {
            temp = canHwRxInUse();
            if (temp > canServiceDiagnostics[CAN_DIAG_RX_HIGH_WATERMARK].asUint) {
                canServiceDiagnostics[CAN_DIAG_RX_HIGH_WATERMARK].asUint = temp;
            }
        }
#endif
        if (f.flags & CAN_RX_EXT) {         // not VLCB
            canHwRxRelease();
            continue;
        }
        handleSelfEnumeration(f.canId);
#ifdef VLCB_DIAG
        canServiceDiagnostics[CAN_DIAG_RX_MESSAGES].asUint++;
#endif
        if (f.flags & CAN_RX_RTR) {
            canHwSendRtrReply();            // another node is enumerating
            canHwRxRelease();
            continue;
        }
        if (f.dlc == 0) {                   // an RTR reply: nothing to process
            canHwRxRelease();
            continue;
        }
#ifdef CAN_RX_ACCEPT
        if (!(CAN_RX_ACCEPT(f.data[0]))) {  // the application never handles this opcode
            canHwRxRelease();
            continue;
        }
#endif
        m->len = f.dlc;
        m->opc = (VlcbOpCodes)f.data[0];
        m->bytes[0] = f.data[1];
        m->bytes[1] = f.data[2];
        m->bytes[2] = f.data[3];
        m->bytes[3] = f.data[4];
        m->bytes[4] = f.data[5];
        m->bytes[5] = f.data[6];
        m->bytes[6] = f.data[7];
        canHwRxRelease();
        return RECEIVED;
    }
    return NOT_RECEIVED;
}

/**
 * Start a Self Enumeration.
 * @param txWaiting TRUE if a transmit request without a valid CANID triggered it.
 */
static void startEnumeration(Boolean txWaiting) {
    uint8_t i;

    for (i=1; i< ENUM_ARRAY_SIZE; i++) {
        enumerationResults[i] = 0;
    }
    enumerationResults[0] = 1;  // Don't allocate canid 0

    enumerationState = txWaiting ? ENUMERATION_IN_PROGRESS_TX_WAITING : ENUMERATION_IN_PROGRESS;
    enumerationStartTime.val = tickGet();
#ifdef VLCB_DIAG
    canServiceDiagnostics[CAN_DIAG_CANID_ENUMS].asUint++;
#endif
    canHwSendRtr(canId);    // Send RTR frame to initiate self enumeration
#ifdef VLCB_DIAG
    canServiceDiagnostics[CAN_DIAG_TX_MESSAGES].asUint++;
#endif
}

/**
 * Collect enumeration replies, or detect another module using our CANID.
 * @param receivedCanId the CANID of a received frame.
 */
static void handleSelfEnumeration(uint8_t receivedCanId) {
    switch (enumerationState) {
        case ENUMERATION_IN_PROGRESS:
        case ENUMERATION_IN_PROGRESS_TX_WAITING:
            arraySetBit(enumerationResults, receivedCanId);
            break;
        case NO_ENUMERATION:
            if (receivedCanId == canId) {
                // Another module has our CANID: enumerate after the hold-off
                // (automatic conflict resolution - thanks to Bob V for this idea)
                enumerationState = ENUMERATION_REQUIRED;
#ifdef VLCB_DIAG
                canServiceDiagnostics[CAN_DIAG_CANID_CONFLICTS].asUint++;
#endif
                enumerationStartTime.val = tickGet();
            }
            break;
        default:
            break;
    }
}

/**
 * Check if enumeration pending, if so kick it off providing hold off time has expired.
 * If enumeration complete, find and set new can id.
 */
static void processEnumeration(void) {
    uint8_t i, newCanId, enumResult;

    switch (enumerationState) {
        case ENUMERATION_REQUIRED:
#ifdef CAN_ADDITIONAL_CANID_CHECKS
            // after an enumeration that found no free CANID, wait ENUMERATION_RETRY
            if (tickTimeSince(enumerationStartTime) > (enumerationBackoff ? ENUMERATION_RETRY : ENUMERATION_HOLDOFF)) {
                // with CANID 0, frames may be held in the controller
                enumerationBackoff = FALSE;
                startEnumeration(((canId == 0) && canHwTxHeld()) ? TRUE : FALSE);
            }
#else
            if (tickTimeSince(enumerationStartTime) > ENUMERATION_HOLDOFF ) {
                startEnumeration(FALSE);
            }
#endif
            break;
        case ENUMERATION_IN_PROGRESS:
        case ENUMERATION_IN_PROGRESS_TX_WAITING:
            if (tickTimeSince(enumerationStartTime) > ENUMERATION_TIMEOUT ) {
                // Enumeration complete, find first free canid
                for (i=0; (i < ENUM_ARRAY_SIZE) && (enumerationResults[i] == 0xFF); i++) {
                    ;
                }
                if ((i < ENUM_ARRAY_SIZE) && ((enumResult = enumerationResults[i]) != 0xFF)) {
                    for (newCanId = i*8; (enumResult & 0x01); newCanId++) {
                        enumResult >>= 1;
                    }
                    if ((newCanId >= 1) && (newCanId <= 99)) {
                        setNewCanId(newCanId);
                    } else {
#ifdef VLCB_DIAG
                        canServiceDiagnostics[CAN_DIAG_CANID_ENUMS_FAIL].asUint++;
                        updateModuleErrorStatus();
#endif
                    }
                } else {
#ifdef VLCB_DIAG
                    canServiceDiagnostics[CAN_DIAG_CANID_ENUMS_FAIL].asUint++;
                    updateModuleErrorStatus();
#endif
                }
                // If there are TX messages waiting then release them now
                if (enumerationState == ENUMERATION_IN_PROGRESS_TX_WAITING) {
#ifdef CAN_ADDITIONAL_CANID_CHECKS
                    if (canId == 0) {
                        // no free CANID found - keep the frames held (never send as
                        // CANID 0) and enumerate again after ENUMERATION_RETRY
                        enumerationState = ENUMERATION_REQUIRED;
                        enumerationStartTime.val = tickGet();
                        enumerationBackoff = TRUE;
                        break;
                    }
#endif
                    canHwTxRelease(canId);
                }
                enumerationState = NO_ENUMERATION;
            }
            break;
        default:
            break;
    }
}

/**
 * Set a new can id. Update the diagnostic statistics.
 * @return CANID_OK upon success CANID_FAIL otherwise
 */
static CanidResult setNewCanId(uint8_t newCanId) {
    if ((newCanId >= 1) && (newCanId <= 99)) {
        canId = newCanId;
        canHwSetCanId(canId);
        writeNVM(CANID_NVM_TYPE, CANID_ADDRESS, newCanId);       // Update saved value
#ifdef VLCB_DIAG
        canServiceDiagnostics[CAN_DIAG_CANID_CHANGES].asUint++;
#endif
        return CANID_OK;
    } else {
        return CANID_FAIL;
    }
}

#ifdef CAN_VIEW_SERVICE
/**
 * A read-only CAN service for a controller this module reports but does not
 * drive. The application answers the ESD and the diagnostics, numbered as
 * canService's (CAN_DIAG_*). Add &canViewService to services[] after
 * &canService.
 */
const Service canViewService = {
    SERVICE_ID_CAN,     // id
    2,                  // version
    NULL,               // factoryReset
    NULL,               // powerUp
    NULL,               // processMessage
    NULL,               // poll
#if defined(_18F66K80_FAMILY_)
    NULL,               // highIsr
    NULL,               // lowIsr
#endif
#ifdef VLCB_SERVICE
    APP_canViewEsdData, // get ESD data
#endif
#ifdef VLCB_DIAG
    APP_canViewDiagnostic   // getDiagnostic
#endif
};
#endif
#endif /* _CAN_H_ */
