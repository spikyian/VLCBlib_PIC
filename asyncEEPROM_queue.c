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
 * Maintains a queue of EEPROM changes required, storing address and value. The changes are stored
 * to EEPROM asynchonously.
 *
 * Reads of EEPROM should be served from the RAM if available otherwise read from EEPROM.
 *
 * The amount of RAM used  depends upon the size of the queue specified. When writing to the queue
 * if the queue is full then the request is processed synchronously.
 * The size of the queue is specified by ASYNC_EEPROM_QUEUE_SIZE
 */

/*
+ * KeithB b47, LCR-004: background EEPROM writer (see nvm.h). Ported from the
+ * CanCan v4.63 writer. Q83 only: the K80 write path waits for completion in
+ * hardware, so there is nothing to overlap.
+ */
#if !defined(_18FXXQ83_FAMILY_)
#error "EEPROM_ASYNC=QUEUE is implemented for the Q83 family only"
#endif
#if ((ASYNC_EEPROM_QUEUE_SIZE) < 2) || ((ASYNC_EEPROM_QUEUE_SIZE) > 255)
#error "ASYNC_EEPROM_QUEUE_SIZE must be 2..255 "
#endif
#include "ticktime.h"

#define NVM_ASYNC_DEPTH     ((uint8_t)(ASYNC_EEPROM_QUEUE_SIZE))
#define NVM_ASYNC_ATTEMPTS  3
#define NVM_ASYNC_WRAP(i)   ((uint8_t)(((uint16_t)(i)) % NVM_ASYNC_DEPTH))

uint16_t nvmAsyncWrites = 0;
uint16_t nvmAsyncFailures = 0;
uint16_t nvmAsyncRefused = 0;
uint16_t nvmAsyncFallbacks = 0;
uint8_t  nvmAsyncHighWater = 0;

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
 * Start a byte write and return at once (the Q83 write runs in hardware). 
 */
static void nvmStartWrite(eeprom_address_t index, eeprom_data_t value) {
    uint8_t interruptEnabled;
    while (NVMCON0bits.GO)
        ;
    NVMCON1bits.WRERR = 0;
    NVMADRU = 0x38;
    NVMADRH = (uint8_t) (index >> 8);
    NVMADRL = (uint8_t) index;
    NVMDATL = value;
    NVMCON1bits.NVMCMD = NVMCMD_WRITE;
    interruptEnabled = geti();
    bothDi();
    NVMLOCK = 0x55;
    NVMLOCK = 0xAA;
    NVMCON0bits.GO = 1;
    if (interruptEnabled) {
        bothEi();
    }
}

/* 
 * The head write has finished (or timed out): verify it. 1 = good. 
 */
static uint8_t nvmFinishWrite(eeprom_address_t index, eeprom_data_t value, uint8_t timedOut) {
    uint8_t ok;
    if (timedOut) {
        while (NVMCON0bits.GO)      // cannot start another NVM operation until it ends
            ;
    }
    ok = (!timedOut && !NVMCON1bits.WRERR) ? 1 : 0;
    NVMCON1bits.WRERR = 0;
    NVMCON1bits.NVMCMD = NVMCMD_NOP;
    if (ok && (EEPROM_Read(index) != value)) {
        ok = 0;
    }
    NVMADR = 0;
    return ok;
}

/*
 * Obtain the next item to be written to EEPROM.
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

/*
 * ??
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

/* 
 * One step of the writer: start the head write, or settle it when done. 
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
                nvmAsyncRefusedSince.val = tickNowGet();   // same clock as tickTimeSinceNow (b40)
            } else if (tickTimeSinceNow(nvmAsyncRefusedSince) > ONE_SECOND) {
                NVM_SAT_INC16(nvmAsyncRefused);
                nvmAsyncPop();      // a rail that stays low must not pin the queue
            }
            return;
        }
        nvmAsyncRefusing = 0;
#endif
        nvmStartWrite(nvmAsyncAddr[h], nvmAsyncVal[h]);
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

/*
 * Write a single byte value asynchronously by adding to the queue.
 */
static uint8_t nvmAsyncQueue(eeprom_address_t index, eeprom_data_t value) {
    uint8_t i, k;
    /* coalesce onto a waiting entry for the same cell (not the in-flight head) */
    for (i = (nvmAsyncBusy ? 1u : 0u); i < nvmAsyncCount; i++) {
        k = NVM_ASYNC_WRAP(nvmAsyncHead + i);
        if (nvmAsyncAddr[k] == index) {
            nvmAsyncVal[k] = value;
            return GRSP_OK;
        }
    }
    if (nvmAsyncCount >= NVM_ASYNC_DEPTH) {
        NVM_SAT_INC16(nvmAsyncFallbacks);
        flushNVM();                             // older writes first, then this one in line
        return EEPROM_Write(index, value);
    }
    k = NVM_ASYNC_WRAP(nvmAsyncHead + nvmAsyncCount);
    nvmAsyncAddr[k] = index;
    nvmAsyncVal[k] = value;
    nvmAsyncCount++;
    if (nvmAsyncCount > nvmAsyncHighWater) {
        nvmAsyncHighWater = nvmAsyncCount;
    }
    return GRSP_OK;
}

/* A queued value for this cell, newest first; -1 if none is queued. */
static int16_t nvmAsyncLookup(eeprom_address_t index) {
    uint8_t i, k;
    for (i = nvmAsyncCount; i > 0; i--) {
        k = NVM_ASYNC_WRAP(nvmAsyncHead + (uint8_t)(i - 1u));
        if (nvmAsyncAddr[k] == index) {
            return (int16_t) nvmAsyncVal[k];
        }
    }
    return -1;
}


#endif
