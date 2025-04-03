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
/*
 *  History for this file:
 *  30/05/24    Ian Hogg        - Created a simple events implementation
 * 
 * Currently written for:
 *  XC8 compiler
 * 
 * This file used the following PIC peripherals:
 * * none
 */
#include <xc.h>
#include "vlcb.h"
#include "nvm.h"
#include "mns.h"
#include "timedResponse.h"
#include "event_teach.h"

/**
 * @file
 * @brief
 * Simple implementation of the VLCB Event Teach Service.
 * @details
 * Event teaching service
 * The service definition object is called eventTeachService.
 *
 * The events are stored as a hash table in NVM (flash is faster to read than EEPROM)
 * There can be up to 255 events. 
 *
 * This generic code needs no knowledge of specific EV usage.
 *
 * @warning
 * BEWARE must set NUM_EVENTS to a maximum of 255!
 * If set to 256 then the for (uint8_t i=0; i<NUM_EVENTS; i++) loops will never end
 * as they use an uint8_t instead of int for space/performance reasons.
 *
 * @warning
 * BEWARE Concurrency: The functions which use the eventtable and hash/lookup must not be used
 * whilst there is a chance of the functions which modify the eventtable of RAM based 
 * hash/lookup tables being called. These functions should therefore either be called
 * from the same thread or disable interrupts. 
 */

/*
 * Events are stored in the EventTable which consists of rows containing the following 
 * fields:
 * * Event event                      4 bytes
 * * uint8_t flags
 * * uint8_t evs[PARAM_NUM_EV_EVENT]  PARAM_NUM_EV_EVENT bytes
 * 
 * The number of table entries is defined by NUM_EVENTS.
 * The 'event' field contains the NN/EN of the event.
 * 
 * Unused entries in the table have the event.EN set to zero.
 */

// forward definitions
static void teachFactoryReset(void);
static void teachPowerUp(void);
static Processed teachProcessMessage(Message * m);
static uint8_t teachGetESDdata(uint8_t id);
void clearAllEvents(void);
Processed checkLen(Message * m, uint8_t needed, uint8_t service);
static Processed teachCheckLen(Message * m, uint8_t needed, uint8_t learn);
static uint8_t evtIdxToTableIndex(uint8_t evtIdx);
TimedResponseResult nerdCallback(uint8_t type, uint8_t serviceIndex, uint8_t step);
TimedResponseResult reqevCallback(uint8_t type, uint8_t serviceIndex, uint8_t step);
uint16_t getNN(uint8_t tableIndex);
uint16_t getEN(uint8_t tableIndex);
uint8_t numEv(uint8_t tableIndex);
int16_t getEv(uint8_t tableIndex, uint8_t evNum);
static uint8_t tableIndexToEvtIdx(uint8_t tableIndex);
uint8_t findEvent(uint16_t nodeNumber, uint16_t eventNumber);
static uint8_t removeTableEntry(uint8_t tableIndex);
uint8_t removeEvent(uint16_t nodeNumber, uint16_t eventNumber);
static void doNnclr(void);
static void doNerd(void);
static void doNnevn(void);
static void doRqevn(void);
static void doNenrd(uint8_t index);
static void doReval(uint8_t enNum, uint8_t evNum);
static void doEvuln(uint16_t nodeNumber, uint16_t eventNumber);
static void doReqev(uint16_t nodeNumber, uint16_t eventNumber, uint8_t evNum);
static void doEvlrn(uint16_t nodeNumber, uint16_t eventNumber, uint8_t evNum, uint8_t evVal);

#ifdef VLCB_DIAG
static DiagnosticVal * teachGetDiagnostic(uint8_t code);
/**
 * The diagnostic values supported by the MNS service.
 */
static DiagnosticVal teachDiagnostics[NUM_TEACH_DIAGNOSTICS+1];
#endif

/** Errno for error number. */
uint8_t errno;

#ifdef EVENT_HASH_TABLE
uint8_t eventChains[EVENT_HASH_LENGTH][EVENT_CHAIN_LENGTH];
#endif

static uint8_t timedResponseOpcode; // used to differentiate a timed response for reqev AND reval

/*
 * Each row in the event table consists of:
 * Event + flags + ev[PARAM_NUM_EV_EVENT] i.e. a total of 5 + PARAM_NUM_EV_EVENT bytes 
 */

#define EVENTTABLE_WIDTH (sizeof(Event) + 1 + PARAM_NUM_EV_EVENT)
#define EVENTTABLE_OFFSET_NNH   0
#define EVENTTABLE_OFFSET_NNL   1
#define EVENTTABLE_OFFSET_ENH   2
#define EVENTTABLE_OFFSET_ENL   3
#define EVENTTABLE_OFFSET_FLAGS 4
#define EVENTTABLE_OFFSET_EVS   5
// The flags
#define EVENT_FLAG_DEFAULT      1

/**
 * The service descriptor for the event teach service. The application must include this
 * descriptor within the const Service * const services[] array and include the
 * necessary settings within module.h in order to make use of the event teach
 * service.
 */
