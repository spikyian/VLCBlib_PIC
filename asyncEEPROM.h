#ifndef _ASYNCEEPROM_H_
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
#define _ASYNCEEPROM_H_
#include "vlcb.h"

/**
 * @file
 * @brief
 * Definitions and declarations for the asynchronous EEPROM writer.
 * @details
 * Two implementations exist. The application selects one by defining
 * ASYNC_EEPROM in module.h as either BUFFER or QUEUE; with ASYNC_EEPROM
 * undefined all EEPROM writes are synchronous as before.
 *
 * BUFFER (asyncEEPROM_buffer.c) keeps a RAM copy of the NUMBER_EEPROM bytes
 * from EEPROM_BASE_ADDRESS and writes changed bytes back one per poll.
 * Addresses outside that window are read and written synchronously.
 *
 * QUEUE (asyncEEPROM_queue.c) keeps a queue of ASYNC_EEPROM_QUEUE_SIZE
 * pending (address, value) writes covering the whole EEPROM, with reads
 * served from the queue first. Q83 only.
 *
 * Both are driven from the library: initRomOps() calls initAsyncEEPROM(),
 * poll() calls pollAsyncEEPROM(), readNVM()/writeNVM() route EEPROM
 * accesses through readAsyncEEPROM()/writeAsyncEEPROM(), and flushNVM()
 * calls flushAsyncEEPROM() before any RESET.
 */
/** Values for ASYNC_EEPROM. */
#define BUFFER  1
#define QUEUE   2

#ifdef ASYNC_EEPROM
#if (ASYNC_EEPROM != BUFFER) && (ASYNC_EEPROM != QUEUE)
#error "ASYNC_EEPROM must be BUFFER or QUEUE"
#endif
#endif

/**
 * Initialise the Async EEPROM writer. Called from initRomOps().
 */
extern void initAsyncEEPROM(void);

/**
 * The poll routine which will perform a write to the EEPROM if one is required.
 * Called from the library poll().
 */
extern void pollAsyncEEPROM(void);

/**
 * Read a byte of EEPROM, returning a pending (not yet written) value if there
 * is one, otherwise the value in the EEPROM cell.
 * @param address EEPROM address
 * @return the byte
 */
extern uint8_t readAsyncEEPROM(eeprom_address_t address);

/**
 * Write a byte of EEPROM. Normally records the write and returns at once;
 * the write happens later from pollAsyncEEPROM().
 * @param address EEPROM address
 * @param data the value to be written
 * @return GRSP_OK, or the error from a synchronous write when one was needed
 */
extern uint8_t writeAsyncEEPROM(eeprom_address_t address, uint8_t data);

/**
 * Write out all pending data to EEPROM. Blocks until done.
 */
extern void flushAsyncEEPROM(void);

#endif

