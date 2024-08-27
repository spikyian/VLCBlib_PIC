#ifndef _EVENT_PRODUCER_H_
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
#include "module.h"
#include "event_teach.h"

#define _EVENT_PRODUCER_H_
/**
 * @author Ian Hogg 
 * @date Dec 2022
 * 
 */ 

/**
 * @file
 * @brief
 * Implementation of the VLCB Event Producer service.
 * @details
 * Handle the production of events.
 * If EVENT_HASH_TABLE is defined then an additional lookup table 
 * uint8_t action2Event[NUM_HAPPENINGS] is used to obtain an Event 
 * using a Happening stored in the first EVs. This table is also populated using 
 * rebuildHashTable(). Given a Happening this table can be used to obtain the 
 * index into the EventTable for the Happening so the Event at that index in the 
 * EventTable can be transmitted.
 * 
 * # Dependencies on other Services
 * The Event Producer service depends upon the Event Teach service. The Event
 * Teach service MUST be included if the Event Producer Service is 
 * included by the module.
 * 
 * If only fixed events are to be produced so that event teaching is not required
 * then it is recommended to call sendMessage() specifying the necessary 
 * ACON/ACOF and ASON/ASOF event messages directly from the application.
 * 
 * # Module.h definitions required for the Event Producer service
 * - \#define PRODUCED_EVENTS    Always defined whenever the Event Producer service is included
 * - \#define HAPPENING_SIZE        Set to the number of bytes to hold a Happening.
 *                               Can be either 1 or 2.
 * 
 */

#if HAPPENING_SIZE == 2
typedef Word Happening;
#endif
#if HAPPENING_SIZE == 1
typedef uint8_t Happening;
#endif

extern const Service eventProducerService;

#ifdef EVENT_HASH_TABLE
extern uint8_t happening2Event[2+MAX_HAPPENING-HAPPENING_BASE];
#endif

#define NUM_PRODUCER_DIAGNOSTICS    1   ///< Number of diagnostics for this service
#define PRODUCER_DIAG_COUNT         0   ///< Number of diagnostics
#define PRODUCER_DIAG_NUMPRODUCED   1   ///< Number of events produced


extern Boolean sendProducedEvent(Happening h, EventState state);
extern void deleteHappeningRange(Happening happening, uint8_t number);

//AREQ stuff
/**
 * The application must provide a function to provide the current event state so
 * that the service can respond to AREQ/ASRQ requests.
 * @param h the Happening for which the state should be returned
 * @return the EventState for the specified Happening
 */
extern EventState APP_GetEventState(Happening h);

#endif