const Service eventTeachService = {
    SERVICE_ID_OLD_TEACH,      // id
    1,                  // version
    teachFactoryReset,  // factoryReset
    teachPowerUp,       // powerUp
    teachProcessMessage,// processMessage
    NULL,               // poll
#if defined(_18F66K80_FAMILY_)
    NULL,               // highIsr
    NULL,               // lowIsr
#endif
#ifdef VLCB_SERVICE
    teachGetESDdata,    // get ESD data
#endif
#ifdef VLCB_DIAG
    teachGetDiagnostic, // getDiagnostic
#endif
};

//
// SERVICE FUNCTIONS
//

/**
 * Factory reset clears the event table.
 */
static void teachFactoryReset(void) {
    clearAllEvents();
}

/**
 * Power up loads the RAM based hash tables from the non volatile event table.
 */
static void teachPowerUp(void) {
    uint8_t i;

#ifdef EVENT_HASH_TABLE
    rebuildHashtable();
#endif
#ifdef VLCB_DIAG
    // Clear the diagnostics
    for (i=1; i<= NUM_TEACH_DIAGNOSTICS; i++) {
        teachDiagnostics[i].asUint = 0;
    }
    teachDiagnostics[TEACH_DIAG_COUNT].asUint = NUM_TEACH_DIAGNOSTICS;
#endif
    errno = 0;
    mode_flags &= ~FLAG_MODE_LEARN; // revert to learn OFF on power up
}

/**
 * Process the event teaching messages. There are many messages to be handles such as
 * ones to enter Learn mode, returning information about the number of slots in
 * the event table. The main functionality manages the Events and EVs.
 * @param m the message to be processed
 * @return PROCESSED if the message need no further processing
 */
