#ifndef _EVENT_CONSUMER_H_
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
#define _EVENT_CONSUMER_H_

#include "module.h"

/**
 * @file
 * @brief
 * Implementation of the VLCB Event Consumer service.
 * @details
 * The service definition object is called eventConsumerService.
 * Process consumed events. Processes Long and Short events.
 * Also handles events with data bytes if HANDLE_DATA_EVENTS is defined within 
 * module.h although the data is ignored.
 * If CONSUMER_EVS_AS_ACTIONS is defined then EVs after the Happening are treated
 * as Actions and are added to an Action queue to be processed by the application.
 * 
 * # Dependencies on other Services
 * The Event Consumer service depends upon the Event Teach service. The Event
 * Teach service MUST be included if the Event Consumer Service is 
 * included by the module.
 * 
 * If only fixed events are to be consumed so that event teaching is not required
 * then it is recommended to process the necessary ACON/ACOF and ASON/ASOF event
 * messages from the application's Processed APP_postProcessMessage(Message * m)
 * function.
 * 
 * # Module.h definitions required for the Event Consumer service
 * - \#define CONSUMED_EVENTS    Always defined whenever the Event Consumer service is included
 * - \#define HANDLE_DATA_EVENTS    Define if the ACON1/ACON2/ACON3 style events 
 *                               with data are to used in the same way as ACON 
 *                               style events.
 * - \#define COMSUMER_EVS_AS_ACTIONS Define if the EVs are to be treated to be Actions
 * - \#define ACTION_SIZE           The number of bytes used to hold an Action. 
 *                               Currently must be 1.
 * - \#define ACTION_QUEUE_SIZE     The size of the Action queue.
 * 
 */ 


extern const Service eventConsumerService;

#define NUM_CONSUMER_DIAGNOSTICS    1   ///< Number of diagnostics
#define CONSUMER_DIAG_NUMCONSUMED   0   ///< Number of events consumed

#if ACTION_SIZE == 1
#define Action uint8_t
#endif
#if ACTION_SIZE == 2
#define Action unit16_t
#endif

/**
 * Structure to store a conbination of an Action and an Event state.
 * Used to form a queue of Actions to be processed later.
 */
typedef struct {
    /** The state (ON/OFF) of the event associated with the Action.*/
    EventState state;
    union {
	/** The Action value. */
        Action value;
	/** The Action as an array of bytes.*/
        uint8_t bytes[ACTION_SIZE];
    } a;
} ActionAndState;

extern ActionAndState * popAction(void);
extern Boolean pushAction(ActionAndState a);
extern void deleteActionRange(Action action, uint8_t number);

#ifndef CONSUMER_EVS_AS_ACTIONS
/**
 * Callback into the Application to allow the Application to perform specialised processing of VLCB
 * events messages. This is only used if CONSUMER_EVS_AS_ACTIONS is not defined.
 */
extern void APP_processConsumedEvent(uint8_t tableIndex, Message * m);
#endif

#endif
