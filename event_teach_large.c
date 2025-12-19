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
 * @author Pete Brownlow
 * @author Ian Hogg 
 * @date Dec 2022
 * 
 */ 
/*
 *  History for this file:
	28/12/15	Pete Brownlow	- Factored out from FLiM module
    25/2/16     Pete Brownlow   - coding in progress
    23/5/17     Ian Hogg        - added support for produced events
    05/06/21    Ian Hogg        - removed happenings and actions so that this file just handles EV bytes
 *  14/12/22    Ian Hogg        - Ported to be a VLCB service and XC8 compiler
 * 
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
#include "event_producer.h"
#include "module.h"

/**
 * @file
 * @brief
 * Implementation of the VLCB Event Teach Service.
 * @details
 * Event teaching service
 * The service definition object is called eventTeachService.
 *
 * The events are stored as a hash table in flash (flash is faster to read than EEPROM)
 * There can be up to 255 events. Since the address in the hash table will be 16 bits, and the
 * address of the whole event table can also be covered by a 16 bit address, there is no
 * advantage in having a separate hashed index table pointing to the actual event table.
 * Therefore the hashing algorithm produces the index into the actual event table, which
 * can be shifted to give the address - each event table entry is 16 bytes. After the event
 * number and hash table overhead, this allows up to 10 EVs per event.
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
 *
 * # Module.h definitions required for the Event Teach service
 * - \#define EVENT_TABLE_WIDTH   This the the width of the table - not the
 *                       number of EVs per event as multiple rows in
 *                       the table can be used to store an event.
 * - \#define NUM_EVENTS          The number of rows in the event table. The
 *                        actual number of events may be less than this
 *                        if any events use more the 1 row.
 * - \#define EVENT_TABLE_ADDRESS   The address where the event table is stored.
 * - \#define EVENT_TABLE_NVM_TYPE  Set to be either FLASH_NVM_TYPE or EEPROM_NVM_TYPE
 * - \#define EVENT_HASH_TABLE      If defined then hash tables will be used for
 *                        quicker lookup of events at the expense of additional RAM.
 * - \#define EVENT_HASH_LENGTH     If hash tables are used then this sets the length
 *                        of the hash.
 * - \#define EVENT_CHAIN_LENGTH    If hash tables are used then this sets the number
 *                        of events in the hash chain.
 * - \#define MAX_HAPPENING         Set to be the maximum Happening value
 *
 * The code is responsible for storing EVs for each defined event and 
 * also for allowing speedy lookup of EVs given an Event or finding an Event given 
 * a Happening which is stored in the first EVs.
 */

