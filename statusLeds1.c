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
 * @date Mar 2024
 * 
 */ 
#include "statusLeds.h"
#include "statusDisplay.h"

/**
 * LED handling.
 * 
 * This file supports modules with a single status LED. This is mormally a yellow LED
 * and maybe labelled FLiM.
 * The LED has a state here to indicate if it is on/off/flashing etc.
 * 
 * This looks a bit like a service but not exposed externally.
 * 
 */
LedState    ledState;     // the requested state
/**
 * Counters to control on/off period.
 */
static uint8_t flashCounter;     // update every 10ms
static TickValue ledTimer;

void resetLedFlashCounters(void) {
    flashCounter = 0;
}

void leds_powerUp(void) {
        // Set up the LEDs
    APP_setPortDirections();
    resetLedFlashCounters();
    ledTimer.val = tickGet();

}

void leds_poll(void) {
    if (tickTimeSince(ledTimer) > TEN_MILI_SECOND) {
        flashCounter++;
        ledTimer.val = tickGet();
    }

    // update the actual LEDs based upon their state
    switch (ledState) {
        case ON:
            APP_writeLED1(1);
            flashCounter = 0;
            break;
        case OFF:
            APP_writeLED1(0);
            flashCounter = 0;
            break;
        case FLASH_50_2HZ:
            // 1Hz (500ms per cycle is a count of 25 
            APP_writeLED1(flashCounter/25); 
            if (flashCounter >= 50) {
                flashCounter = 0;
            }
            break;
        case FLASH_50_1HZ:
            // 1Hz (500ms per cycle is a count of 50 
            APP_writeLED1(flashCounter/50); 
            if (flashCounter >= 100) {
                flashCounter = 0;
            }
            break;
        case FLASH_50_HALF_HZ:
            APP_writeLED1(flashCounter/100);
            if (flashCounter >= 200) {
                flashCounter = 0;
            }
            break;
        case SINGLE_FLICKER_ON:
            APP_writeLED1(1);
            if (flashCounter >= 25) {     // 250ms
                flashCounter = 0;
                ledState = OFF;
            }
            break;
        case SINGLE_FLICKER_OFF:
            APP_writeLED1(0);
            if (flashCounter >= 25) {     // 250ms
                flashCounter = 0;
                ledState = ON;
            }
            break;
        case LONG_FLICKER_ON:
            APP_writeLED1(1);
            if (flashCounter >= 50) {     // 500ms
                flashCounter = 0;
                ledState = OFF;
            }
            break;
        case LONG_FLICKER_OFF:
            APP_writeLED1(0);
            if (flashCounter >= 50) {     // 500ms
                flashCounter = 0;
                ledState = ON;
            }
            break;
        case OFF_1S:
            APP_writeLED1(0);
            if (flashCounter >= 100) {     // 500ms
                flashCounter = 0;
                ledState = ON;
            }
            break;
    }
}

void showStatus(StatusDisplay s) {
    switch (s) {
        case STATUS_OFF:
            ledState = OFF;
            break;
        case STATUS_UNINITIALISED:
            ledState = FLASH_50_HALF_HZ;
            break;
        case STATUS_SETUP:
            ledState = FLASH_50_1HZ;
            break;
        case STATUS_NORMAL:
        case STATUS_LEARN:
        case STATUS_BOOT:
            ledState = ON;
            break;
        case STATUS_MEMORY_FAULT:
        case STATUS_FATAL_ERROR:
        case STATUS_RESET_WARNING:
            ledState = FLASH_50_2HZ;
            break;
        case STATUS_MESSAGE_RECEIVED:
            ledState = SINGLE_FLICKER_OFF;
            break;
        case STATUS_MESSAGE_ACTED:
            ledState = LONG_FLICKER_OFF;
            break;
        case STATUS_TRANSMIT_ERROR:
        case STATUS_RECEIVE_ERROR:
            ledState = OFF_1S;
            break;
    }
}


