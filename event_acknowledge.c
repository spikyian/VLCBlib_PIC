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
#include "event_acknowledge.h"
#include "event_teach.h"
#include "mns.h"

/**
 * @file
 * @brief
 * Implementation of the VLCB EventAcknowledge Service.
 * @details
 * This service will send a ENACK message if the module has been taught to
 * consume the received event. The module must be in EVENT ACK mode. 
 * The service definition object is called eventAckService.
 */
static void ackPowerUp(void);
static Processed ackEventProcessMessage(Message * m);
static Processed ackEventCheckLen(Message * m, uint8_t needed);
#ifdef VLCB_DIAG
static DiagnosticVal * ackGetDiagnostic(uint8_t code);
static DiagnosticVal ackDiagnostics[NUM_ACK_DIAGNOSTICS+1];
#endif

extern uint8_t isConsumedEvent(uint8_t eventIndex);

/**
 * The service descriptor for the Event Acknowledge service. The application must include this
 * descriptor within the const Service * const services[] array and include the
 * necessary settings within module.h in order to make use of the PIC event acknowledgement
 * service.
 */
const Service eventAckService = {
    SERVICE_ID_EVENTACK,// id
    1,                  // version
    NULL,               // factoryReset
#ifdef VLCB_DIAG
    ackPowerUp,         // powerUp
#else
    NULL,
#endif
    ackEventProcessMessage,                // processMessage
    NULL,               // poll
#if defined(_18F66K80_FAMILY_)
    NULL,               // highIsr
    NULL,               // lowIsr
#endif
#ifdef VLCB_SERVICE
    NULL,               // Get ESD data
#endif
#ifdef VLCB_DIAG
    ackGetDiagnostic    // getDiagnostic
#endif
};

#ifdef VLCB_DIAG
static void ackPowerUp(void) {
    uint8_t i;
    
    // Clear the diagnostics
    for (i=1; i<= NUM_ACK_DIAGNOSTICS; i++) {
        ackDiagnostics[i].asUint = 0;
    }
    ackDiagnostics[ACK_DIAG_COUNT].asUint = NUM_ACK_DIAGNOSTICS;
}
#endif

/**
 * This only provides the functionality for event acknowledge.
 */
static Processed ackEventProcessMessage(Message * m) {
    Word eventNN, eventEN;
    uint8_t eventIndex;
    int16_t ev;
    
#ifdef VLCB_MODE
    if (m->opc == OPC_MODE) {      // 76 MODE - NN, mode
        if (ackEventCheckLen(m, 4) == PROCESSED) return PROCESSED;
        if ((m->bytes[0] == nn.bytes.hi) && (m->bytes[1] == nn.bytes.lo)) {
            if (m->bytes[2] == MODE_EVENT_ACK_ON) {
                // Do enter Learn mode
                mode_flags |= FLAG_MODE_LEARN;
                return PROCESSED;
            } else if (m->bytes[2] == MODE_EVENT_ACK_OFF) {
                // Do exit Learn mode
                mode_flags &= ~FLAG_MODE_LEARN;
                return PROCESSED;
            }
        } 
        return NOT_PROCESSED;   // mode probably processed by other services
    }
#endif
            
    // Check that the Event Ack mode is set
    if (! (mode_flags & FLAG_MODE_EVENTACK)) {
        return NOT_PROCESSED;
    }
    // check that we have event consumption service
    if (findService(SERVICE_ID_CONSUMER) == NULL) {
        return NOT_PROCESSED;
    }
    if (m->len < 5) {
        return NOT_PROCESSED;
    }
    eventNN.bytes.hi = m->bytes[0];
    eventNN.bytes.lo = m->bytes[1];
    eventEN.bytes.hi = m->bytes[2];
    eventEN.bytes.lo = m->bytes[3];
    
    switch (m->opc) {
        case OPC_ACON:
        case OPC_ACOF:
            // Long event
            eventIndex = findEvent(eventNN.word, eventNN.word);
            break;
        case OPC_ASON:
        case OPC_ASOF:
            // Short event
            eventIndex = findEvent(0, eventNN.word);
            break;
        default:
            return NOT_PROCESSED;
    }
    if (eventIndex != NO_INDEX) {
        // we have the event in the event table
        // check that we have a consumed Action
        if (isConsumedEvent(eventIndex)) {
            // sent the ack
            sendMessage7(OPC_ENACK, nn.bytes.hi, nn.bytes.lo, (uint8_t)(m->opc), m->bytes[0], m->bytes[1], m->bytes[2], m->bytes[3]);
#ifdef VLCB_DIAG
            ackDiagnostics[ACK_DIAG_NUM_ACKED].asInt++;
#endif
        }
    }
    return NOT_PROCESSED;
}

/**
 * Check the message length is sufficient for the opcode.
 * @param m the message
 * @param needed the number of bytes needed
 * @return PROCESSED if the message is invalid and should not be processed further
 */
static Processed ackEventCheckLen(Message * m, uint8_t needed) {
    return checkLen(m, needed, SERVICE_ID_EVENTACK);
}

#ifdef VLCB_DIAG
/**
 * Provide the means to return the diagnostic data.
 * @param index the diagnostic index 1..NUM_CAN_DIAGNOSTSICS
 * @return a pointer to the diagnostic data or NULL if the data isn't available
 */
static DiagnosticVal * ackGetDiagnostic(uint8_t index) {
    if (index > NUM_ACK_DIAGNOSTICS) {
        return NULL;
    }
    return &(ackDiagnostics[index]);
}
#endif