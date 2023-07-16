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
 * Implementation of the VLCB Event Teach service.
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
 * define EVENT_HASH_TABLE to use event hash tables for fast access - at the expense of some RAM
 *
 * The code is responsible for storing EVs for each defined event and 
 * also for allowing speedy lookup of EVs given an Event or finding an Event given 
 * a Happening which is stored in the first EVs.
 * 
 * # Dependencies on other Services
 * Although the Event Teach service does not depend upon any other services all modules
 * must include the MNS service.
 * 
 * # Module.h definitions required for the Event Teach service
 * - #define EVENT_TABLE_WIDTH   This the the width of the table - not the 
 *                       number of EVs per event as multiple rows in
 *                       the table can be used to store an event.
 * - #define NUM_EVENTS          The number of rows in the event table. The
 *                        actual number of events may be less than this
 *                        if any events use more the 1 row.
 * - #define EVENT_TABLE_ADDRESS   The address where the event table is stored. 
 * - #define EVENT_TABLE_NVM_TYPE  Set to be either FLASH_NVM_TYPE or EEPROM_NVM_TYPE
 * - #define EVENT_HASH_TABLE      If defined then hash tables will be used for
 *                        quicker lookup of events at the expense of additional RAM.
 * - #define EVENT_HASH_LENGTH     If hash tables are used then this sets the length
 *                        of the hash.
 * - #define EVENT_CHAIN_LENGTH    If hash tables are used then this sets the number
 *                        of events in the hash chain.
 * - #define MAX_HAPPENING         Set to be the maximum Happening value
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
 * @return error number or 0 for success
 */
extern uint8_t APP_addEvent(uint16_t nodeNumber, uint16_t eventNumber, uint8_t evNum, uint8_t evVal);

extern Boolean validStart(uint8_t index);
extern int16_t getEv(uint8_t tableIndex, uint8_t evIndex);
extern uint16_t getNN(uint8_t tableIndex);
extern uint16_t getEN(uint8_t tableIndex);
extern uint8_t findEvent(uint16_t nodeNumber, uint16_t eventNumber);
extern uint8_t addEvent(uint16_t nodeNumber, uint16_t eventNumber, uint8_t evNum, uint8_t evVal, uint8_t forceOwnNN);
#ifdef EVENT_HASH_TABLE
extern void rebuildHashtable(void);
extern uint8_t getHash(uint16_t nodeNumber, uint16_t eventNumber);
#endif

#if HAPPENING_SIZE == 2
typedef Word Happening;
#endif
#if HAPPENING_SIZE == 1
typedef uint8_t Happening;
#endif


// A helper structure to store the details of an event.
typedef struct {
    uint16_t NN;
    uint16_t EN;
} Event;

// Indicates whether on ON event or OFF event
typedef enum {
    EVENT_OFF=0,
    EVENT_ON=1
} EventState;


typedef union
{
    struct
    {
        unsigned char eVsUsed:4;  // How many of the EVs in this row are used. Only valid if continued is clear
        uint8_t    continued:1;    // there is another entry 
        uint8_t    continuation:1; // Continuation of previous event entry
        uint8_t    forceOwnNN:1;   // Ignore the specified NN and use module's own NN
        uint8_t    freeEntry:1;    // this row in the table is not used - takes priority over other flags
    };
    uint8_t    asByte;       // Set to 0xFF for free entry, initially set to zero for entry in use, then producer flag set if required.
} EventTableFlags;

typedef struct {
    EventTableFlags flags;          // put first so could potentially use the Event bytes for EVs in subsequent rows.
    uint8_t next;                   // index to continuation also indicates if entry is free
    Event event;                    // the NN and EN
    uint8_t evs[EVENT_TABLE_WIDTH]; // EVENT_TABLE_WIDTH is maximum of 15 as we have 4 bits of maxEvUsed
} EventTable;

#define EVENTTABLE_OFFSET_FLAGS    0
#define EVENTTABLE_OFFSET_NEXT     1
#define EVENTTABLE_OFFSET_NN       2
#define EVENTTABLE_OFFSET_EN       4
#define EVENTTABLE_OFFSET_EVS      6
#define EVENTTABLE_ROW_WIDTH       16

#define NO_INDEX            0xff
#define EV_FILL             0xff

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
#define     EVENT_SET_MASK   0b10010000
#define     EVENT_CLR_MASK   0b00000110
#define     EVENT_ON_MASK    0b00000001
#define     EVENT_SHORT_MASK 0b00001000

#define NUM_TEACH_DIAGNOSTICS 16      ///< The number of diagnostic values associated with this service
#define TEACH_DIAG_NUM_TEACH          0x00 ///< Number of teaches counter

#endif
