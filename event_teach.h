#ifndef _EVENT_TEACH_H_
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

  Ian Hogg Dec 2022
 */
#define _EVENT_TEACH_H_

#include "xc.h"
#include "module.h"
#include "vlcb.h"

/**
 * @file
 * @brief
 * Implementation of the VLCB Event Teach service.
 * @details
 * Event teaching service
 * The service definition object is called eventTeachService.
 *
 * The events are stored in non volatile memory (note flash is faster to read than EEPROM)
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
 * 
 * # Dependencies on other Services
 * Although the Event Teach service does not depend upon any other services all modules
 * must include the MNS service.
 * 
 * # Module.h definitions required for the Event Teach service
 * - \#define NUM_EVENTS          The number of rows in the event table. The
 *                        actual number of events may be less than this
 *                        if any events use more the 1 row.
 * - \#define EVENT_TABLE_ADDRESS   The address where the event table is stored. 
 * - \#define EVENT_TABLE_NVM_TYPE  Set to be either FLASH_NVM_TYPE or EEPROM_NVM_TYPE
 * 
 */
extern const Service eventTeachService;

/**
 * Function called before the EV is saved. This allows the application to perform additional
 * behaviour and to validate that the EV is acceptable.
 * @param nodeNumber the event's NN
 * @param eventNumber the event's EN
 * @param evNum the index of the EVs
 * @param evVal the EV value
 * @param forceOwnNN indicates whether the event's NN changes if the module's NN changes
 * @return error number or 0 for success
 */
extern uint8_t APP_addEvent(uint16_t nodeNumber, uint16_t eventNumber, uint8_t evNum, uint8_t evVal, Boolean forceOwnNN);

extern int16_t getEv(uint8_t tableIndex, uint8_t evIndex);
extern uint8_t getEVs(uint8_t tableIndex);
extern uint8_t evs[PARAM_NUM_EV_EVENT];
extern uint8_t writeEv(uint8_t tableIndex, uint8_t evNum, uint8_t evVal);
extern uint16_t getNN(uint8_t tableIndex);
extern uint16_t getEN(uint8_t tableIndex);
extern uint8_t findEvent(uint16_t nodeNumber, uint16_t eventNumber);
extern uint8_t addEvent(uint16_t nodeNumber, uint16_t eventNumber, uint8_t evNum, uint8_t evVal, Boolean forceOwnNN);
extern uint8_t addIndexedEvent(uint8_t enNum, uint8_t nnh, uint8_t nnl, uint8_t enh, uint8_t enl, uint8_t evNum, uint8_t evVal, Boolean forceOwnNN);

#ifdef EVENT_HASH_TABLE
extern void rebuildHashtable(void);
extern uint8_t getHash(uint16_t nodeNumber, uint16_t eventNumber);
#endif

/**
 * A structure to store the details of an event.
 * Contains the Node Number and the Event Number.
 */
typedef struct {
    uint16_t NN;    ///< The Node Number.
    uint16_t EN;    ///< The Event Number.
} Event;



/** Represents an invalid index into the EventTable.*/
#define NO_INDEX            0xff


// EVENT DECODING
//    An event opcode has bits 4 and 7 set, bits 1 and 2 clear
//    An ON event opcode also has bit 0 clear
//    An OFF event opcode also has bit 0 set
//
//  eg:
//  ACON  90    1001 0000
//  ACOF  91    1001 0001
//  ASON  98    1001 1000
//  ASOF  99    1001 1001
//
//  ACON1 B0    1011 0000
//  ACOF1 B1    1011 0001
//  ASON1 B8    1011 1000
//  ASOF1 B9    1011 1001
//
//  ACON2 D0    1101 0000
//  ACOF2 D1    1101 0001
//  ASON2 D8    1101 1000
//  ASOF2 D9    1101 1001
//
//  ACON3 F0    1111 0000
//  ACOF3 F1    1111 0001
//  ASON3 F8    1111 1000
//  ASOF3 F9    1111 1001
//
// ON/OFF         determined by d0
// Long/Short     determined by d3
// num data bytes determined by d7,d6
//
/** Mask used to determine whether an opcode is an event.*/
#define     EVENT_SET_MASK   0b10010000
/** Mask used to determine whether an opcode is an event.*/
#define     EVENT_CLR_MASK   0b00000110
/** Mask used to determine whether an opcode is an ON event.*/
#define     EVENT_ON_MASK    0b00000001
/** Mask used to determine whether an opcode is a Short event.*/
#define     EVENT_SHORT_MASK 0b00001000

#define NUM_TEACH_DIAGNOSTICS 1      ///< The number of diagnostic values associated with this service
#define TEACH_DIAG_COUNT              0x00 ///< Number of diagnostics
#define TEACH_DIAG_NUM_TEACH          0x01 ///< Number of teaches counter

#endif