/*
 *  Events are stored in the EventTable which consists of rows containing the following 
 * fields:
 * * EventTableFlags flags            1 byte
 * * uint8_t next                     1 byte
 * * Event event                      4 bytes
 * * uint8_t evs[EVENT_TABLE_WIDTH]   EVENT_TABLE_WIDTH bytes
 * 
 * The number of table entries is defined by NUM_EVENTS.
 * 
 * The EVENT_TABLE_WIDTH determines the number of EVs that can be stored within 
 * a single EventTable row. Where events can have more EVs that can be fitted within 
 * a single row then multiple rows are chained together using the 'next' field. 
 * 'next' indicates the index into the EventTable for chained entries for a single 
 * Event.
 * 
 * An example to clarify is the CANMIO which sets EVENT_TABLE_WITH to 10 so that 
 * size of a row is 16bytes. A chain of two rows can store 20 EVs. CANMIO has a 
 * limit of 20 EVs per event (EVperEVT) so that a maximum of 2 entries are chained.
 * 
 * The 'event' field is only used in the first in a chain of entries and contains 
 * the NN/EN of the event.
 * 
 * The EventTableFlags have the following entries:
 * eVsUsed                4 bits
 * continued              1 bit
 * forceOwnNN             1 bit
 * freeEntry              1 bit
 * 
 * The 'continued' flag indicates if there is another table entry chained to this 
 * one. If the flag is set then the 'next' field contains the index of the chained 
 * entry.
 * 
 * The 'forceOwnNN' flag indicates that for produced events the NN in the event 
 * field should be ignored and the module's current NN should be used instead. 
 * This allows default events to be maintained even if the NN of the module is 
 * changed. Therefore this flag should be set for default produced events.
 * 
 * The 'freeEntry' flag indicates that this entry in the EventTable is currently 
 * unused.
 * 
 * The 'eVsUsed' field records how many of the evs contain valid data. 
 * It is only applicable for the last entry in the chain since all EVs less than 
 * this are assumed to contain valid data. Since this field is only 4 bits long 
 * this places a limit on the EVENT_TABLE_WIDTH of 15.
 * 
 * EXAMPLE
 * Let's go through an example of filling in the table. We'll look at the first 
 * 4 entries in the table and let's have EVENT_TABLE_WIDTH=4 but have EVperEVT=8.
 * 
 * At the outset there is an empty table. All rows have the 'freeEntry' bit set:
 * <pre>
 * index   eVsUsed        continued       forceOwnNN      freeEntry       next    Event   evs[]
 * 0       0              0               0               1               0       0:0     0,0,0,0
 * 1       0              0               0               1               0       0:0     0,0,0,0
 * 2       0              0               0               1               0       0:0     0,0,0,0
 * 3       0              0               0               1               0       0:0     0,0,0,0
 * </pre>
 * Now if an EV of an event is set (probably using EVLRN CBUS command) then the 
 * table is updated. Let's set EV#1 for event 256:101 to the value 99:
 * <pre>
 * index   eVsUsed        continued       forceOwnNN      freeEntry       next    Event   evs[]
 * 0       1              0               0               0               0       256:101 99,0,0,0
 * 1       0              0               0               1               0       0:0     0,0,0,0
 * 2       0              0               0               1               0       0:0     0,0,0,0
 * 3       0              0               0               1               0       0:0     0,0,0,0
 * </pre>
 * Now let's set EV#2 of event 256:102 to 15:
 * <pre>
 * index   eVsUsed        continued       forceOwnNN      freeEntry       next    Event   evs[]
 * 0       1              0               0               0               0       256:101 99,0,0,0
 * 1       2              0               0               0               0       256:102 0,15,0,0
 * 2       0              0               0               1               0       0:0     0,0,0,0
 * 3       0              0               0               1               0       0:0     0,0,0,0
 * </pre>
 * Now let's set EV#8 of event 256:101 to 29:
 * <pre>
 * index   eVsUsed        continued       forceOwnNN      freeEntry       next    Event   evs[]
 * 0       1              1               0               0               0       256:101 99,0,0,0
 * 1       2              0               0               0               0       256:102 0,15,0,0
 * 2       4              0               0               0               0       0:0     0,0,0,29
 * 3       0              0               0               1               0       0:0     0,0,0,0
 * </pre>
 * To perform the speedy lookup of EVs given an Event a hash table can be used by 
 * defining EVENT_HASH_TABLE. The hash table is stored in 
 * uint8_t eventChains[HASH_LENGTH][CHAIN_LENGTH];
 * 
 * An event hashing function is provided uint8_t getHash(nn, en) which should give 
 * a reasonable distribution of hash values given the typical events used.
 * 
 * This table is populated from the EventTable upon power up using rebuildHashtable(). 
 * This function must be called before attempting to use the hash table. Each Event 
 * from the EventTable is hashed using getHash(nn,en), trimmed to the HASH_LENGTH 
 * and the index in the EventTable is then stored in the eventChains at the next 
 * available bucket position.
 * 
 * When an Event is received from CBUS and we need to find its index within the 
 * EventTable it is firstly hashed using getHash(nn,en), trimmed to HASH_LENGTH 
 * and this is used as the first index into eventChains[][]. We then step through 
 * the second index of buckets within the chain. Each entry is an index into the 
 * eventtable and the eventtable's event field is checked to see if it matches the
 * received event. It it does match then the index into eventtable has been found 
 * and is returned. The EVs can then be accessed from the ev[] field.
 * 
 */