static Processed teachProcessMessage(Message* m) {
    switch(m->opc) {
        /* First mode changes. Another check if we are going into Learn or another node is going to Learn */
        case OPC_NNLRN:
            if (teachCheckLen(m, 3, 0) == PROCESSED) return PROCESSED;
            if ((m->bytes[0] == nn.bytes.hi) && (m->bytes[1] == nn.bytes.lo)) {
                mode_flags |= FLAG_MODE_LEARN;
            } else {
                mode_flags &= ~FLAG_MODE_LEARN;
            }
            return PROCESSED;
#ifdef VLCB_MODE
        case OPC_MODE:      // 76 MODE - NN, mode
            if (teachCheckLen(m, 4, 0) == PROCESSED) return PROCESSED;
            if ((m->bytes[0] == nn.bytes.hi) && (m->bytes[1] == nn.bytes.lo)) {
                if (m->bytes[2] == MODE_LEARN_ON) {
                    // Do enter Learn mode
                    mode_flags |= FLAG_MODE_LEARN;
                    return PROCESSED;
                } else if (m->bytes[2] == MODE_LEARN_OFF) {
                    // Do exit Learn mode
                    mode_flags &= ~FLAG_MODE_LEARN;
                    return PROCESSED;
                }
            } else {
                // Another module going to Learn so we must exit learn
                mode_flags &= ~FLAG_MODE_LEARN;
            }
            return NOT_PROCESSED;   // mode probably processed by other services
#endif
        /* This block must be in Learn mode and NN doesn't need to match ours */
        case OPC_EVLRN:     // D2 EVLRN - NN, EN, EV#, EVval
            if (teachCheckLen(m, 7, 1) == PROCESSED) {
                sendMessage3(OPC_CMDERR, nn.bytes.hi, nn.bytes.lo, CMDERR_INV_CMD);
//                sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_EVLRN, SERVICE_ID_OLD_TEACH, CMDERR_INV_CMD);
                return PROCESSED;
            }
            if (! (mode_flags & FLAG_MODE_LEARN)) return PROCESSED;
            // Do learn
            doEvlrn((uint16_t)(m->bytes[0]<<8) | (m->bytes[1]), (uint16_t)(m->bytes[2]<<8) | (m->bytes[3]), m->bytes[4], m->bytes[5]);
            return PROCESSED;
        case OPC_EVULN:     // 95 EVULN - NN, EN
            if (teachCheckLen(m, 5, 1) == PROCESSED) return PROCESSED;
            if (! (mode_flags & FLAG_MODE_LEARN)) return PROCESSED;
            // do unlearn
            doEvuln((uint16_t)(m->bytes[0]<<8) | (m->bytes[1]), (uint16_t)(m->bytes[2]<<8) | (m->bytes[3]));
            return PROCESSED;
        case OPC_REQEV:     // B2 REQEV - NN EN EV#
            if (teachCheckLen(m, 6, 1) == PROCESSED) return PROCESSED;
            if (! (mode_flags & FLAG_MODE_LEARN)) return PROCESSED;
            // do read EV
            doReqev((uint16_t)(m->bytes[0]<<8) | (m->bytes[1]), (uint16_t)(m->bytes[2]<<8) | (m->bytes[3]), m->bytes[4]);
            return PROCESSED;
        /* This block contain an NN which needs to match our NN */
        case OPC_NNULN:     // 54 NNULN - NN
            if (teachCheckLen(m, 3, 0) == PROCESSED) return PROCESSED;
            if ((m->bytes[0] != nn.bytes.hi) || (m->bytes[1] != nn.bytes.lo)) return PROCESSED;  // not us
            // Do exit Learn mode
            mode_flags &= ~FLAG_MODE_LEARN;
            return PROCESSED;
        case OPC_NNCLR:     // 55 NNCLR - NN
            if (teachCheckLen(m, 3, 1) == PROCESSED) return PROCESSED;
            if ((m->bytes[0] != nn.bytes.hi) || (m->bytes[1] != nn.bytes.lo)) return PROCESSED;  // not us
            /* Must be in Learn mode for this one */
            if (! (mode_flags & FLAG_MODE_LEARN)) {
                sendMessage3(OPC_CMDERR, nn.bytes.hi, nn.bytes.lo, CMDERR_NOT_LRN);
#ifdef VLCB_GRSP
                sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_NNCLR, SERVICE_ID_OLD_TEACH, CMDERR_NOT_LRN);
#endif
                return PROCESSED;
            }
            // do NNCLR
            doNnclr();
            break;
        case OPC_NERD:      // 57 NERD - NN
            if (teachCheckLen(m, 3, 0) == PROCESSED) return PROCESSED;
            if ((m->bytes[0] != nn.bytes.hi) || (m->bytes[1] != nn.bytes.lo)) return PROCESSED;  // not us
            // do NERD
            doNerd();
            return PROCESSED;
        case OPC_NNEVN:     // 56 NNEVN - NN
            if (teachCheckLen(m, 3, 0) == PROCESSED) return PROCESSED;
            if ((m->bytes[0] != nn.bytes.hi) || (m->bytes[1] != nn.bytes.lo)) return PROCESSED;  // not us
            // do NNEVN
            doNnevn();
            return PROCESSED;
        case OPC_RQEVN:     // 58 RQEVN - NN
            if (teachCheckLen(m, 3, 0) == PROCESSED) return PROCESSED;
            if ((m->bytes[0] != nn.bytes.hi) || (m->bytes[1] != nn.bytes.lo)) return PROCESSED;  // not us
            // do RQEVN
            doRqevn();
            return PROCESSED;
        case OPC_NENRD:     // 72 NENRD - NN, EN#
            if (teachCheckLen(m, 4, 0) == PROCESSED) return PROCESSED;
            if ((m->bytes[0] != nn.bytes.hi) || (m->bytes[1] != nn.bytes.lo)) return PROCESSED;  // not us
            // do NENRD
            doNenrd(m->bytes[2]);
            return PROCESSED;
        case OPC_REVAL:     // 9C REVAL - NN, EN#, EV#
            if (teachCheckLen(m, 5, 0) == PROCESSED) return PROCESSED;
            if ((m->bytes[0] != nn.bytes.hi) || (m->bytes[1] != nn.bytes.lo)) return PROCESSED;  // not us
            // do REVAL
            doReval(m->bytes[2], m->bytes[3]);
            return PROCESSED;
        case OPC_EVLRNI:    // F5 EVLRNI - NN, EN, EN#, EV#, EVval
            if (teachCheckLen(m, 8, 1) == PROCESSED) return PROCESSED;
            // do EVLRNI
            doEvlrn((uint16_t)(m->bytes[0]<<8) | (m->bytes[1]), (uint16_t)(m->bytes[2]<<8) | (m->bytes[3]), m->bytes[5], m->bytes[6]);
            return PROCESSED;
        default:
            break;
    }
    return NOT_PROCESSED;
}

/**
 * Check the message length to ensure it is valid.
 * @param m the message
 * @param needed the number of bytes needed
 * @param learn indicator whether to use Learn mode or NN
 * @return PROCESSED if there are insufficient bytes
 */
static Processed teachCheckLen(Message * m, uint8_t needed, uint8_t learn) {
    if (learn) {
        // This is a message using Learn mode and therefore no NN
        if (m->len < needed) {
            // message is short
            if (mode_flags & FLAG_MODE_LEARN) {
                // This module is in Learn mode so we should indicate an error
#ifdef VLCB_GRSP
                sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, (uint8_t)(m->opc), SERVICE_ID_OLD_TEACH, CMDERR_INV_CMD);
#endif
            }
            return PROCESSED;
        }
        return NOT_PROCESSED;   // message length ok
    }
    return checkLen(m, needed, SERVICE_ID_OLD_TEACH);
}

#ifdef VLCB_SERVICE
/**
 * The Teach service supports data in the ESD message.
 * @param id which of the ESD data bytes
 * @return the value to be returned in the data byte
 */
static uint8_t teachGetESDdata(uint8_t id) {
    switch (id) {
        case 1: return NUM_EVENTS;
        case 2: return PARAM_NUM_EV_EVENT;
        default: return 0;
    }
}
#endif

