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
 * This file supports modules with two status LEDs. These are normally green and yellow.
 * The green LED may be labelled SLiM and the yellow LED labelled FLiM.
 * Each LED has a state here to indicate if it is on/off/flashing etc.
 * 
 * This looks a bit like a service but not exposed externally.
 * 
 */
LedState    ledState[2];     // the requested state
// LED identifiers
#define GREEN_LED   0
#define YELLOW_LED  1

/**
 * Counters to control on/off period.
 */
static uint8_t flashCounter[2];     // update every 10ms
static TickValue ledTimer;

void leds_powerUp(void) {
        // Set up the LEDs
    APP_setPortDirections();
    flashCounter[GREEN_LED] = 0;
    flashCounter[YELLOW_LED] = 0; 
    ledTimer.val = tickGet();

}

void leds_poll(void) {
    if (tickTimeSince(ledTimer) > TEN_MILI_SECOND) {
        flashCounter[GREEN_LED]++;
        flashCounter[YELLOW_LED]++;
        ledTimer.val = tickGet();
    }
    // update the actual LEDs based upon their state
    switch (ledState[YELLOW_LED]) {
        case ON:
            APP_writeLED2(1);
            flashCounter[YELLOW_LED] = 0;
            break;
        case OFF:
            APP_writeLED2(0);
            flashCounter[YELLOW_LED] = 0;
            break;
        case FLASH_50_2HZ:
            // 1Hz (500ms per on or off is a count of 25 
            APP_writeLED2(flashCounter[YELLOW_LED]/25); 
            if (flashCounter[YELLOW_LED] >= 50) {
                flashCounter[YELLOW_LED] = 0;
            }
            break;
        case FLASH_50_1HZ:
            // 1Hz (500ms per on or off is a count of 50 
            APP_writeLED2(flashCounter[YELLOW_LED]/50); 
            if (flashCounter[YELLOW_LED] >= 100) {
                flashCounter[YELLOW_LED] = 0;
            }
            break;
        case FLASH_50_HALF_HZ:
            APP_writeLED2(flashCounter[YELLOW_LED]/100);
            if (flashCounter[YELLOW_LED] >= 200) {
                flashCounter[YELLOW_LED] = 0;
            }
            break;
        case SINGLE_FLICKER_ON:
            APP_writeLED2(1);
            if (flashCounter[YELLOW_LED] >= 25) {     // 250ms
                flashCounter[YELLOW_LED] = 0;
                ledState[YELLOW_LED] = OFF;
            }
            break;
        case SINGLE_FLICKER_OFF:
            APP_writeLED2(0);
            if (flashCounter[YELLOW_LED] >= 25) {     // 250ms
                flashCounter[YELLOW_LED] = 0;
                ledState[YELLOW_LED] = ON;
            }
            break;
        case LONG_FLICKER_ON:
            APP_writeLED2(1);
            if (flashCounter[YELLOW_LED] >= 50) {     // 500ms
                flashCounter[YELLOW_LED] = 0;
                ledState[YELLOW_LED] = OFF;
            }
            break;
        case LONG_FLICKER_OFF:
            APP_writeLED2(0);
            if (flashCounter[YELLOW_LED] >= 50) {     // 500ms
                flashCounter[YELLOW_LED] = 0;
                ledState[YELLOW_LED] = ON;
            }
            break;
        case OFF_1S:
            APP_writeLED2(0);
            if (flashCounter[YELLOW_LED] >= 100) {     // 500ms
                flashCounter[YELLOW_LED] = 0;
                ledState[YELLOW_LED] = ON;
            }
            break;
    }

    switch (ledState[GREEN_LED]) {
        case ON:
            APP_writeLED1(1);
            flashCounter[GREEN_LED] = 0;
            break;
        case OFF:
            APP_writeLED1(0);
            flashCounter[GREEN_LED] = 0;
            break;
        case FLASH_50_2HZ:
            // 1Hz (500ms per cycle is a count of 25 
            APP_writeLED1(flashCounter[GREEN_LED]/25); 
            if (flashCounter[GREEN_LED] >= 50) {
                flashCounter[GREEN_LED] = 0;
            }
            break;
        case FLASH_50_1HZ:
            // 1Hz (500ms per cycle is a count of 50 
            APP_writeLED1(flashCounter[GREEN_LED]/50); 
            if (flashCounter[GREEN_LED] >= 100) {
                flashCounter[GREEN_LED] = 0;
            }
            break;
        case FLASH_50_HALF_HZ:
            APP_writeLED1(flashCounter[GREEN_LED]/100);
            if (flashCounter[GREEN_LED] >= 200) {
                flashCounter[GREEN_LED] = 0;
            }
            break;
        case SINGLE_FLICKER_ON:
            APP_writeLED1(1);
            if (flashCounter[GREEN_LED] >= 25) {     // 250ms
                flashCounter[GREEN_LED] = 0;
                ledState[GREEN_LED] = OFF;
            }
            break;
        case SINGLE_FLICKER_OFF:
            APP_writeLED1(0);
            if (flashCounter[GREEN_LED] >= 25) {     // 250ms
                flashCounter[GREEN_LED] = 0;
                ledState[GREEN_LED] = ON;
            }
            break;
        case LONG_FLICKER_ON:
            APP_writeLED1(1);
            if (flashCounter[GREEN_LED] >= 50) {     // 500ms
                flashCounter[GREEN_LED] = 0;
                ledState[GREEN_LED] = OFF;
            }
            break;
        case LONG_FLICKER_OFF:
            APP_writeLED1(0);
            if (flashCounter[GREEN_LED] >= 50) {     // 500ms
                flashCounter[GREEN_LED] = 0;
                ledState[GREEN_LED] = ON;
            }
            break;
        case OFF_1S:
            APP_writeLED1(0);
            if (flashCounter[GREEN_LED] >= 100) {     // 500ms
                flashCounter[GREEN_LED] = 0;
                ledState[GREEN_LED] = ON;
            }
            break;
    }
}


