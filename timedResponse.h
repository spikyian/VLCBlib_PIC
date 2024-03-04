#ifndef TIMEDRESPONSE_H
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
	The FLiM routines have no code or definitions that are specific to any
	module, so they can be used to provide FLiM facilities for any module 
	using these libraries.
*/ 

/**
 * @author Ian Hogg 
 * 
 * Updates:
 *    Nov 2022    ih     Changed to support the requirements of VLCB
 * @date Dec 2022
 *  * Created on 08 December 2021, 16:19
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

#define	TIMEDRESPONSE_H

#ifdef	__cplusplus
extern "C" {
#endif

// When we need to send a number of responses delayed over a period of time
// These are the different timed response processes we can do
// Although considered converting these to an enum eventually decided against
// it as this would limit reuse.
#define TIMED_RESPONSE_SOD      1
#define TIMED_RESPONSE_NERD     2
#define TIMED_RESPONSE_RQSD     3
#define TIMED_RESPONSE_RDGN     4
#define TIMED_RESPONSE_REQEV    5
#define TIMED_RESPONSE_NVRD     6
#define TIMED_RESPONSE_RQNPN    7
#define TIMED_RESPONSE_NONE     0xFF // must be an invalid tableIndex so same as NO_INDEX
    
// The different APP callback responses
typedef enum {
    TIMED_RESPONSE_RESULT_FINISHED, // done everything - no need to call back again
    TIMED_RESPONSE_RESULT_RETRY,    // something went wrong, call back again later
    TIMED_RESPONSE_RESULT_NEXT      // not yet finished, call back again with next step
} TimedResponseResult;

/**
 * The callback function type.
 * Called every 10ms with step incrementing, starting at 0.
 */
typedef TimedResponseResult (* TimedResponseCallback)(uint8_t type, const Service * service, uint8_t step);   // Callback is  pointer to function returning uint8_t

/*
 * Initialsation routine.
 */
extern void initTimedResponse(void);

/*
 * Request callbacks at a regular rate.
 * @param type  indicate to the callback function the type of the callback.
 * @param serviceId passed to the user's callback function. If SERVICE_ID_ALL is 
 * passed then the callback is repeatedly for each service.
 * @param callback the user specific callback function
 */
extern void startTimedResponse(uint8_t type, uint8_t serviceIndex, TimedResponseResult (*callback)(uint8_t type, uint8_t si, uint8_t step));

/*
 * Call regularly to call the user's callback function. Handles the call back 
 * function's results to increment the step value and cycle through the services.
 */
extern void pollTimedResponse(void);

#ifdef	__cplusplus
}
#endif

#endif	/* TIMEDRESPONSE_H */