// forward definitions
static void teachFactoryReset(void);
static void teachPowerUp(void);
static Processed teachProcessMessage(Message * m);
static uint8_t teachGetESDdata(uint8_t id);
static void clearAllEvents(void);
Processed checkLen(Message * m, uint8_t needed, uint8_t service);
static Processed teachCheckLen(Message * m, uint8_t needed, uint8_t learn);
static uint8_t evtIdxToTableIndex(uint8_t evtIdx);
TimedResponseResult nerdCallback(uint8_t type, uint8_t serviceIndex, uint8_t step);
TimedResponseResult reqevCallback(uint8_t type, uint8_t serviceIndex, uint8_t step);
Boolean validStart(uint8_t tableIndex);
uint16_t getNN(uint8_t tableIndex);
uint16_t getEN(uint8_t tableIndex);
uint8_t numEv(uint8_t tableIndex);
int16_t getEv(uint8_t tableIndex, uint8_t evNum);
static uint8_t tableIndexToEvtIdx(uint8_t tableIndex);
uint8_t findEvent(uint16_t nodeNumber, uint16_t eventNumber);
static uint8_t removeTableEntry(uint8_t tableIndex);
uint8_t removeEvent(uint16_t nodeNumber, uint16_t eventNumber);
void checkRemoveTableEntry(uint8_t tableIndex);
static void doNnclr(void);
static void doNerd(void);
static void doNnevn(void);
static void doRqevn(void);
static void doNenrd(uint8_t index);
static void doReval(uint8_t enNum, uint8_t evNum);
static void doEvuln(uint16_t nodeNumber, uint16_t eventNumber);
static void doReqev(uint16_t nodeNumber, uint16_t eventNumber, uint8_t evNum);
static void doEvlrn(uint16_t nodeNumber, uint16_t eventNumber, uint8_t evNum, uint8_t evVal);

/**
 * The flags containing information about the event table entry.
 * A union provides access as bits or as a complete byte.
 */
typedef union
{
    struct
    {
        uint8_t    eVsUsed:4;  ///< How many of the EVs in this row are used. Only valid if continued is clear.
        uint8_t    continued:1;    ///< there is another entry.
        uint8_t    continuation:1; ///< Continuation of previous event entry.
        uint8_t    forceOwnNN:1;   ///< Ignore the specified NN and use module's own NN.
        uint8_t    freeEntry:1;    ///< this row in the table is not used - takes priority over other flags.
    };
    uint8_t    asByte;       ///< Set to 0xFF for free entry, initially set to zero for entry in use, then producer flag set if required.
} EventTableFlags;

/**
 * Defines a row within the Event table.
 * Each row consists of 1 byte of flags, an index into the table for the next
 * row if this event is spread over multiple rows, the event and the events EVs.
 */
typedef struct {
    EventTableFlags flags;          ///< put first so could potentially use the Event bytes for EVs in subsequent rows.
    uint8_t next;                   ///< index to continuation also indicates if entry is free.
    Event event;                    ///< the NN and EN.
    uint8_t evs[EVENT_TABLE_WIDTH]; ///< EVENT_TABLE_WIDTH is maximum of 15 as we have 4 bits of maxEvUsed.
} EventTable;

