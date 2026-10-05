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
#include <xc.h>
#include "nvm.h"
#include "module.h"
#if ASYNC_EEPROM == BUFFER
/**
 * Maintains a RAM copy EEPROM so that changes can stored immediately and the application
 * does not need to wait. The write to EEPROM are handled asynchonously.
 * Reads of EEPROM should be served from the RAM buffer.
 *
 * The amount of RAM used can be minimised by ensuring that EEPROM usage is packed
 * together into a small range of addresses. The address range starts at 
 * EEPROM_BASE_ADDRESS and is of size NUMBER_EEPROM.
 */

static uint8_t writeNeeded[(NUMBER_EEPROM/8) + 1];
static uint8_t eeValue[NUMBER_EEPROM];
static uint8_t currentMemory;

#define clearWriteNeeded(b) (writeNeeded[b/8] &= ~(1<<(b%8)))
#define setWriteNeeded(b) (writeNeeded[b/8] |= (1<<(b%8)))
#define testWriteNeeded(b) ((writeNeeded[b/8] & (1<<(b%8))) != 0)

/**
 * Initialise the EEPROM writer.
 */
void initAsyncEEPROM(void) {
    for(currentMemory = 0; currentMemory < NUMBER_EEPROM; currentMemory++) {
        clearWriteNeeded(currentMemory);
        eeValue[currentMemory] = (uint8_t)readNVM(EEPROM_NVM_TYPE, EEPROM_BASE_ADDRESS+currentMemory);
    }
    currentMemory = 0;
}

/**
 * Request that EEPROM is written with the specified value to the supplied 
 * address. This does not actually perform a write but instead buffers the 
 * request and flags that the address needs to be written at a suitable time 
 * later.
 * 
 * @param address offset address in EEPROM
 * @param value the value to be written
 */
void writeAsyncEEPROM(eeprom_address_t address, uint8_t value) {
    if (address >= NUMBER_EEPROM) return;    // KeithB b33: bounds check
    if (eeValue[address] != value) {
        eeValue[address] = value;
        setWriteNeeded(address);
    }
}

/**
 * Read the contents of the EE value from RAM.
 * @param address
 * @return 
 */
uint8_t readAsyncEEPROM(eeprom_address_t address) {
    if (address >= NUMBER_EEPROM) return 0;  // KeithB b33: bounds check
    return eeValue[address];
}

/**
 * Waits for the NVM peripheral to be available then looks for the next 
 * EEPROM byte to be written and writes it to EEPROM.
 */
void pollAsyncEEPROM(void) {
    uint8_t i;
    
    // Is the NVM available to be used?
    if (NVMCON0 == 0) {
        // write the next
        for (i=0; i < NUMBER_EEPROM; i++) {
            currentMemory ++;
            if (currentMemory >= NUMBER_EEPROM) {
                currentMemory = 0;
            }
            if (testWriteNeeded(currentMemory)) {
                // start the write to EEPROM
                EEPROM_WriteNoVerify(EEPROM_BASE_ADDRESS + currentMemory, eeValue[currentMemory]);
                return;
            }
        }
        // No writes needed
    }
}

/**
 * Flush add data back to NVM.
 */
void flushAsyncEEPROM(void) {
    uint8_t i;
	for (i=0; i < NUMBER_EEPROM; i++) {
        if (testWriteNeeded(i)) {
            // start the write to EEPROM
            EEPROM_WriteNoVerify(EEPROM_BASE_ADDRESS + i, eeValue[i]);
            return;
        }
    }
}

#endif
