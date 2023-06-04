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
**************************************************************************************************************
	
*/ 

#include <xc.h>
#include "vlcb.h"
#include "module.h"
#include "timedResponse.h"
#include "romops.h"

/**
 * @author Ian Hogg  Created 8 December 2021
 * @author Ian Hogg significant updates for VLCB and XC8 Dec 2022
 * @date Dec 2022
 * 
 */ 
/**
 * @file
 * TimedResponse records that a sequence of VLCB messages are to be sent at
 * a slow rate to allow receivers of these messages to process them.
 * @details
 * startTimedResponse() is to be called to start the transmission. A callback
 * function is provided and that function is called with an incrementing step 
 * value. The function must return a result to indicate that it has finished or 
 * that it still has more work to do.
 * 
 * Only one set of timedResponse can be running at a time. If a second 
 * timedResponse is started then the first will be abandoned and replaced
 * by the new one.
 * 
 * Call initTimedResponse() before other TimedResponse processing. 
 * This will set the current response set to be None.
 * Call pollTimedResponse() on a regular basis which will do the work needed for 
 * the next step and increments the step counter.
 * 
 * To start send a set of timed responses call startTimedResponse specifying
 * the callback.
 */
static uint8_t timedResponseServiceIndex;
static uint8_t timedResponseAllServicesFlag;
static uint8_t timedResponseType;
static uint8_t timedResponseStep;
static TimedResponseResult (*timedResponseCallback)(uint8_t type, uint8_t serviceIndex, uint8_t step);


/**
 * Initialise the timedResponse functionality.
 */
void initTimedResponse(void) {
    timedResponseType = TIMED_RESPONSE_NONE;
}

/**
 * Request callbacks at a regular rate.
 * 
 * @param type  indicate to the callback function the type of the callback.
 * @param serviceIndex passed to the user's callback function. 1..NUM_SERVICES. If SERVICE_ID_ALL is 
 * passed then the callback is repeatedly for each service.
 * @param callback the user specific callback function
 */
void startTimedResponse(uint8_t type, uint8_t serviceIndex, TimedResponseResult (*callback)(uint8_t type, uint8_t si, uint8_t step)) {
    timedResponseType = type;
    if (serviceIndex == SERVICE_ID_ALL) { 
        // go through all the services
        timedResponseAllServicesFlag = 1;
        timedResponseServiceIndex = 0;
    } else {
        timedResponseAllServicesFlag = 0;
        if ((serviceIndex < 1) || (serviceIndex > NUM_SERVICES)) {
            // if we don't have the requested service then don't do anything
            timedResponseType = TIMED_RESPONSE_NONE;
            return;
        }
        timedResponseServiceIndex = (uint8_t)serviceIndex-1;
    }
    timedResponseStep = 0;
    timedResponseCallback = callback;
}

/**
 * Call regularly to call the user's callback function. Handles the call back 
 * function's results to increment the step value and cycle through the services.
 */
void pollTimedResponse() {
    TimedResponseResult result;
    
    if (timedResponseType == TIMED_RESPONSE_NONE) {
        // no timed responses in progress
        return;
    }
    if (timedResponseCallback == NULL) {
        // no callback defined so finish
        timedResponseType = TIMED_RESPONSE_NONE;
        return;
    }

    // Now call the callback function
    result = (*timedResponseCallback)(timedResponseType, timedResponseServiceIndex, timedResponseStep);
    switch (result) {
        case TIMED_RESPONSE_RESULT_FINISHED:
            // the callback tells us it has finished but lets check if there are other
            // services still to do
            if (timedResponseAllServicesFlag) {
                // move on to next service
                timedResponseServiceIndex++;
                if (timedResponseServiceIndex >= NUM_SERVICES) {
                    // finished all the services
                    timedResponseType = TIMED_RESPONSE_NONE;
                } else {
                    timedResponseStep = 0;
                }
            } else {
                timedResponseType = TIMED_RESPONSE_NONE;
            }
            break;
        case TIMED_RESPONSE_RESULT_RETRY:
            break;
        case TIMED_RESPONSE_RESULT_NEXT:
            timedResponseStep++;
            break;
    }
}