/** Byte index into an EventTable row to access the flags element.*/
#define EVENTTABLE_OFFSET_FLAGS    0
/** Byte index into an EventTable row to access the next element.*/
#define EVENTTABLE_OFFSET_NEXT     1
/** Byte index into an EventTable row to access the event nn element.*/
#define EVENTTABLE_OFFSET_NN       2
/** Byte index into an EventTable row to access the event en element.*/
#define EVENTTABLE_OFFSET_EN       4
/** Byte index into an EventTable row to access the event variables.*/
#define EVENTTABLE_OFFSET_EVS      6
/** Total number of bytes in an EventTable row.*/
#define EVENTTABLE_ROW_WIDTH       16

/** Represents an invalid index into the EventTable.*/
#define NO_INDEX            0xff

#ifdef VLCB_DIAG
static DiagnosticVal * teachGetDiagnostic(uint8_t code);
/**
 * The diagnostic values supported by the MNS service.
 */
static DiagnosticVal teachDiagnostics[NUM_TEACH_DIAGNOSTICS+1];
#endif

/**
 * The service descriptor for the event teach service. The application must include this
 * descriptor within the const Service * const services[] array and include the
 * necessary settings within module.h in order to make use of the event teach
 * service.
 */
const Service eventTeachService = {
    SERVICE_ID_OLD_TEACH,      // id
    2,                  // version
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

// Space for the event table and initialise to 0xFF
//static const uint8_t eventTable[NUM_EVENTS * EVENTTABLE_ROW_WIDTH] __at(EVENT_TABLE_ADDRESS) ={[0 ... NUM_EVENTS * EVENTTABLE_ROW_WIDTH-1] = 0xFF};

#ifdef EVENT_HASH_TABLE
uint8_t eventChains[EVENT_HASH_LENGTH][EVENT_CHAIN_LENGTH];
#ifdef EVENT_PRODUCED_EVENT_HASH
uint8_t happening2Event[2+MAX_HAPPENING-HAPPENING_BASE];
#endif
#endif

static uint8_t timedResponseOpcode; // used to differentiate a timed response for reqev AND reval

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
            
        // The following Opcodes use Index so shouldn't be here but FCU needs them
        case OPC_REVAL:     // 9C REVAL - NN, EN#, EV#
            if (teachCheckLen(m, 5, 0) == PROCESSED) return PROCESSED;
            if ((m->bytes[0] != nn.bytes.hi) || (m->bytes[1] != nn.bytes.lo)) return PROCESSED;  // not us
            // do REVAL
            doReval(m->bytes[2], m->bytes[3]);
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
static void clearAllEvents(void) {
    uint8_t tableIndex;
    for (tableIndex=0; tableIndex<NUM_EVENTS; tableIndex++) {
        // set the free flag
        writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex + EVENTTABLE_OFFSET_FLAGS, 0xff);
    }
    flushFlashBlock();
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
        EventTableFlags f;
        f.asByte = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*i+EVENTTABLE_OFFSET_FLAGS);
        if (f.freeEntry) {
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
    // if its not free and not a continuation then it is start of an event
    if (validStart(step)) {
        nodeNumber.word = getNN(step);
        eventNumber.word = getEN(step);
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
    if ( ! validStart(tableIndex)) {
        sendMessage3(OPC_CMDERR, nn.bytes.hi, nn.bytes.lo, CMDERR_INV_EN_IDX);
            // DEBUG  
//        cbusMsg[d7] = readNVM((uint16_t)(& (EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*i+EVENTTABLE_OFFSET_[tableIndex].flags.asByte))); 
//        cbusSendOpcMyNN( 0, OPC_ENRSP, cbusMsg );
        return;
    }
    nodeNumber = getNN(tableIndex);
    eventNumber = getEN(tableIndex);
    sendMessage7(OPC_ENRSP, nn.bytes.hi, nn.bytes.lo, nodeNumber>>8, nodeNumber&0xFF, eventNumber>>8, eventNumber&0xFF, tableIndex);   

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
        if (validStart(i)) {
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
    uint8_t error;
    evNum--;    // convert VLCB message numbering (starts at 1) to internal numbering)
    if (evNum >= PARAM_NUM_EV_EVENT) {
        sendMessage3(OPC_CMDERR, nn.bytes.hi, nn.bytes.lo, CMDERR_INV_EV_IDX);
#ifdef VLCB_GRSP
        sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_EVLRN, SERVICE_ID_OLD_TEACH, CMDERR_INV_EV_IDX);
#endif
        return;
    }
    // Normally this will be #defined in module.h to be addEvent() but the 
    // application may instead handle the adding of the event itself to 
    // perform any special processing and/or call addEvent() itself.
    error = APP_addEvent(nodeNumber, eventNumber, evNum, evVal, FALSE);
    if (error) {
        // validation error
        sendMessage3(OPC_CMDERR, nn.bytes.hi, nn.bytes.lo, error);
#ifdef VLCB_GRSP
        sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_EVLRN, SERVICE_ID_OLD_TEACH, error);
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
    
    if (evNum > PARAM_NUM_EV_EVENT) {
        sendMessage3(OPC_CMDERR, nn.bytes.hi, nn.bytes.lo, CMDERR_INV_EV_IDX);
        return;
    }

    evIndex = evNum-1U;    // Convert from CBUS numbering (starts at 1 for produced action))
    
    // check it is a valid index
    if (tableIndex < NUM_EVENTS) {
        if (validStart(tableIndex)) {
            int evVal;
            
            if (evNum == 0) {
                if ((mode_flags & FLAG_MODE_FCUCOMPAT) == 0) {
                    // send all of the EVs
                    // Note this somewhat abuses the type parameter
                    timedResponseOpcode = OPC_NEVAL;
                    startTimedResponse(tableIndex, findServiceIndex(SERVICE_ID_OLD_TEACH), reqevCallback);
                } 
                evVal = numEv(tableIndex);
            } else {
                evVal = getEv(tableIndex, evIndex);
            }
            
            if (evVal >= 0) {
                sendMessage5(OPC_NEVAL, nn.bytes.hi, nn.bytes.lo, enNum, evNum, (uint8_t)evVal);
                return;
            }
            // a negative value is the error code
            sendMessage3(OPC_CMDERR, nn.bytes.hi, nn.bytes.lo, (uint8_t)(-evVal));
            return;
        }
    }
    sendMessage3(OPC_CMDERR, nn.bytes.hi, nn.bytes.lo, CMDERR_INVALID_EVENT);
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
        if ((mode_flags & FLAG_MODE_FCUCOMPAT) == 0) {
            sendMessage6(OPC_EVANS, nodeNumber>>8, nodeNumber&0xFF, eventNumber>>8, eventNumber&0xFF, 0, numEv(tableIndex));
            // send all of the EVs
            // Note this somewhat abuses the type parameter
            timedResponseOpcode = OPC_EVANS;
            startTimedResponse(tableIndex, findServiceIndex(SERVICE_ID_OLD_TEACH), reqevCallback);
            return;
        }
        evVal = numEv(tableIndex);
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
 * The callback to do the REQEV and REVAL responses.
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
    EventTableFlags f;

#ifdef SAFETY
    if (tableIndex >= NUM_EVENTS) return CMDERR_INV_EV_IDX;
#endif
    if (validStart(tableIndex)) {
        f.asByte = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_FLAGS);
        // set the free flag
        writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_FLAGS, 0xff);
        // Now follow the next pointer
        while (f.continued) {
            tableIndex = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_NEXT);
            f.asByte = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_FLAGS);
        
            if (tableIndex >= NUM_EVENTS) return CMDERR_INV_EV_IDX; // shouldn't be necessary
                    
            // set the free flag
            writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_FLAGS, 0xff);
        
        }
        flushFlashBlock();
