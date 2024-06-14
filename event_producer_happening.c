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
 * Implementation of the VLCB Event Producer Service.
 * @details
 * Handle the production of events.
 * If EVENT_HASH_TABLE is defined then an additional lookup table 
 * uint8_t action2Event[NUM_HAPPENINGS] is used to obtain an Event 
 * using a Happening stored in the first EVs. This table is populated using 
 * rebuildHashTable(). 
 * The function Boolean sendProducedEvent(Happening happening, EventState onOff)
 * is used to transmit an event lokked up using a Happening.
 * Given a Happening this table can be used to obtain the 
 * index into the EventTable for the Happening so the Event at that index in the 
 * EventTable can be transmitted.
 */

#include <xc.h>
#include "module.h"
#include "vlcb.h"
#include "event_teach.h"
#include "event_producer.h"
#include "mns.h"
#include "event_teach_large.h"

extern Boolean validStart(uint8_t tableIndex);

// Forward function declarations
static Processed producerProcessMessage(Message *m);
#ifdef VLCB_DIAG
static void producerPowerUp(void);
static DiagnosticVal * producerGetDiagnostic(uint8_t index);
static DiagnosticVal producerDiagnostics[NUM_PRODUCER_DIAGNOSTICS];
#endif
static uint8_t producerEsdData(uint8_t id);

/**
 * The service descriptor for the event producer service. The application must include this
 * descriptor within the const Service * const services[] array and include the
 * necessary settings within module.h in order to make use of the event producer
 * service.
 */
const Service eventProducerService = {
    SERVICE_ID_PRODUCER,// id
    1,                  // version
    NULL,               // factoryReset
#ifdef VLCB_DIAG
    producerPowerUp,    // powerUp
#else
    NULL,               // powerUp
#endif
    producerProcessMessage,  // processMessage
    NULL,               // poll
#if defined(_18F66K80_FAMILY_)
    NULL,               // highIsr
    NULL,               // lowIsr
#endif
#ifdef VLCB_SERVICE
    producerEsdData,               // Get ESD data
#endif
#ifdef VLCB_DIAG
    producerGetDiagnostic                // getDiagnostic
#endif
};

#ifdef VLCB_DIAG
static void producerPowerUp(void) {
    uint8_t i;
    for (i=0; i<NUM_PRODUCER_DIAGNOSTICS; i++) {
        producerDiagnostics[i].asInt = 0;
    }
}
#endif

/**
 * Process and events associated with the eventProduction service.
 * This handles the processing of the AREQ and ASRQ requests.
 * 
 * @param m the received message
 * @return PROCESSED if the message was handled by this function
 */
static Processed producerProcessMessage(Message *m) {
    uint8_t index;
    Happening h;
    int16_t ev;
    
    switch (m->opc) {
        case OPC_AREQ:
        case OPC_ASRQ:
            if (m->len < 5) {
                sendMessage3(OPC_CMDERR, nn.bytes.hi, nn.bytes.lo, CMDERR_INV_CMD);
                return PROCESSED;
            }
            if (m->opc == OPC_AREQ) {
                index = findEvent((uint16_t)((m->bytes[0]<<8)|(m->bytes[1])), (uint16_t)((m->bytes[2]<<8)|(m->bytes[3])));
            } else {
                index = findEvent(0, (uint16_t)((m->bytes[2]<<8)|(m->bytes[3])));
            }
            if (index == NO_INDEX) return PROCESSED;
            // now get the happening
            ev = getEv(index, 0);
            if (ev <= 0) return PROCESSED;
#if HAPPENING_SIZE == 1
            h = (uint8_t)ev;
#endif
#if HAPPENING_SIZE == 2
            h.bytes.hi = (uint8_t)ev;
            ev = getEv(index, 1);
            if (ev < 0) return PROCESSED;
            h.bytes.lo = (uint8_t)ev;
#endif
            if (m->opc == OPC_AREQ) {
                if (APP_GetEventState(h) == EVENT_ON) {
                    sendMessage4(OPC_ARON, m->bytes[0], m->bytes[1], m->bytes[2], m->bytes[3]);
                } else {
                    sendMessage4(OPC_AROF, m->bytes[0], m->bytes[1], m->bytes[2], m->bytes[3]);
                }
            } else {
                if (APP_GetEventState(h) == EVENT_ON) {
                    sendMessage4(OPC_ARSON, nn.bytes.hi, nn.bytes.lo, m->bytes[2], m->bytes[3]);
                } else {
                    sendMessage4(OPC_ARSOF, nn.bytes.hi, nn.bytes.lo, m->bytes[2], m->bytes[3]);
                }
            }
            return PROCESSED;
        default:
            break;
    }
    return NOT_PROCESSED;
}