#ifdef VLCB_DIAG
/**
 * Provide the means to return the diagnostic data.
 * @param index the diagnostic index 1..NUM_CAN_DIAGNOSTSICS
 * @return a pointer to the diagnostic data or NULL if the data isn't available
 */
static DiagnosticVal * teachGetDiagnostic(uint8_t index) {
    if (index > NUM_TEACH_DIAGNOSTICS) {
        return NULL;
    }
    return &(teachDiagnostics[index]);
}
#endif

//
// FUNCTIONS TO DO THE ACTUAL WORK
//

/**
 * Removes all events including default events.
 */
void clearAllEvents(void) {
    uint8_t tableIndex;

    for (tableIndex=0; tableIndex<NUM_EVENTS; tableIndex++) {
        removeTableEntry(tableIndex);
    }
#ifdef EVENT_HASH_TABLE
    rebuildHashtable();
#endif
}

/**
 * Read number of available event slots.
 * This returned the number of unused slots in the Consumed event Event2Action table.
 */
static void doNnevn(void) {
    // count the number of unused slots.
    uint8_t count = 0;
    uint8_t i;
    for (i=0; i<NUM_EVENTS; i++) {
        uint16_t eventNumber;
        eventNumber = getEN(i);
        if (eventNumber == 0) {
            count++;
        }
    }
    sendMessage3(OPC_EVNLF, nn.bytes.hi, nn.bytes.lo, count);
} // doNnevn


/**
 * Do the NERD. 
 * This sets things up so that timedResponse will do the right stuff.
 */
static void doNerd(void) {
    startTimedResponse(TIMED_RESPONSE_NERD, findServiceIndex(SERVICE_ID_OLD_TEACH), nerdCallback);
}

/**
 * The callback to do the NERD responses.
 * @param type the type of the timedResponse
 * @param serviceIndex the service
 * @param step how far through the processing
 * @return whether to finish or continue processing
 */
TimedResponseResult nerdCallback(uint8_t type, uint8_t serviceIndex, uint8_t step){
    Word nodeNumber, eventNumber;
    // The step is used to index through the event table
    if (step >= NUM_EVENTS) {  // finished?
        return TIMED_RESPONSE_RESULT_FINISHED;
    }

    eventNumber.word = getEN(step);
    if (eventNumber.word != 0) {
        nodeNumber.word = getNN(step);
        sendMessage7(OPC_ENRSP, nn.bytes.hi, nn.bytes.lo, nodeNumber.bytes.hi, nodeNumber.bytes.lo, eventNumber.bytes.hi, eventNumber.bytes.lo, tableIndexToEvtIdx(step));
    }

    return TIMED_RESPONSE_RESULT_NEXT;
}


/**
 * Read a single stored event by index and return a ENRSP response.
 * 
 * @param index index into event table
 */
static void doNenrd(uint8_t index) {
    uint8_t tableIndex;
    uint16_t nodeNumber, eventNumber;
    
    tableIndex = evtIdxToTableIndex(index);
    // check this is a valid index
    if (tableIndex >= NUM_EVENTS) {
        sendMessage3(OPC_CMDERR, nn.bytes.hi, nn.bytes.lo, CMDERR_INV_EN_IDX);
#ifdef VLCB_GRSP
                sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_NENRD, SERVICE_ID_OLD_TEACH, CMDERR_INV_EN_IDX);
#endif
        return;
    }
    nodeNumber = getNN(tableIndex);
    eventNumber = getEN(tableIndex);
    sendMessage5(OPC_ENRSP, nodeNumber>>8, nodeNumber&0xFF, eventNumber>>8, eventNumber&0xFF, index);

} // doNenrd

/**
 * Read number of stored events.
 * This returns the number of events which is different to the number of used slots 
 * in the Event table.
 */
static void doRqevn(void) {
    // Count the number of used slots.
    uint8_t count = 0;
    uint8_t i;
    for (i=0; i<NUM_EVENTS; i++) {
        uint16_t eventNumber;
        eventNumber = getEN(i);
        if (eventNumber != 0) {
            count++;
        }
    }
    sendMessage3(OPC_NUMEV, nn.bytes.hi, nn.bytes.lo, count);
} // doRqevn

/**
 * Clear all Events.
 */
static void doNnclr(void) {
    clearAllEvents();
    sendMessage2(OPC_WRACK, nn.bytes.hi, nn.bytes.lo);
#ifdef VLCB_GRSP
    sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_NNCLR, SERVICE_ID_OLD_TEACH, GRSP_OK);
#endif
} //doNnclr

/**
 * Teach event whilst in learn mode.
 * Teach or reteach an event associated with an action. 

 * @param nodeNumber event's NN
 * @param eventNumber the EN
 * @param evNum the EV number
 * @param evVal the EV value
 */