#ifdef EVENT_HASH_TABLE
        // easier to rebuild from scratch
        rebuildHashtable();
#endif
    }
    return 0;
}

/**
 * Check to see if any event entries can be removed. Entries can be removed if 
 * their EVs are all set to EV_FILL.
 * 
 * @param tableIndex
 */
void checkRemoveTableEntry(uint8_t tableIndex) {
    uint8_t e;
    
    if ( validStart(tableIndex)) {
        if (getEVs(tableIndex)) {
            return;
        }
        for (e=0; e<EVperEVT; e++) {
            if (evs[e] != EV_FILL) {
                return;
            }
        }
        removeTableEntry(tableIndex);
    }
}

/**
 * Add an event/EV.
 * Teach or re-teach an EV for an event. 
 * This may (optionally) need to create a new event and then optionally
 * create additional chained entries. All newly allocated table entries need
 * to be initialised.
 * 
 * @param nodeNumber Event NN
 * @param eventNumber Event NN
 * @param evNum the EV index (starts at 0 for the produced action)
 * @param evVal the EV value
 * @param forceOwnNN the value of the flag
 * @return error number or 0 for success
 */
uint8_t addEvent(uint16_t nodeNumber, uint16_t eventNumber, uint8_t evNum, uint8_t evVal, Boolean forceOwnNN) {
    uint8_t tableIndex;
    uint8_t error;
    // do we currently have an event
    tableIndex = findEvent(nodeNumber, eventNumber);
    if (tableIndex == NO_INDEX) {
        // Ian - 2k check for special case. Don't create an entry for a EV_FILL
        // This is a solution to the problem of FCU filling the event table with unused
        // 00 Actions. 
        // It does not fix a much less frequent problem of releasing some of the 
        // table entries when they are filled with No Action.
        if (evVal == EV_FILL) {
            return 0;
        }
        error = 1;
        // didn't find the event so find an empty slot and create one
        for (tableIndex=0; tableIndex<NUM_EVENTS; tableIndex++) {
            EventTableFlags f;
            f.asByte = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_FLAGS);
            if (f.freeEntry) {
                uint8_t e;
                // found a free slot, initialise it
                writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_NN, nodeNumber&0xFF);
                writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_NN+1, nodeNumber>>8);
                writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_EN, eventNumber&0xFF);
                writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_EN+1, eventNumber>>8);
                f.asByte = 0;
                f.forceOwnNN = forceOwnNN?1:0;
                writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_FLAGS, f.asByte);
            
                for (e = 0; e < EVENT_TABLE_WIDTH; e++) {
                    writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_EVS+e, EV_FILL);
                }
                error = 0;
                break;
            }
        }
        if (error) {
            return CMDERR_TOO_MANY_EVENTS;
        }
    }
 
    if (writeEv(tableIndex, evNum, evVal)) {
        // failed to write
        return CMDERR_INV_EV_IDX;
    }
    // success
    flushFlashBlock();