void showStatus(StatusDisplay s) {
    switch (s) {
        case STATUS_OFF:
            ledState[GREEN_LED] = OFF;
            ledState[YELLOW_LED] = OFF;
            break;
        case STATUS_UNINITIALISED:
            ledState[GREEN_LED] = ON;
            ledState[YELLOW_LED] = OFF;
            break;
        case STATUS_SETUP:
            ledState[GREEN_LED] = OFF;
            ledState[YELLOW_LED] = FLASH_50_1HZ;
            break;
        case STATUS_NORMAL:
        case STATUS_LEARN:
        case STATUS_BOOT:
        case STATUS_TRANSMIT_ERROR:
        case STATUS_RECEIVE_ERROR:
            ledState[GREEN_LED] = OFF;
            ledState[YELLOW_LED] = ON;
            break;
        case STATUS_RESET_WARNING:
            flashCounter[YELLOW_LED] = 0;
            flashCounter[GREEN_LED] = 25;
            ledState[GREEN_LED] = FLASH_50_2HZ;
            ledState[YELLOW_LED] = FLASH_50_2HZ;
            break;
        case STATUS_MESSAGE_RECEIVED:
            ledState[GREEN_LED] = SINGLE_FLICKER_ON;
            ledState[YELLOW_LED] = ON;
            break;
        case STATUS_MESSAGE_ACTED:
            ledState[GREEN_LED] = LONG_FLICKER_ON;
            ledState[YELLOW_LED] = ON;
            break;
        case STATUS_MEMORY_FAULT:
        case STATUS_FATAL_ERROR:
            flashCounter[YELLOW_LED] = 0;
            flashCounter[GREEN_LED] = 0;
            ledState[GREEN_LED] = FLASH_50_2HZ;
            ledState[YELLOW_LED] = FLASH_50_2HZ;
            break;
    }
}