static void doEvlrn(uint16_t nodeNumber, uint16_t eventNumber, uint8_t evNum, uint8_t evVal) {

    evNum--;    // convert VLCB message numbering (starts at 1) to internal numbering)
    if (evNum >= PARAM_NUM_EV_EVENT) {
        sendMessage3(OPC_CMDERR, nn.bytes.hi, nn.bytes.lo, CMDERR_INV_EV_IDX);
#ifdef VLCB_GRSP
        sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_EVLRN, SERVICE_ID_OLD_TEACH, CMDERR_INV_EV_IDX);
#endif
        return;
    }
    APP_addEvent(nodeNumber, eventNumber, evNum, evVal, FALSE);
    if (errno) {
        // validation error
        sendMessage3(OPC_CMDERR, nn.bytes.hi, nn.bytes.lo, errno);
#ifdef VLCB_GRSP
        sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_EVLRN, SERVICE_ID_OLD_TEACH, errno);
#endif
        return;
    }
#ifdef VLCB_DIAG
    teachDiagnostics[TEACH_DIAG_NUM_TEACH].asUint++;
#endif
    sendMessage2(OPC_WRACK, nn.bytes.hi, nn.bytes.lo);
#ifdef VLCB_GRSP
    sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_EVLRN, SERVICE_ID_OLD_TEACH, GRSP_OK);
#endif
    return;
}

/**
 * Read an event variable by index.
 * 
 * @param enNum index into event table
 * @param evNum EV number index
 */
static void doReval(uint8_t enNum, uint8_t evNum) {
	// Get event index and event variable number from message
	// Send response with EV value
    uint8_t evIndex;
    uint8_t tableIndex = evtIdxToTableIndex(enNum);
    int evVal;
    
    if (tableIndex >= NUM_EVENTS) {
        sendMessage3(OPC_CMDERR, nn.bytes.hi, nn.bytes.lo, CMDERR_INV_EV_IDX);
        return;
    }

    evIndex = evNum-1U;    // Convert from CBUS numbering (starts at 1 for produced action))
    
    if (evNum == 0) {
        evVal = EVperEVT;
        if ((mode_flags & FLAG_MODE_FCUCOMPAT) == 0) {
            // send all of the EVs
            // Note this somewhat abuses the type parameter
            timedResponseOpcode = OPC_NEVAL;
            startTimedResponse(tableIndex, findServiceIndex(SERVICE_ID_OLD_TEACH), reqevCallback);
        } 
    } else {
        evVal = getEv(tableIndex, evIndex);
    }
    
    if (evVal < 0) {
        // a negative value is the error code
        sendMessage3(OPC_CMDERR, nn.bytes.hi, nn.bytes.lo, (uint8_t)(-evVal));
#ifdef VLCB_GRSP
        sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_REVAL, SERVICE_ID_OLD_TEACH, (uint8_t)(-evVal));
#endif
        return;
    }
    sendMessage5(OPC_NEVAL, nn.bytes.hi, nn.bytes.lo, enNum, evNum, (uint8_t)evVal);            
} // doReval

/**
 * Unlearn event.
 * @param nodeNumber Event NN
 * @param eventNumber Event EN
 */
static void doEvuln(uint16_t nodeNumber, uint16_t eventNumber) {
    uint8_t result;
    result = removeEvent(nodeNumber, eventNumber);
    if (result) {
        sendMessage3(OPC_CMDERR, nn.bytes.hi, nn.bytes.lo, result);
#ifdef VLCB_GRSP
        sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_EVULN, SERVICE_ID_OLD_TEACH, result);
#endif
        return;
    }
    // Send a WRACK - difference from CBUS
    sendMessage2(OPC_WRACK, nn.bytes.hi, nn.bytes.lo);
#ifdef VLCB_GRSP
    sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_EVULN, SERVICE_ID_OLD_TEACH, GRSP_OK);
#endif
}

/**
 * Read an event variable by event id.
 * @param nodeNumber Event NN
 * @param eventNumber Event EN
 * @param evNum EV number/index
 */
static void doReqev(uint16_t nodeNumber, uint16_t eventNumber, uint8_t evNum) {
    int16_t evVal;
    // get the event
    uint8_t tableIndex = findEvent(nodeNumber, eventNumber);
    if (tableIndex == NO_INDEX) {
        sendMessage3(OPC_CMDERR, nn.bytes.hi, nn.bytes.lo, CMDERR_INVALID_EVENT);
#ifdef VLCB_GRSP
        sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_REQEV, SERVICE_ID_OLD_TEACH, CMDERR_INVALID_EVENT);
#endif
        return;
    }
    if (evNum > PARAM_NUM_EV_EVENT) {
        sendMessage3(OPC_CMDERR, nn.bytes.hi, nn.bytes.lo, CMDERR_INV_EV_IDX);
#ifdef VLCB_GRSP
        sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_REQEV, SERVICE_ID_OLD_TEACH, CMDERR_INV_EV_IDX);