#ifdef EVENT_HASH_TABLE
    rebuildHashtable();
#endif
    return 0;
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
        EventTableFlags f;
        f.asByte = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_FLAGS);
        if (( ! f.freeEntry) && ( ! f.continuation)) {
            uint16_t node, en;
            node = getNN(tableIndex);
            en = getEN(tableIndex);
            if ((node == nodeNumber) && (en == eventNumber)) {
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
    EventTableFlags f;
    uint8_t startIndex = tableIndex;
    if (evNum >= PARAM_NUM_EV_EVENT) {
        return CMDERR_INV_EV_IDX;
    }
    while (evNum >= EVENT_TABLE_WIDTH) {
        uint8_t nextIdx;
        
        // skip forward looking for the right chained table entry
        evNum -= EVENT_TABLE_WIDTH;
        f.asByte = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_FLAGS);
        
        if (f.continued) {
            tableIndex = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_NEXT);
            if (tableIndex == NO_INDEX) {
                return CMDERR_INVALID_EVENT;
            }
        } else {
            // Ian - 2k check for special case. Don't create an entry for a EV_FILL
            // This is a solution to the problem of FCU filling the event table with unused
            // 00 Actions. 
            // It does not fix a much less frequent problem of releasing some of the 
            // table entries when they are filled with No Action.
            // don't add a new table slot just to store a EV_FILL
            if (evVal == EV_FILL) {
                return 0;
            }
            // find the next free entry
            for (nextIdx = tableIndex+1 ; nextIdx < NUM_EVENTS; nextIdx++) {
                EventTableFlags nextF;
                nextF.asByte = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*nextIdx+EVENTTABLE_OFFSET_FLAGS);
                if (nextF.freeEntry) {
                    uint8_t e;
                     // found a free slot, initialise it
                    writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*nextIdx+EVENTTABLE_OFFSET_NN, 0xff); // this field not used
                    writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*nextIdx+EVENTTABLE_OFFSET_NN+1, 0xff); // this field not used
                    writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*nextIdx+EVENTTABLE_OFFSET_EN, 0xff); // this field not used
                    writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*nextIdx+EVENTTABLE_OFFSET_EN+1, 0xff); // this field not used
                    writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*nextIdx+EVENTTABLE_OFFSET_FLAGS, 0x20);    // set continuation flag, clear free and numEV to 0
                    for (e = 0; e < EVENT_TABLE_WIDTH; e++) {
                        writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*nextIdx+EVENTTABLE_OFFSET_EVS+e, EV_FILL); // clear the EVs
                    }
                    // set the next of the previous in chain
                    writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_NEXT, nextIdx);
                    // set the continued flag
                    f.continued = 1;
                    writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_FLAGS, f.asByte);
                    tableIndex = nextIdx;
                    break;
                }
            }
            if (nextIdx >= NUM_EVENTS) {
                // ran out of table entries
                return CMDERR_TOO_MANY_EVENTS;
            }
        } 
    }
    // now write the EV
    writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_EVS+evNum, evVal);
    // update the number per row count
    f.asByte = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_FLAGS);
    if (f.eVsUsed <= evNum) {
        f.eVsUsed = evNum+1U;
        writeNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_FLAGS, f.asByte);
    }
    // If we are deleting then see if we can remove all
    if (evVal == EV_FILL) {
        checkRemoveTableEntry(startIndex);
    }
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
    EventTableFlags f;
    if ( ! validStart(tableIndex)) {
        // not a valid start
        return -CMDERR_INVALID_EVENT;
    }
    if (evNum >= PARAM_NUM_EV_EVENT) {
        return -CMDERR_INV_EV_IDX;
    }
    f.asByte = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_FLAGS);
    while (evNum >= EVENT_TABLE_WIDTH) {
        // if evNum is beyond current EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*i+EVENTTABLE_OFFSET_ entry move to next one
        if (! f.continued) {
            return -CMDERR_NO_EV;
        }
        tableIndex = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_NEXT);
        if (tableIndex == NO_INDEX) {
            return -CMDERR_INVALID_EVENT;
        }
        f.asByte = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_FLAGS);
        evNum -= EVENT_TABLE_WIDTH;
    }
    if (evNum+1 > f.eVsUsed) {
        if (f.continued) {
            return EV_FILL;
        }
        return -CMDERR_NO_EV;
    }
    // it is within this entry
    return (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_EVS+evNum);
}

