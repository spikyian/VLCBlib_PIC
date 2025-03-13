#ifndef _EVENT_TEACH_LARGE_H_
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

  Ian Hogg May 2024
 */
#define _EVENT_TEACH_LARGE_H_

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
 */

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
 * 
 * Do NOT allocate space for this table but instead define EVENT_TABLE_ADDRESS in your module.h
 * file and ensure that this space is not used by the Linker by setting this 
 * outside of the memory range defined in project properties
 * Properties->XC8 Global Options->XC8 Linker->memory model->ROM ranges
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

extern Boolean validStart(uint8_t index);
extern void checkRemoveTableEntry(uint8_t tableIndex);

#endif