#endif
        return;
    }
    
    if (evNum == 0) {
        evVal = EVperEVT;
        if ((mode_flags & FLAG_MODE_FCUCOMPAT) == 0) {
            sendMessage6(OPC_EVANS, nodeNumber>>8, nodeNumber&0xFF, eventNumber>>8, eventNumber&0xFF, 0, numEv(tableIndex));
            // send all of the EVs
            // Note this somewhat abuses the type parameter
            timedResponseOpcode = OPC_EVANS;
            startTimedResponse(tableIndex, findServiceIndex(SERVICE_ID_OLD_TEACH), reqevCallback);
            return;
        } 
    } else {
        evVal = getEv(tableIndex, evNum-1);
    }
    
    if (evVal < 0) {
        // a negative value is the error code
        sendMessage3(OPC_CMDERR, nn.bytes.hi, nn.bytes.lo, (uint8_t)(-evVal));
#ifdef VLCB_GRSP
        sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_REQEV, SERVICE_ID_OLD_TEACH, (uint8_t)(-evVal));
#endif
        return;
    }

    sendMessage6(OPC_EVANS, nodeNumber>>8, nodeNumber&0xFF, eventNumber>>8, eventNumber&0xFF, evNum, (uint8_t)evVal);
    return;
}
/**
 * The callback to do the REQEV responses.
 * @param tableIndex the index of the event in the eventTable
 * @param serviceIndex the service
 * @param step how far through the processing, considered to be an EV#-1
 * @return whether to finish or continue processing
 */
TimedResponseResult reqevCallback(uint8_t tableIndex, uint8_t serviceIndex, uint8_t step){
    Word nodeNumber, eventNumber;

    uint8_t nEv = numEv(tableIndex);
    int16_t ev;
    // The step is used to index through the event table
    if (step+1 > nEv) {  // finished?
        return TIMED_RESPONSE_RESULT_FINISHED;
    }
    // if its not free and not a continuation then it is start of an event
    nodeNumber.word = getNN(tableIndex);
    eventNumber.word = getEN(tableIndex);
    ev = getEv(tableIndex, step);
    if (ev >= 0) {
        if (timedResponseOpcode == OPC_EVANS) {
            sendMessage6(OPC_EVANS, nodeNumber.bytes.hi, nodeNumber.bytes.lo, eventNumber.bytes.hi, eventNumber.bytes.lo, step+1, (uint8_t)ev);
        } else {
            sendMessage5(OPC_NEVAL, nodeNumber.bytes.hi, nodeNumber.bytes.lo, tableIndexToEvtIdx(tableIndex), step+1, (uint8_t)ev);
        }
    }
    return TIMED_RESPONSE_RESULT_NEXT;
}


/**
 * Remove event.
 * 
 * @param nodeNumber Event NN
 * @param eventNumber Event EN
 * @return error or 0 for success
 */
uint8_t removeEvent(uint16_t nodeNumber, uint16_t eventNumber) {
    // need to delete this action from the Event table. 
    uint8_t tableIndex = findEvent(nodeNumber, eventNumber);
    if (tableIndex == NO_INDEX) return CMDERR_INVALID_EVENT; // not found
    // found the event to delete
    return removeTableEntry(tableIndex);
}

/**
 * Remove an event from the event table clearing any continuation entries.
 * @param tableIndex which event to be cleared
 * @return error or 0 for success
 */
static uint8_t removeTableEntry(uint8_t tableIndex) {
    uint8_t i;
#ifdef SAFETY
    if (tableIndex >= NUM_EVENTS) return CMDERR_INV_EV_IDX;
#endif
    // set the NN and EN to zero
    writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_WIDTH*tableIndex + EVENTTABLE_OFFSET_NNH, 0x00);
    writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_WIDTH*tableIndex + EVENTTABLE_OFFSET_NNL, 0x00);
    writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_WIDTH*tableIndex + EVENTTABLE_OFFSET_ENH, 0x00);
    writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_WIDTH*tableIndex + EVENTTABLE_OFFSET_ENL, 0x00);
    writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_WIDTH*tableIndex + EVENTTABLE_OFFSET_FLAGS, 0x00);
        
    for (i=0; i<PARAM_NUM_EV_EVENT; i++) {
        writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_WIDTH*tableIndex + (EVENTTABLE_OFFSET_EVS + i), 0x00);
    }
    flushFlashBlock();
#ifdef EVENT_HASH_TABLE
    rebuildHashtable();
#endif
    return 0;
}

/**
 * Add an event/EV.
 * Teach or re-teach an EV for an event. 
 * This may (optionally) need to create a new event and then optionally
 * create additional chained entries. All newly allocated table entries need
 * to be initialised. Errors are returned in global uint8_t errno.
 * 
 * @param nodeNumber Event NN
 * @param eventNumber Event NN
 * @param evNum the EV index (starts at 0 for the produced action)
 * @param evVal the EV value
 * @param forceOwnNN the value of the flag
 * @return event table index
 */
