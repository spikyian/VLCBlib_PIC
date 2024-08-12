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
 * @date May 2024
 * 
 */ 

#include <xc.h>
#include "vlcb.h"
#include "event_consumer_simple.h"
#include "event_teach.h"

/**
 * @file
 * @brief
 * Implementation of the VLCB Event Consumer service.
 * @details
 * The service definition object is called eventConsumerService.
 * Process consumed events. Process Long and Short events.
 * Also handles events with data bytes if HANDLE_DATA_EVENTS is defined. The data is ignored.
 */

static DiagnosticVal consumerDiagnostics[NUM_CONSUMER_DIAGNOSTICS];
static Processed consumerProcessMessage(Message * m);
static DiagnosticVal * consumerGetDiagnostic(uint8_t index); 
static uint8_t consumerEsdData(uint8_t index);

extern uint8_t APP_isConsumedEvent(uint8_t eventIndex);
        
/**
 * The service descriptor for the eventConsumer service. The application must include this
 * descriptor within the const Service * const services[] array and include the
 * necessary settings within module.h in order to make use of the event consumer
 * service.
 */
const Service eventConsumerService = {
    SERVICE_ID_CONSUMER,// id
    1,                  // version
    NULL,               // factoryReset
    NULL,               // powerUp
    consumerProcessMessage,               // processMessage
    NULL,               // poll
#if defined(_18F66K80_FAMILY_)
    NULL,               // highIsr
    NULL,               // lowIsr
#endif
#ifdef VLCB_SERVICE
    consumerEsdData,               // Get ESD
#endif
#ifdef VLCB_DIAG
    consumerGetDiagnostic                // getDiagnostic
#endif
};

/**
 * Process consumed events. Process Long and Short events.
 * Also handles events with data bytes if HANDLE_DATA_EVENTS is defined. The data is ignored.
 * 
 * @param m received message
 * @return PROCESSED if the message needs no further processing
 */
static Processed consumerProcessMessage(Message *m) {
    Processed ret;
    uint8_t tableIndex;
    uint16_t enn;
    
    if (m->len < 5) return NOT_PROCESSED;
    
    enn = ((uint16_t)m->bytes[0])*256+m->bytes[1];

    switch (m->opc) {
        case OPC_ASON:
#ifdef HANDLE_DATA_EVENTS
        case OPC_ASON1:
        case OPC_ASON2:
        case OPC_ASON3:
#endif
		enn = 0;
		// fall through
        case OPC_ACON:
#ifdef HANDLE_DATA_EVENTS
        case OPC_ACON1:
        case OPC_ACON2:
        case OPC_ACON3:
#endif
        case OPC_ASOF:
#ifdef HANDLE_DATA_EVENTS
        case OPC_ASOF1:
        case OPC_ASOF2:
        case OPC_ASOF3:
#endif
		enn = 0;
		// fall through
        case OPC_ACOF:
#ifdef HANDLE_DATA_EVENTS
        case OPC_ACOF1:
        case OPC_ACOF2:
        case OPC_ACOF3:
#endif
            break;
        default:
            return NOT_PROCESSED;
    }

    tableIndex = findEvent(enn, ((uint16_t)m->bytes[2])*256+m->bytes[3]);
    if (tableIndex == NO_INDEX) return NOT_PROCESSED;

    ret = APP_processConsumedEvent(tableIndex, m);
    if (ret == PROCESSED) {
        consumerDiagnostics[CONSUMER_DIAG_NUMCONSUMED].asUint++;
    }
    return ret;
}

/**
 * This simple service doesn't know if the event is consumed so hand off to the
 * application to determine.
 * @param eventIndex
 * @return true if the event is a consumed event
 */
uint8_t isConsumedEvent(uint8_t eventIndex) {
    return APP_isConsumedEvent(eventIndex);
}

#ifdef VLCB_DIAG
/**
 * Provide the means to return the diagnostic data.
 * @param index the diagnostic index
 * @return a pointer to the diagnostic data or NULL if the data isn't available
 */
static DiagnosticVal * consumerGetDiagnostic(uint8_t index) {
    if ((index<1) || (index>NUM_CONSUMER_DIAGNOSTICS)) {
        return NULL;
    }
    return &(consumerDiagnostics[index-1]);
}
#endif

#ifdef VLCB_SERVICE
/**
 * Return the service extended definition bytes.
 * @param id identifier for the extended service definition data
 * @return the ESD data
 */
static uint8_t consumerEsdData(uint8_t index) {
    switch (index){
        case 0:
            return CONSUMER_EV_NOT_SPECIFIED;
        default:
            return 0;
    }
}
#endif
