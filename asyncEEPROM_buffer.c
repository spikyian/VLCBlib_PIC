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
#include "vlcbdefs_enums.h"
#include "nvm.h"
#if defined(ASYNC_EEPROM) && (ASYNC_EEPROM == BUFFER)
/**
 * @file
 * @brief
 * Buffered asynchronous EEPROM writer.
 * @details
 * Maintains a RAM copy of EEPROM so that changes can be stored immediately and the application
 * does not need to wait. The writes to EEPROM are handled asynchronously from pollAsyncEEPROM(),
 * one byte per poll: the byte is written, and on the next poll it is read back and its flag is
 * cleared only if the read matches (a failed write is tried again).
 * Reads of buffered addresses are served from the RAM buffer.
 *
 * The amount of RAM used can be minimised by ensuring that EEPROM usage is packed
 * together into a small range of addresses. The buffered address range starts at
 * EEPROM_BASE_ADDRESS and is of size NUMBER_EEPROM. Addresses outside the range
 * (for example the library's NN, CANID, mode and NV bytes) are read and written
 * synchronously, so the buffer only needs to cover the application's own bytes
 * although the EEPROM used by the library may also be included.
 */
#if !defined(EEPROM_BASE_ADDRESS) || !defined(NUMBER_EEPROM)
#error "ASYNC_EEPROM=BUFFER needs EEPROM_BASE_ADDRESS and NUMBER_EEPROM in module.h"
#endif

/* Is an NVM write still in progress? The K80 EEPROM_WriteNoVerify() waits for
 * the write to finish, so there it never is. */
#if defined(_18FXXQ83_FAMILY_)
#define NVM_BUSY()  (NVMCON0bits.GO)
#else
#define NVM_BUSY()  (0)
#endif

static uint8_t writeNeeded[(NUMBER_EEPROM/8) + 1];
static uint8_t eeValue[NUMBER_EEPROM];
static uint8_t currentMemory;
static uint8_t writeCheckNeeded;    // currentMemory was written on the last poll: verify it now

#define clearWriteNeeded(b) (writeNeeded[(b)/8] &= (uint8_t)~(1u<<((b)%8)))
#define setWriteNeeded(b)   (writeNeeded[(b)/8] |= (uint8_t)(1u<<((b)%8)))
#define testWriteNeeded(b)  ((writeNeeded[(b)/8] & (1u<<((b)%8))) != 0)
#define inBuffer(address)   (((address) >= (EEPROM_BASE_ADDRESS)) && ((address) < (EEPROM_BASE_ADDRESS) + (NUMBER_EEPROM)))
/**
 * Initialise the EEPROM writer: load the buffer from EEPROM. Called from initRomOps().
 */
void initAsyncEEPROM(void) {
    for(currentMemory = 0; currentMemory < NUMBER_EEPROM; currentMemory++) {
        clearWriteNeeded(currentMemory);
        eeValue[currentMemory] = EEPROM_Read(EEPROM_BASE_ADDRESS + currentMemory);
    }
    currentMemory = 0;
    writeCheckNeeded = 0;
}

/**
 * Request that EEPROM is written with the specified value to the supplied 
 * address. For a buffered address this does not actually perform a write but
 * instead buffers the request and flags that the address needs to be written
 * at a suitable time later. Other addresses are written synchronously.
 * 
 * @param address EEPROM address
 * @param data the value to be written
 * @return GRSP_OK, or the error from a synchronous write
 */
uint8_t writeAsyncEEPROM(eeprom_address_t address, uint8_t data) {
    uint16_t offset;
    
    if (!inBuffer(address)) {
        // Synchronous call
        return EEPROM_Write(address, data);
    }
    offset = (uint8_t)(address - EEPROM_BASE_ADDRESS);
    if (eeValue[offset] != data) {
        eeValue[offset] = data;
        setWriteNeeded(address);
    }
    return GRSP_OK;
}

/**
 * Read the value of a buffered address from RAM, or any other address from EEPROM.
 * @param address
 * @return the EEPROM data
 */
uint8_t readAsyncEEPROM(eeprom_address_t address) {
    if (!inBuffer(address)) {
        // Synchronous read
        return EEPROM_Read(address);
    }
    return eeValue[address - EEPROM_BASE_ADDRESS];
}

/**
 * If the NVM peripheral is available then either verifies the byte
 * written on the previous poll, or looks for the next EEPROM byte to be
 * written and writes it to EEPROM. Called from the library poll().
 */
void pollAsyncEEPROM(void) {
    uint8_t i;
    
    // Is the NVM available to be used?
    if (NVM_BUSY()) return;
    if (writeCheckNeeded) {
        // Verify the last write; its flag stays set, and it is written again, if it failed.
        writeCheckNeeded = 0;
        if (EEPROM_Read(EEPROM_BASE_ADDRESS + currentMemory) == eeValue[currentMemory]) {
            clearWriteNeeded(currentMemory);
        }
        return;
    }
    // write the next
    for (i=0; i < NUMBER_EEPROM; i++) {
        currentMemory ++;
        if (currentMemory >= NUMBER_EEPROM) {
            currentMemory = 0;
        }
        if (testWriteNeeded(currentMemory)) {
            // start the write to EEPROM; verified on the next poll
            if (EEPROM_WriteNoVerify(EEPROM_BASE_ADDRESS + currentMemory, eeValue[currentMemory]) == GRSP_OK) {
                writeCheckNeeded = 1;
            }
            return;
        }
    }
    // No writes needed
}

/**
 * Flush add data back to NVM.
 * Write all flagged bytes back to EEPROM. Blocks until done. A byte that
 * still does not verify after being written is left flagged.
 */
void flushAsyncEEPROM(void) {
    uint8_t i;
    while (NVM_BUSY())
        ;
    if (writeCheckNeeded) {
        writeCheckNeeded = 0;
        if (EEPROM_Read(EEPROM_BASE_ADDRESS + currentMemory) == eeValue[currentMemory]) {
            clearWriteNeeded(currentMemory);
        }
    }
    for (i=0; i < NUMBER_EEPROM; i++) {
        if (testWriteNeeded(i)) {
            if (EEPROM_Write(EEPROM_BASE_ADDRESS + i, eeValue[i]) == GRSP_OK) {
                clearWriteNeeded(i);
            }
        }
    }
}

#endif
