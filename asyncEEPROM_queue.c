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

#ifdef ASYNC_EEPROM == QUEUE
/**
 * @file
 * @brief
 * Queued asynchronous EEPROM writer.
 * @details
 * Maintains a queue of EEPROM changes required, storing address and value. The changes are stored
 * to EEPROM asynchronously, one at a time, from pollAsyncEEPROM(). Each write is started and the
 * poll returns while the hardware does it; the next poll verifies the write and moves on.
 * A later write to a cell that is still queued replaces the queued value. A cell that already
 * holds the value is not written.
 *
 * Reads are served from the queue if the cell has a pending write, otherwise from EEPROM.
 * 
 * The amount of RAM used depends upon the size of the queue specified. When writing to the queue
 * if the queue is full then the queue is drained and the request is processed synchronously.
 * The size of the queue is specified by ASYNC_EEPROM_QUEUE_SIZE (2..255).
 *
 * With VLCB_VDD_GUARD the writer does not start a write while Vdd is below the HLVD level; an
 * entry held back for a second is dropped as refused (a synchronous write would refuse it too).
 *
 * Q83 only: the K80 write path waits for completion in hardware, so there is nothing to overlap.
 */

#if !defined(_18FXXQ83_FAMILY_)
#error "ASYNC_EEPROM=QUEUE is implemented for the Q83 family only"
#endif
#if !defined(ASYNC_EEPROM_QUEUE_SIZE)
#error "ASYNC_EEPROM=QUEUE needs ASYNC_EEPROM_QUEUE_SIZE (2..255) in module.h"
#endif
#if ((ASYNC_EEPROM_QUEUE_SIZE) < 2) || ((ASYNC_EEPROM_QUEUE_SIZE) > 255)
#error "ASYNC_EEPROM_QUEUE_SIZE must be 2..255 "
#endif
#include "ticktime.h"
#ifdef VLCB_DIAG
#include "mns.h"
#endif

#define NVM_ASYNC_DEPTH     ((uint8_t)(ASYNC_EEPROM_QUEUE_SIZE))
#define NVM_ASYNC_ATTEMPTS  3
#define NVM_ASYNC_WRAP(i)   ((uint8_t)(((uint16_t)(i)) % NVM_ASYNC_DEPTH))

/* Counters, readable by the application (e.g. for a diagnostic). */
uint16_t nvmAsyncWrites = 0;        ///< writes completed and verified
uint16_t nvmAsyncFailures = 0;      ///< entries dropped after NVM_ASYNC_ATTEMPTS failed writes
uint16_t nvmAsyncRefused = 0;       ///< entries dropped because Vdd stayed low (VLCB_VDD_GUARD)
uint16_t nvmAsyncFallbacks = 0;     ///< writes done synchronously because the queue was full
uint8_t  nvmAsyncHighWater = 0;     ///< most entries ever queued

static eeprom_address_t nvmAsyncAddr[NVM_ASYNC_DEPTH];
static eeprom_data_t    nvmAsyncVal[NVM_ASYNC_DEPTH];
static uint8_t  nvmAsyncHead = 0;
static uint8_t  nvmAsyncCount = 0;      // entries queued, including the one in flight
static uint8_t  nvmAsyncBusy = 0;       // head write started, not yet settled
static uint8_t  nvmAsyncAttempt = 0;    // failed attempts on the head so far
static TickValue nvmAsyncStart;         // when the head write started
#ifdef VLCB_VDD_GUARD
static uint8_t  nvmAsyncRefusing = 0;   // head refused by the VDD guard, waiting for the rail
static TickValue nvmAsyncRefusedSince;
#endif

#define NVM_SAT_INC16(c)    do { if ((c) != 0xFFFFu) (c)++; } while (0)

/**
 * Return the number of asynchronous writes still waiting to be written to EEPROM.
 */
uint8_t nvmAsyncPending(void) {
    return nvmAsyncCount;
}