uint8_t addEvent(uint16_t nodeNumber, uint16_t eventNumber, uint8_t evNum, uint8_t evVal, Boolean forceOwnNN) {
    uint8_t tableIndex;
    
    // do we currently have an event
    tableIndex = findEvent(nodeNumber, eventNumber);
    if (tableIndex == NO_INDEX) {
        errno = CMDERR_TOO_MANY_EVENTS;
        // didn't find the event so find an empty slot and create one
        for (tableIndex=0; tableIndex<NUM_EVENTS; tableIndex++) {
            uint16_t en = getEN(tableIndex);
            if (en == 0) {
                uint8_t e;
                // found a free slot, initialise it
                writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_WIDTH*tableIndex+EVENTTABLE_OFFSET_NNL, nodeNumber&0xFF);
                writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_WIDTH*tableIndex+EVENTTABLE_OFFSET_NNH, nodeNumber>>8);
                writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_WIDTH*tableIndex+EVENTTABLE_OFFSET_ENL, eventNumber&0xFF);
                writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_WIDTH*tableIndex+EVENTTABLE_OFFSET_ENH, eventNumber>>8);
                if (forceOwnNN) {
                    writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_WIDTH*tableIndex+EVENTTABLE_OFFSET_FLAGS, EVENT_FLAG_DEFAULT);
                } else {
                    writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_WIDTH*tableIndex+EVENTTABLE_OFFSET_FLAGS, 0);
                }
                for (e = 0; e < EVENT_TABLE_WIDTH; e++) {   // in this case EVENT_TABLE_WIDTH == EVperEvt
                    writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_WIDTH*tableIndex+EVENTTABLE_OFFSET_EVS+e, EV_FILL);
                }
                errno = 0;
                break;
            }
        }
        if (errno) {
            return NO_INDEX;
        }
    }
 
    if (writeEv(tableIndex, evNum, evVal)) {
        // failed to write
        errno = CMDERR_INV_EV_IDX;
        return NO_INDEX;
    }
    // success
    flushFlashBlock();
#ifdef EVENT_HASH_TABLE
    rebuildHashtable();
#endif
    return tableIndex;
}

/**
 * Find an event in the event table and return its index.
 * 
 * @param nodeNumber event NN
 * @param eventNumber event EN
 * @return index into event table or NO_INDEX if not present
 */
uint8_t findEvent(uint16_t nodeNumber, uint16_t eventNumber) {
#ifdef EVENT_HASH_TABLE
    uint8_t hash = getHash(nodeNumber, eventNumber);
    uint8_t chainIdx;
    for (chainIdx=0; chainIdx<EVENT_CHAIN_LENGTH; chainIdx++) {
        uint8_t tableIndex = eventChains[hash][chainIdx];
        uint16_t nn, en;
        if (tableIndex == NO_INDEX) return NO_INDEX;
        nn = getNN(tableIndex);
        en = getEN(tableIndex);
        if ((nn == nodeNumber) && (en == eventNumber)) {
            return tableIndex;
        }
    }
#else
    uint8_t tableIndex;
    for (tableIndex=0; tableIndex < NUM_EVENTS; tableIndex++) {
        uint16_t b = getEN(tableIndex);
        if (b == eventNumber) {
            b = getNN(tableIndex);
            if (b == nodeNumber) {
                return tableIndex;
            }
        }
    }
#endif
    return NO_INDEX;
}

/**
 * Write an EV value to an event.
 * 
 * @param tableIndex the index into the event table
 * @param evNum the EV number (0 for the produced action)
 * @param evVal the EV value
 * @return 0 if success otherwise the error
 */
uint8_t writeEv(uint8_t tableIndex, uint8_t evNum, uint8_t evVal) {
    if (evNum >= PARAM_NUM_EV_EVENT) {
        return CMDERR_INV_EV_IDX;
    }
    if (tableIndex >= NUM_EVENTS) {
        return CMDERR_INV_EN_IDX;
    }
    
    // now write the EV
    writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_WIDTH*tableIndex+EVENTTABLE_OFFSET_EVS+evNum, evVal);
    return 0;
}
 
/**
 * Return an EV value for an event.
 * 
 * @param tableIndex the index of the start of an event
 * @param evNum ev number starts at 0 (Happening)
 * @return the ev value or -error code if error
 */
int16_t getEv(uint8_t tableIndex, uint8_t evNum) {
    if (tableIndex >= NUM_EVENTS) {
        return CMDERR_INV_EN_IDX;
    }
    if (evNum >= PARAM_NUM_EV_EVENT) {
        return -CMDERR_INV_EV_IDX;
    }
    return (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_WIDTH*tableIndex+EVENTTABLE_OFFSET_EVS+evNum);
}

/**
 * Return the number of EVs for an event.
 * 
 * @param tableIndex the index of the start of an event
 * @return the number of EVs
 */
uint8_t numEv(uint8_t tableIndex) {
    return PARAM_NUM_EV_EVENT;
}

/**
 * The EVs for an event after calling getEVs()
 */
uint8_t evs[PARAM_NUM_EV_EVENT];
/**
 * Return all the EV values for an event. EVs are put into the global evs array.
 * 
 * @param tableIndex the index of the start of an event
 * @return the error code or 0 for no error
 */
