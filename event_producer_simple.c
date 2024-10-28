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
 * @date Oct 2024
 * 
 */ 

/**
 * @file
 * @brief
 * Implementation of the VLCB Event Producer Service.
 * @details
 * Handle the production of events.
 */

#include <xc.h>
#include "module.h"
#include "vlcb.h"
#include "event_teach.h"
#include "event_producer.h"
#include "mns.h"

extern Boolean validStart(uint8_t tableIndex);

// Forward function declarations
static Processed producerProcessMessage(Message *m);
#ifdef VLCB_DIAG
static void producerPowerUp(void);
static DiagnosticVal * producerGetDiagnostic(uint8_t index);
static DiagnosticVal producerDiagnostics[NUM_PRODUCER_DIAGNOSTICS+1];
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
    for (i=1; i <= NUM_PRODUCER_DIAGNOSTICS; i++) {
        producerDiagnostics[i].asUint = 0;
    }
    producerDiagnostics[PRODUCER_DIAG_COUNT].asUint = NUM_PRODUCER_DIAGNOSTICS;
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

            if (m->opc == OPC_AREQ) {
                if (APP_GetEventIndexState(index) == EVENT_ON) {
                    sendMessage4(OPC_ARON, m->bytes[0], m->bytes[1], m->bytes[2], m->bytes[3]);
                } else {
                    sendMessage4(OPC_AROF, m->bytes[0], m->bytes[1], m->bytes[2], m->bytes[3]);
                }
            } else {
                if (APP_GetEventIndexState(index) == EVENT_ON) {
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
    if (index > NUM_PRODUCER_DIAGNOSTICS) {
        return NULL;
    }
    return &(producerDiagnostics[index]);
}

/**
 * Bump the number produced diagnostic counter.
 */
void incrementProducerCounter() {
    producerDiagnostics[PRODUCER_DIAG_NUMPRODUCED].asUint++;
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