/* 
#ifdef VLCB_VDD_GUARD
/**
 * Is Vdd above the HLVD level right now? 1 = yes (or the HLVD is not running).
 * The background writer never waits for the rail; it just tries again next poll.
 */
static uint8_t nvmVddOkNow(void) {
+   if (!HLVDCON0bits.EN || !HLVDCON0bits.RDY) return 1;
    return HLVDCON0bits.OUT ? 0 : 1;
}
#endif

/** 
 * The head write has finished (or timed out): verify it. 1 = good. 
 */
static uint8_t nvmFinishWrite(eeprom_address_t index, eeprom_data_t value, uint8_t timedOut) {
    uint8_t ok;
    while (NVMCON0bits.GO)      // cannot start another NVM operation until it ends
        ;
    ok = (!timedOut && !NVMCON1bits.WRERR) ? 1 : 0;
    NVMCON1bits.WRERR = 0;
    if (ok && (EEPROM_Read(index) != value)) {
        ok = 0;
    }
    return ok;
}

/**
 * Drop the head entry.
 */
static void nvmAsyncPop(void) {
    nvmAsyncHead = NVM_ASYNC_WRAP(nvmAsyncHead + 1u);
    nvmAsyncCount--;
    nvmAsyncAttempt = 0;
    nvmAsyncBusy = 0;
#ifdef VLCB_VDD_GUARD
    nvmAsyncRefusing = 0;
#endif
}

/**
 * Account for the result of the head write: pop it if good, retry it up to
 * NVM_ASYNC_ATTEMPTS times if not, then drop it as failed.
 */
static void nvmAsyncSettle(uint8_t ok) {
    if (ok) {
        NVM_SAT_INC16(nvmAsyncWrites);
        nvmAsyncPop();
    } else {
#ifdef VLCB_DIAG
        mnsDiagnostics[MNS_DIAGNOSTICS_MEMERRS].asUint++;
        updateModuleErrorStatus();
#endif
        if (++nvmAsyncAttempt >= NVM_ASYNC_ATTEMPTS) {
            NVM_SAT_INC16(nvmAsyncFailures);
            nvmAsyncPop();
        }
        // else: stays at the head, not busy - the next step retries it
    }
}

/**
 * Initialise the queue. Called from initRomOps().
 */
void initAsyncEEPROM(void) {
    nvmAsyncHead = 0;
    nvmAsyncCount = 0;
    nvmAsyncBusy = 0;
    nvmAsyncAttempt = 0;
#ifdef VLCB_VDD_GUARD
    nvmAsyncRefusing = 0;
#endif
}



/* 
 * One step of the writer: start the head write, or settle it when done.
 * Called from the library poll().
 */
void pollAsyncEEPROM(void) {
    uint8_t h;
    if (nvmAsyncCount == 0) return;
    h = nvmAsyncHead;
    if (!nvmAsyncBusy) {
        if ((nvmAsyncAttempt == 0) && (EEPROM_Read(nvmAsyncAddr[h]) == nvmAsyncVal[h])) {
            nvmAsyncPop();          // already holds the value: no write, no wear
            return;
        }
#ifdef VLCB_VDD_GUARD
        if (!nvmVddOkNow()) {
            if (!nvmAsyncRefusing) {
                nvmAsyncRefusing = 1;
                nvmAsyncRefusedSince.val = tickNowGet();
            } else if (tickTimeSinceNow(nvmAsyncRefusedSince) > ONE_SECOND) {
                NVM_SAT_INC16(nvmAsyncRefused);
                nvmAsyncPop();      // a rail that stays low must not pin the queue
            }
            return;
        }
        nvmAsyncRefusing = 0;
#endif
        // EEPROM_WriteNoVerify starts the write and returns while the hardware does it
        if (EEPROM_WriteNoVerify(nvmAsyncAddr[h], nvmAsyncVal[h]) != GRSP_OK) {
            return;                 // refused (VDD guard): try again next poll
        }
        nvmAsyncStart.val = tickNowGet();
        nvmAsyncBusy = 1;
        return;
    }
    if (NVMCON0bits.GO) {
        if (tickTimeSinceNow(nvmAsyncStart) < HUNDRED_MILI_SECOND) {
            return;                 // still writing - the normal case
        }
        nvmAsyncBusy = 0;
        nvmAsyncSettle(nvmFinishWrite(nvmAsyncAddr[h], nvmAsyncVal[h], 1));
        return;
    }
    nvmAsyncBusy = 0;
    nvmAsyncSettle(nvmFinishWrite(nvmAsyncAddr[h], nvmAsyncVal[h], 0));
}