uint8_t getEVs(uint8_t tableIndex) {

    uint8_t evIdx;
    if (tableIndex >= NUM_EVENTS) {
        return CMDERR_INV_EN_IDX;
    }

    for (evIdx=0; evIdx < PARAM_NUM_EV_EVENT; evIdx++) {
        evs[evIdx] = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_WIDTH*tableIndex+EVENTTABLE_OFFSET_EVS+evIdx);
    }
    return 0;
}

/**
 * Return the NN for an event.
 * Getter so that the application code can obtain information about the event.
 * 
 * @param tableIndex the index of the start of an event
 * @return the Node Number
 */
uint16_t getNN(uint8_t tableIndex) {
    uint16_t hi;
    uint16_t lo;
    uint8_t flags;
    if (tableIndex >= NUM_EVENTS) {
        return CMDERR_INV_EN_IDX;
    }
    
    flags = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_WIDTH*tableIndex+EVENTTABLE_OFFSET_FLAGS);
    if (flags & EVENT_FLAG_DEFAULT) {
        return nn.word;
    }
    lo = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_WIDTH*tableIndex+EVENTTABLE_OFFSET_NNL);
    hi = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_WIDTH*tableIndex+EVENTTABLE_OFFSET_NNH);
    return lo | (hi << 8);
}

/**
 * Return the EN for an event.
 * Getter so that the application code can obtain information about the event.
 * 
 * @param tableIndex the index of the start of an event
 * @return the Event Number
 */
uint16_t getEN(uint8_t tableIndex) {
    uint16_t hi;
    uint16_t lo;
    
    lo = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_WIDTH*tableIndex+EVENTTABLE_OFFSET_ENL);
    hi = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_WIDTH*tableIndex+EVENTTABLE_OFFSET_ENH);
    return lo | (hi << 8);
}

/**
 * Convert an evtIdx from CBUS to an index into the EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*i+EVENTTABLE_OFFSET_.
 * The CBUS spec uses "EN#" as an index into an "Event Table". This is very implementation
 * specific. In this implementation we do actually have an event table behind the scenes
 * so we can have an EN#. However we may also wish to provide some kind of mapping between
 * the CBUS index and out actual implementation specific index. These functions allow us
 * to have a mapping.
 * I currently I just adjust by 1 since the CBUS index starts at 1 whilst the EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*i+EVENTTABLE_OFFSET_
 * index starts at 0.
 * 
 * @param evtIdx
 * @return an index into EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*i+EVENTTABLE_OFFSET_
 */
static uint8_t evtIdxToTableIndex(uint8_t evtIdx) {
    return evtIdx - 1;
}

/**
 * Convert an internal tableIndex into a CBUS EvtIdx.
 * 
 * @param tableIndex index into the EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*i+EVENTTABLE_OFFSET_
 * @return an CBUS EvtIdx
 */
static uint8_t tableIndexToEvtIdx(uint8_t tableIndex) {
    return tableIndex + 1;
}

#ifdef EVENT_HASH_TABLE
/**
 * Obtain a hash for the specified Event. 
 * 
 * The hash table uses an algorithm of an XOR of all the bytes with appropriate shifts.
 * 
 * If we used just the node number, then all short events would hash to the same number
 * If we used just the event number, then for long events the vast majority would 
 * be 1 to 8, so not giving a very good spread.
 *
 * This algorithm hopefully produces a good spread for layouts with either
 * predominantly short events, long events or a mixture.
 * This also means that for layouts using the default FLiM node numbers from 256, 
 * we are effectively starting from zero as far as the hash algorithm is concerned.
 * 
 * @param e the event
 * @return the hash
 */
uint8_t getHash(uint16_t nn, uint16_t en) {
    uint8_t hash;
    // need to hash the NN and EN to a uniform distribution across HASH_LENGTH
    hash = (uint8_t)(nn ^ (nn >> 8U));
    hash = (uint8_t)(7U*hash + (en ^ (en>>8U))); 
    // ensure it is within bounds of eventChains
    hash %= EVENT_HASH_LENGTH;
    return hash;
}


/**
 * Initialise the RAM hash chain for reverse lookup of event to tableIndex.
 */
void rebuildHashtable(void) {
    // invalidate the current hash table
    uint8_t hash;
    uint8_t chainIdx;
    uint8_t tableIndex;
    int a;

    for (hash=0; hash<EVENT_HASH_LENGTH; hash++) {
        for (chainIdx=0; chainIdx < EVENT_CHAIN_LENGTH; chainIdx++) {
            eventChains[hash][chainIdx] = NO_INDEX;
        }
    }
    // now scan the event2Action table and populate the hash and lookup tables
    for (tableIndex=0; tableIndex<NUM_EVENTS; tableIndex++) {
        if (getEN(tableIndex) != 0) {
            int16_t ev;
    
            // found the start of an event definition
            hash = getHash(getNN(tableIndex), getEN(tableIndex));            
            for (chainIdx=0; chainIdx<EVENT_CHAIN_LENGTH; chainIdx++) {
                if (eventChains[hash][chainIdx] == NO_INDEX) {
                    // available
                    eventChains[hash][chainIdx] = tableIndex;
                    break;
                }
            }
        }
    }
}
#endif
