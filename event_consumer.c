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
#include "event_consumer.h"
#include "event_teach.h"
/**
 * @file
 * Implementation of the VLCB Event Consumer service.
 * @details
 * The service definition object is called eventConsumerService.
 * Process consumed events. Process Long and Short events.
 * Also handles events with data bytes if HANDLE_DATA_EVENTS is defined. The data is ignored.
 * If COMSUMER_EVS_AS_ACTIONS is defined then EVs after the Happening are treated
 * as Actions and are added to an Action queue to be processed by the application.
 */

static DiagnosticVal consumerDiagnostics[NUM_CONSUMER_DIAGNOSTICS];
static void consumerPowerUp(void);
static Processed consumerProcessMessage(Message * m);
static DiagnosticVal * consumerGetDiagnostic(uint8_t index); 
Boolean pushAction(Action a);
        
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
    consumerPowerUp,               // powerUp
    consumerProcessMessage,               // processMessage
    NULL,               // poll
#if defined(_18F66K80_FAMILY_)
    NULL,               // highIsr
    NULL,               // lowIsr
#endif
    NULL,               // Get ESD data
    consumerGetDiagnostic                // getDiagnostic
};

#ifdef COMSUMER_EVS_AS_ACTIONS
static Action actionQueue[ACTION_QUEUE_SIZE];
static uint8_t areader;
static uint8_t awriter;
#endif

static void consumerPowerUp(void) {
#ifdef COMSUMER_EVS_AS_ACTIONS
    areader = 0;
    awriter = 0;
#endif
}

/**
 * Process consumed events. Process Long and Short events.
 * Also handles events with data bytes if HANDLE_DATA_EVENTS is defined. The data is ignored.
 * If COMSUMER_EVS_AS_ACTIONS is defined then EVs after the Happening are treated
 * as Actions and are added to an Action queue to be processed by the application.
 * 
 * @param m received message
 * @return PROCESSED if the message needs no further processing
 */
static Processed consumerProcessMessage(Message *m) {
    uint8_t start, end;
    uint8_t tableIndex;
    int8_t change;
    uint8_t e;
    Action a;
    
    if (m->len < 5) return NOT_PROCESSED;
    
    switch (m->opc) {
        case OPC_ACON:
        case OPC_ACOF:
#ifdef HANDLE_DATA_EVENTS
        case OPC_ACON1:
        case OPC_ACOF1:
        case OPC_ACON2:
        case OPC_ACOF2:
        case OPC_ACON3:
        case OPC_ACOF3:
#endif
            tableIndex = findEvent(((uint16_t)m->bytes[0])*256+m->bytes[1], ((uint16_t)m->bytes[2])*256+m->bytes[3]);
            if (tableIndex == NO_INDEX) return NOT_PROCESSED;
            
            break;
        case OPC_ASON:
        case OPC_ASOF:
#ifdef HANDLE_DATA_EVENTS
        case OPC_ASON1:
        case OPC_ASOF1:
        case OPC_ASON2:
        case OPC_ASOF2:
        case OPC_ASON3:
        case OPC_ASOF3:
#endif
            tableIndex = findEvent(((uint16_t)m->bytes[0])*256+m->bytes[1], ((uint16_t)m->bytes[2])*256+m->bytes[3]);
            if (tableIndex == NO_INDEX) return NOT_PROCESSED;
            
            break;
        default:
            return NOT_PROCESSED;
    }
#ifdef COMSUMER_EVS_AS_ACTIONS
    switch (m->opc) {
        case OPC_ACON:
        case OPC_ACON1:
        case OPC_ACON2:
        case OPC_ACON3:
        case OPC_ASON:
        case OPC_ASON1:
        case OPC_ASON2:
        case OPC_ASON3:
            start = HAPPENING_SIZE;
            end = PARAM_NUM_EV_EVENT;
            change = ACTION_SIZE;
            break;
        case OPC_ACOF:
        case OPC_ACOF1:
        case OPC_ACOF2:
        case OPC_ACOF3:
        case OPC_ASOF:
        case OPC_ASOF1:
        case OPC_ASOF2:
        case OPC_ASOF3:
            start = PARAM_NUM_EV_EVENT-ACTION_SIZE;
            end = HAPPENING_SIZE-1;
            change = -ACTION_SIZE;
            break;
        default:
            return NOT_PROCESSED;
    }
    // get the list of actions and add then to the action queue
    for (e=start; e!=end; e+=change) {
        int16_t ev;
        uint8_t evi;
        uint8_t validEv = 1;
        
        for (evi=0; evi<ACTION_SIZE; evi++) {   // build up the Action from the EVs
            ev = getEv(tableIndex, e+evi);
            if (ev < 0) {
                validEv = 0;               
                break;                          // skip if invalid EV
            }
            a.a.bytes[evi] = (uint8_t)ev;
        }
        if (validEv) {                          // Successfully got an Action
            a.state = (change>0);
            pushAction(a);
        }
    }
#else
    APP_processConsumedEvent(tableIndex, m);
#endif
    consumerDiagnostics[CONSUMER_DIAG_NUMCONSUMED].asUint++;
    return NOT_PROCESSED;
}


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

/**
 * Push a message onto the Action queue.
 * @param a the Action
 * @return TRUE for success FALSE for buffer full
 */
Boolean pushAction(Action a) {
    if (((awriter+1)&(ACTION_QUEUE_SIZE-1)) == areader) return FALSE;	// buffer full
    actionQueue[awriter++] = a;
    if (awriter >= ACTION_QUEUE_SIZE) awriter = 0;
    return TRUE;
}


/**
 * Pull the next Action from the queue.
 * If COMSUMER_EVS_AS_ACTIONS is defined in module.h then the event's EVs will be
 * treated as Actions and will be pushed onto the Action queue. The application 
 * should use popAction() to obtain the next Action for processing.
 *
 * @return the next action of NULL if the queue was empty
 */
Action * popAction(void) {
    Action * ret;
	if (awriter == areader) {
        return NULL;	// buffer empty
    }
	ret = &(actionQueue[areader++]);
	if (areader >= ACTION_QUEUE_SIZE) areader = 0;
	return ret;
}
