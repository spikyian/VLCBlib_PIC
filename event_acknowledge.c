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
 * Implementation of the VLCB EventAcknowledge service.
 * @details
 * This service will send a ENACK message if the module has been taught to
 * consume the received event. The module must be in EVENT ACK mode. 
 * The service definition object is called eventAckService.
 */

static Processed ackEventProcessMessage(Message * m);

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
    NULL,               // powerUp
    ackEventProcessMessage,                // processMessage
    NULL,               // poll
    NULL,               // highIsr
    NULL,               // lowIsr
    NULL,               // Get ESD data
    NULL                // getDiagnostic
};

/**
 *  This only provides the functionality for event acknowledge.
 */
static Processed ackEventProcessMessage(Message * m) {
    Word eventNN, eventEN;
    uint8_t eventIndex;
    int16_t ev;
    
    // Check that the Event Ack mode is set
    if (mode != MODE_EVENT_ACK) {
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
        ev = getEv(eventIndex, HAPPENING_SIZE); // skip over the Happening EVs to the first Action
        if (ev >= 0) {
            // sent the ack
            sendMessage7(OPC_ENACK, nn.bytes.hi, nn.bytes.lo, m->opc, m->bytes[0], m->bytes[1], m->bytes[2], m->bytes[3]);
        }
    }
    return NOT_PROCESSED;
}