/**
 * Return the number of EVs for an event.
 * 
 * @param tableIndex the index of the start of an event
 * @return the number of EVs
 */
uint8_t numEv(uint8_t tableIndex) {
    EventTableFlags f;
    uint8_t num=0;
    if ( ! validStart(tableIndex)) {
        // not a valid start
        return 0;
    }
    f.asByte = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_FLAGS);
    while (f.continued) {
        tableIndex = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_NEXT);
        if (tableIndex == NO_INDEX) {
            return 0;
        }
        f.asByte = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_FLAGS);
        num += EVENT_TABLE_WIDTH;
    }
    num += f.eVsUsed;
    return num;
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
    EventTableFlags f;
    uint8_t evNum;
    
    if ( ! validStart(tableIndex)) {
        // not a valid start
        return CMDERR_INVALID_EVENT;
    }
    for (evNum=0; evNum < PARAM_NUM_EV_EVENT; ) {
        uint8_t evIdx;
        for (evIdx=0; evIdx < EVENT_TABLE_WIDTH; evIdx++) {
            evs[evNum] = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_EVS+evIdx);
            evNum++;
        }
        f.asByte = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_FLAGS);
        if (! f.continued) {
            for (; evNum < PARAM_NUM_EV_EVENT; evNum++) {
                evs[evNum] = EV_FILL;
            }
            return 0;
        }
        tableIndex = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_NEXT);
        if (tableIndex == NO_INDEX) {
            return CMDERR_INVALID_EVENT;
        }
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
    EventTableFlags f;
    
    f.asByte = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_FLAGS);
    if (f.forceOwnNN) {
        return nn.word;
    }
    lo = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_NN);
    hi = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_NN+1);
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
    
    lo = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_EN);
    hi = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_EN+1);
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