#ifdef VLCB_DIAG
/**
 * Provide the means to return the diagnostic data.
 * @param index the diagnostic index
 * @return a pointer to the diagnostic data or NULL if the data isn't available
 */
static DiagnosticVal * producerGetDiagnostic(uint8_t index) {
    if ((index<1) || (index>NUM_PRODUCER_DIAGNOSTICS)) {
        return NULL;
    }
    return &(producerDiagnostics[index-1]);
}
#endif

#ifdef VLCB_SERVICE
/**
 * Return the service extended definition bytes.
 * @param id identifier for the extended service definition data
 * @return the ESD data
 */
static uint8_t producerEsdData(uint8_t index) {
    switch (index){
        case 0:
            return PRODUCER_EV_HAPPENING;
        case 1:
            return HAPPENING_SIZE;
        default:
            return 0;
    }
}
#endif

/**
 * Get the Produced Event to transmit for the specified action.
 * If the same produced action has been provisioned for more than 1 event
 * only the first provisioned event will be returned.
 * The Happening is assumed to be in the first one or two EVs depending upon the
 * HAPPENING_SIZE defined in module.h.
 * 
 * @param happening used to lookup the event to be sent
 * @param onOff TRUE for an ON event, FALSE for an OFF event
 * @return TRUE if the produced event is found FALSE otherwise
 */
Boolean sendProducedEvent(Happening happening, EventState onOff) {
    Word producedEventNN;
    Word producedEventEN;
    uint8_t opc;
#ifndef EVENT_HASH_TABLE
    uint8_t tableIndex;
#endif

#ifdef EVENT_HASH_TABLE
    if (happening2Event[happening] == NO_INDEX) return FALSE;
    producedEventNN.word = getNN(happening2Event[happening]);
    producedEventEN.word = getEN(happening2Event[happening]);
#else
    for (tableIndex=0; tableIndex < NUM_EVENTS; tableIndex++) {
        if (validStart(tableIndex)) {
            Happening h;
            int16_t ev; 
#if HAPPENING_SIZE == 2
            ev = getEv(tableIndex, 0);
            if (ev < 0) continue;
            h.bytes.hi = (uint8_t) ev;
            ev = getEv(tableIndex, 1);
            if (ev < 0) continue;
            h.bytes.lo = (uint8_t) ev;
            if (h.word == happening.word) {
#endif
#if HAPPENING_SIZE ==1
            ev = getEv(tableIndex, 0);
            if (ev < 0) continue;
            h = (uint8_t) ev;
            if (h == happening) {
#endif
                producedEventNN.word = getNN(tableIndex);
                producedEventEN.word = getEN(tableIndex);
#endif
                if (producedEventNN.word == 0) {
                    // Short event
                    if (onOff == EVENT_ON) {
                        opc = OPC_ASON;
                    } else {
                        opc = OPC_ASOF;
                    }
                    producedEventNN.word = nn.word;
                } else {
                    // Long event
                    if (onOff == EVENT_ON) {
                        opc = OPC_ACON;
                    } else {
                        opc = OPC_ACOF;
                    }
                }
                sendMessage4(opc, producedEventNN.bytes.hi, producedEventNN.bytes.lo, producedEventEN.bytes.hi, producedEventEN.bytes.lo);
#ifdef VLCB_DIAG
                producerDiagnostics[PRODUCER_DIAG_NUMPRODUCED].asUint++;
#endif
                return TRUE;
#ifndef EVENT_HASH_TABLE
            }
        }
    }
#endif
    return FALSE;
}

/**
 * Delete all occurrences of a range of Happenings.
 * @param happening the start of the Happening range
 * @param number the number of Happening in the range
 */
void deleteHappeningRange(Happening happening, uint8_t number) {
    uint8_t tableIndex;
    for (tableIndex=0; tableIndex < NUM_EVENTS; tableIndex++) {
        if ( validStart(tableIndex)) {
            EventTableFlags f;
            Happening h;
            f.asByte = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, 
                    EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_FLAGS);
            h = (Happening)readNVM(EVENT_TABLE_NVM_TYPE, 
                    EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_EVS);
            if ((h >= happening) && (h < happening+number)) {
                writeEv(tableIndex, 0, EV_FILL);
                checkRemoveTableEntry(tableIndex);
            }                
        }
    }
    flushFlashBlock();
    rebuildHashtable();
}