/* 
 * Drain the queue, blocking. Never loops on a low rail: a refused entry is
 * dropped as refused (the synchronous writers would refuse it too). 
 */
void flushAsyncEEPROM(void) {
    uint8_t h;
    while (nvmAsyncCount != 0) {
        h = nvmAsyncHead;
        if (nvmAsyncBusy) {
            while (NVMCON0bits.GO)
                ;
            nvmAsyncBusy = 0;
            nvmAsyncSettle(nvmFinishWrite(nvmAsyncAddr[h], nvmAsyncVal[h], 0));
            continue;
        }
        if ((nvmAsyncAttempt == 0) && (EEPROM_Read(nvmAsyncAddr[h]) == nvmAsyncVal[h])) {
            nvmAsyncPop();
            continue;
        }
        if (EEPROM_WriteNoVerify(nvmAsyncAddr[h], nvmAsyncVal[h]) != GRSP_OK) {
            NVM_SAT_INC16(nvmAsyncRefused);     // VDD guard refused (it has already waited)
            nvmAsyncPop();
            continue;
        }
        while (NVMCON0bits.GO)
            ;
        nvmAsyncSettle(nvmFinishWrite(nvmAsyncAddr[h], nvmAsyncVal[h], 0));
    }
}

/**
 * Queue a byte write. Coalesces onto a waiting entry for the same cell. If
 * the queue is full it is drained first and this write is done synchronously.
 * @param address EEPROM address
 * @param data the value to be written
 * @return GRSP_OK, or the error from the synchronous write
 */
static uint8_t nvmAsyncQueue(eeprom_address_t address, eeprom_data_t data) {
    uint8_t i, k;
    /* coalesce onto a waiting entry for the same cell (not the in-flight head) */
    for (i = (nvmAsyncBusy ? 1u : 0u); i < nvmAsyncCount; i++) {
        k = NVM_ASYNC_WRAP(nvmAsyncHead + i);
        if (nvmAsyncAddr[k] == address) {
            nvmAsyncVal[k] = data;
            return GRSP_OK;
        }
    }
    if (nvmAsyncCount >= NVM_ASYNC_DEPTH) {
        NVM_SAT_INC16(nvmAsyncFallbacks);
        flushAsyncEEPROM();                             // older writes first, then this one in line
        return EEPROM_Write(address, data);
    }
    k = NVM_ASYNC_WRAP(nvmAsyncHead + nvmAsyncCount);
    nvmAsyncAddr[k] = address;
    nvmAsyncVal[k] = data;
    nvmAsyncCount++;
    if (nvmAsyncCount > nvmAsyncHighWater) {
        nvmAsyncHighWater = nvmAsyncCount;
    }
    return GRSP_OK;
}

/**
 * Read a byte: the newest queued value for the cell if there is one,
 * otherwise the EEPROM cell.
 * @param address EEPROM address
 * @return the byte
 */
uint8_t readAsyncEEPROM(eeprom_address_t address) {
    uint8_t i, k;
    for (i = nvmAsyncCount; i > 0; i--) {
        k = NVM_ASYNC_WRAP(nvmAsyncHead + (uint8_t)(i - 1u));
        if (nvmAsyncAddr[k] == address) {
            return nvmAsyncAddr[k];
        }
    }
    return EEPROM_Read(address);
}

#endif