/**
 * Checks if the specified index is the start of a set of linked entries.
 * 
 * @param tableIndex the index into event table to check
 * @return true if the specified index is the start of a linked set
 */
Boolean validStart(uint8_t tableIndex) {
    EventTableFlags f;
#ifdef SAFETY
    if (tableIndex >= NUM_EVENTS) return FALSE;
#endif
    f.asByte = (uint8_t)readNVM(EVENT_TABLE_NVM_TYPE, EVENT_TABLE_ADDRESS + EVENTTABLE_ROW_WIDTH*tableIndex+EVENTTABLE_OFFSET_FLAGS);
    if (( !f.freeEntry) && ( ! f.continuation)) {
        return TRUE;
    } else {
        return FALSE;
    }
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
 * Initialise the RAM hash chain for reverse lookup of event to action. Uses the
 * data from the Flash Event2Action table.
 */
void rebuildHashtable(void) {
    // invalidate the current hash table
    uint8_t hash;
    uint8_t chainIdx;
    uint8_t tableIndex;
    int a;
#ifdef EVENT_PRODUCED_EVENT_HASH
    // first initialise to nothing
    Happening happening;
    for (happening=0; happening<=(1+MAX_HAPPENING-HAPPENING_BASE); happening++) {
        happening2Event[happening] = NO_INDEX;
    }
#endif
    for (hash=0; hash<EVENT_HASH_LENGTH; hash++) {
        for (chainIdx=0; chainIdx < EVENT_CHAIN_LENGTH; chainIdx++) {
            eventChains[hash][chainIdx] = NO_INDEX;
        }
    }
    // now scan the event2Action table and populate the hash and lookup tables
    
    for (tableIndex=0; tableIndex<NUM_EVENTS; tableIndex++) {
        if (validStart(tableIndex)) {
            int16_t ev;
    
            // found the start of an event definition
#ifdef EVENT_PRODUCED_EVENT_HASH
            // ev[0] and ev[1] is used to store the Produced event's action
#if HAPPENING_SIZE == 2
            ev = getEv(tableIndex, 0);
            if (ev < 0) continue;
            happening.bytes.hi = (uint8_t) ev;
            ev = getEv(tableIndex, 1);
            if (ev < 0) continue;
            happening.bytes.lo = (uint8_t) ev;
#endif
#if HAPPENING_SIZE ==1
            // ev[0] is used to store the Produced event's action
            ev = getEv(tableIndex, 0);
            if (ev < 0) continue;
            happening = (uint8_t) ev;
#endif
            if ((happening<= MAX_HAPPENING) && (happening >= HAPPENING_BASE)) {
                happening2Event[happening-HAPPENING_BASE] = tableIndex;
            } 
#endif
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

