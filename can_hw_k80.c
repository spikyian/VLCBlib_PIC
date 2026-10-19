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
 * @file
 * @brief
 * CAN controller layer for the ECAN peripheral on the K series (PIC18F66K80 family).
 * @details
 * Opt-in (CAN_SHARED_SERVICE): included by can18_ecan.c, after its #includes,
 * ahead of can_service.c. Moves frames and counts what only the hardware sees;
 * every VLCB rule is in can_service.c. Interface: can_hw.h.
 *
 * The peripheral is set up as can18_ecan.c sets it up: ECAN mode 2, all eight
 * receive buffers as a FIFO with the high-water-mark interrupt, TXB0 for data
 * frames fed from a software queue by the TX interrupt, TXB1 for the RTR and
 * TXB2 for the reply to other nodes' RTRs.
 *
 * Differences from can18_ecan.c, all in this opt-in path only:
 *  - The high-water-mark interrupt moves raw frames (RTR and zero-length
 *    frames included) into a ring; the service sees every frame through one
 *    path. can18_ecan.c moved only data frames into its queue, so while the
 *    interrupt was draining the FIFO (under load) the module did not answer
 *    RTRs, collect enumeration replies or see a CANID conflict.
 *  - TXB1 (RTR) and TXB2 (RTR reply) carry all seven CANID bits.
 *    can18_ecan.c copied the low three bits from TXB0SIDL - the last data
 *    frame sent, or 0 after power-up.
 *  - The interrupt only touches the receive FIFO while the main loop allows
 *    it (FIFOWMIE), and only the transmit queue while TXBnIE is set; a TX or
 *    error interrupt no longer drains the receive FIFO under the main loop.
 *  - When the ring is full the interrupt stops (FIFOWMIE off, FIFOWMIF left
 *    set) instead of returning to be re-entered at once; the main loop then
 *    reads the hardware FIFO directly and re-enables it.
 *  - A transmit error while the main loop holds the queue (TXBnIE off) sets
 *    TXBnIF, so the next frame is started when TXBnIE comes back on.
 *  - Held frames do not count as waiting in canHwWaitTxDrain(), as on the Q83. A hardware overflow (RXBnOVFL) is
 *    counted as an RX overrun.
 *  - Frames held for a CANID (CAN_ADDITIONAL_CANID_CHECKS) stay in the queue
 *    until the service releases them.
 *  - A frame loaded into TXB0 directly (queue empty) is given the same
 *    CAN_TX_TIMEOUT as one loaded by the interrupt.
 * The ECAN behaviour relied on (FIFO pointer in CANCON, RXFUL release, buffer
 * register layout, TXREQ/TXLARB/TXERR) is as can18_ecan.c uses it.
 */
/* Compiled only through can18_can_2.c / can18_ecan.c with CAN_SHARED_SERVICE;
 * empty if a project builds this file on its own. */
#if defined(CAN_SHARED_SERVICE) && defined(_CAN_H_)
#include "can_hw.h"

#if !defined(CAN_CLOCK_MHz) || ((CAN_CLOCK_MHz != 16) && (CAN_CLOCK_MHz != 32) && (CAN_CLOCK_MHz != 64))
#error "CAN_CLOCK_MHz must be 16, 32 or 64"
#endif

/* Host test seam: lets a register model act on a write at once (RXFUL
 * release, a mode request), as the peripheral does. Empty on the PIC. */
#ifndef CAN_HW_SYNC
#define CAN_HW_SYNC()
#endif

//
// ECAN registers
//
#define CON     0
#define SIDH    1
#define SIDL    2
#define EIDH    3
#define EIDL    4
#define DLC     5
#define D0      6
#define D1      7
#define D2      8
#define D3      9
#define D4      10
#define D5      11
#define D6      12
#define D7      13

#ifndef CAN_HW_RX_RING
#define CAN_HW_RX_RING  CAN_NUM_RXBUFFERS   ///< frames the interrupt can hold for the main loop
#endif

/** A frame as the ECAN received it. */
typedef struct RawFrame {
    uint8_t sidh;
    uint8_t sidl;
    uint8_t dlc;            ///< as received: RTR bit 0x40, length 0x0F
    uint8_t d[8];
} RawFrame;

static RawFrame rxRing[CAN_HW_RX_RING];
static uint8_t rxRingRead;              ///< main loop only
static uint8_t rxRingWrite;             ///< interrupt only
static volatile uint8_t rxRingCount;    ///< both: changed only with FIFOWMIE off in the main loop
static uint8_t * rxHwBuffer;            ///< the ECAN buffer canHwRxPeek() returned, or NULL for the ring

static Message txBuffers[CAN_NUM_TXBUFFERS];
static MessageQueue txQueue;
static uint8_t txCanId;                 ///< the CANID frames are loaded into TXB0 with
static Boolean txHold;                  ///< frames are held until canHwTxRelease()

static TickValue  canTransmitTimeout;
static uint8_t  canTransmitFailed;

/*
 * VLCB priority to TXBnSIDH bits 7-4 (MjPri, MinPri). Same table as can18_ecan.c.
 */
static const uint8_t canPri[] = {
    0b10110000, // pLOW
    0b10100000, // pNORMAL
    0b10010000, // pABOVE
    0b10000000, // pHIGH
    0b00000000  // pSUPER
};
#define pSUPER  4   // Not message priority so supply here

static void checkTxFifo(void) __reentrant;
static void checkCANTimeout(void) __reentrant;
static void canTxError(void) __reentrant;
static void canFillRxRing(void) __reentrant;
static void loadTxb0(const Message * mp) __reentrant;

/**
 *  Set pointer to correct receive register set for incoming packet.
 */
static uint8_t * getBufferPointer(uint8_t b) {
    switch (b) {
        case 0:
            return (uint8_t*) & RXB0CON;
        case 1:
            return (uint8_t*) & RXB1CON;
        case 2:
            return (uint8_t*) & B0CON;
        case 3:
            return (uint8_t*) & B1CON;
        case 4:
            return (uint8_t*) & B2CON;
        case 5:
            return (uint8_t*) & B3CON;
        case 6:
            return (uint8_t*) & B4CON;
        case 7:
            return (uint8_t*) & B5CON;
        default:
            return NULL;
    }
}

void canHwInit(uint8_t canId) {
    // initialise the TX buffers
    txQueue.readIndex = 0;
    txQueue.writeIndex = 0;
    txQueue.messages = txBuffers;
    txQueue.size = CAN_NUM_TXBUFFERS;
    txCanId = canId;
    txHold = FALSE;
    rxRingRead = 0;
    rxRingWrite = 0;
    rxRingCount = 0;
    rxHwBuffer = NULL;

    canTransmitFailed=0;
    IPR5 = CAN_INTERRUPT_PRIORITY;    // CAN interrupts priority
    // Put module into Configuration mode.
    CANCON = 0b10000000;
    // ************************ Wait for config mode ***************************
    while (CANSTATbits.OPMODE2 == 0) {
        CAN_HW_SYNC();
    }

    ECANCON   = 0b10110000;   // ECAN mode 2 with FIFO, FIFOWM = 1 (init when four spaces left), init to first RX buffer
    BSEL0     = 0;            // Use all 8 buffers for receive FIFO, as they do not work as a FIFO for transmit
                              // (the FIFOWM comment is can18_ecan.c's: the level is unverified - bench item)

    // Bit timing as can18_ecan.c: Tq = 500 ns, 16 Tq per bit = 125 kbit/s
#if CAN_CLOCK_MHz==16
    BRGCON1 = 0B00000011;
#endif
#if CAN_CLOCK_MHz==32
    BRGCON1 = 0b00000111;
#endif
#if CAN_CLOCK_MHz==64
    BRGCON1 = 0b00001111;
#endif
    BRGCON2 = 0b10011110; // freely programmable, sample once, phase 1 = 4xTq, prop time = 7xTq
    BRGCON3 = 0b00000011; // Wake-up enabled, wake-up filter not used, phase 2 = 4xTq
    CIOCON    = 0b00100000;    // TX drives Vdd when recessive, CAN capture to CCP1 disabled

    /* Set the Filter and mask */
    // Setup mask 0 so all filter bits are ignored apart from EXIDEN
    RXM0SIDH = 0;
    RXM0SIDL = 0x08;    // include EXIDEN in mask
    RXM0EIDH = 0;
    RXM0EIDL = 0;
    // Set filter 0 for standard ID only to reject bootloader messages
    RXF0SIDL = 0x00;
    // Link filter to RXB0
    RXFBCON0 = 0;
    // Link filter to mask 0
    MSEL0 = 0;
    // Enable filter 0
    RXFCON0 = 1;

    // Clear RXFUL bits
    RXB0CON = 0x00;
    RXB1CON = 0x00;
    B0CON = 0;
    B1CON = 0;
    B2CON = 0;
    B3CON = 0;
    B4CON = 0;
    B5CON = 0;

    BIE0 = 0;                 // No Rx buffer interrupts (but we do use the high water mark interrupt)
    TXBIEbits.TXB0IE = 1;     // Tx buffer interrupts from buffer 0 only
    TXBIEbits.TXB1IE = 0;
    TXBIEbits.TXB2IE = 0;

    CANCON = 0;               // Set normal operation mode
    // ************************** Wait for normal mode *************************
    while (CANSTATbits.OPMODE2 != 0) {
        CAN_HW_SYNC();
    }

    // TXB0: VLCB data frames. CANID and priority are set when a frame is loaded.
    TXB0CON = 0;
    TXB0CONbits.TXPRI0 = 0;                           // sent after any self enumeration packets
    TXB0CONbits.TXPRI1 = 0;
    TXB0DLC = 0;
    TXB0SIDH = 0;
    TXB0SIDL = 0;

    // TXB1: the RTR that starts a self enumeration
    TXB1CON = 0;
    TXB1CONbits.TXPRI0 = 0;                           // before VLCB data, after enumeration replies
    TXB1CONbits.TXPRI1 = 1;
    TXB1DLC = 0x40;                                   // RTR packet with zero payload

    // TXB2: the zero length reply to another node's RTR
    TXB2CON = 0;
    TXB2CONbits.TXPRI0 = 1;                           // before any VLCB packets
    TXB2CONbits.TXPRI1 = 1;
    TXB2DLC = 0;                                      // Not RTR, zero payload

    canHwSetCanId(canId);

    canTransmitTimeout.val = 0;

    FIFOWMIE = 1;    // Enable Fifo 1 space left interrupt
    TXBnIE = 1;      // Enable the TX buffer transmission complete interrupt
    ERRIE = 1;       // Enable error interrupts
}

/**
 * The controller's interrupts. The receive FIFO is only touched while the
 * main loop allows it (FIFOWMIE), the transmit side only while TXBnIE is set.
 */
void canHwIsr(void) {
    if (FIFOWMIE && FIFOWMIF) {     // Receive buffer high water mark: move frames into the ring
        canFillRxRing();
    }
    if (ERRIF) {        //handle errors
        canTxError();
    }
    if (TXBnIE) {
        if (TXBnIF) {
            checkTxFifo();
        }
        checkCANTimeout();
    }
}

/**
 * Move every frame in the ECAN FIFO into the ring (interrupt).
 */
static void canFillRxRing(void) {
    uint8_t * ptr;
    RawFrame * e;

    while (COMSTATbits.NOT_FIFOEMPTY) {
        if (rxRingCount >= CAN_HW_RX_RING) {
            FIFOWMIE = 0;               // ring full: the main loop reads the FIFO itself
            return;
        }
        ptr = getBufferPointer(CANCON & 0x07U);
        if (ptr == NULL) {
            return;
        }
        RXBnIF = 0;
        if (RXBnOVFL) {
#ifdef VLCB_DIAG
            canServiceDiagnostics[CAN_DIAG_RX_BUFFER_OVERRUN].asUint++;
#endif
            RXBnOVFL = 0;
        }
        if ((ptr[SIDL] & 0x08) == 0) {  // standard frames only
            e = &rxRing[rxRingWrite];
            e->sidh = ptr[SIDH];
            e->sidl = ptr[SIDL];
            e->dlc  = ptr[DLC];
            e->d[0] = ptr[D0];
            e->d[1] = ptr[D1];
            e->d[2] = ptr[D2];
            e->d[3] = ptr[D3];
            e->d[4] = ptr[D4];
            e->d[5] = ptr[D5];
            e->d[6] = ptr[D6];
            e->d[7] = ptr[D7];
            if (++rxRingWrite >= CAN_HW_RX_RING) rxRingWrite = 0;
            rxRingCount++;
        }
        // Record and Clear any previous invalid message bit flag.
        if (IRXIF) {
#ifdef VLCB_DIAG
            canServiceDiagnostics[CAN_DIAG_RX_ERRORS].asUint++;
#endif
            IRXIF = 0;
        }
        // Mark that this buffer is read and empty.
        ptr[CON] &= 0x7f;
        CAN_HW_SYNC();
    }
    FIFOWMIF = 0;       // only once the FIFO is empty: a full ring leaves it set, so the
                        // interrupt comes back as soon as the main loop re-enables FIFOWMIE
}

Boolean canHwRxPeek(CanRxFrame * f) {
    uint8_t sidh, sidl, dlc;
    const uint8_t * data;

    FIFOWMIE = 0;   // the interrupt must not move frames while one is being looked at
    if (rxRingCount) {
        RawFrame * e = &rxRing[rxRingRead];     // the ring holds the older frames
        sidh = e->sidh;
        sidl = e->sidl;
        dlc = e->dlc;
        data = e->d;
        rxHwBuffer = NULL;
    } else if (COMSTATbits.NOT_FIFOEMPTY) {
        uint8_t * p = getBufferPointer(CANCON & 0x07);
        if (p == NULL) {
            FIFOWMIE = 1;
            return FALSE;
        }
        RXBnIF = 0;
        sidh = p[SIDH];
        sidl = p[SIDL];
        dlc = p[DLC];
        data = &p[D0];
        rxHwBuffer = p;
    } else {
        FIFOWMIE = 1;
        return FALSE;
    }
    f->canId = (uint8_t)(((sidh << 3) + (sidl >> 5)) & 0x7f);
    f->flags = (uint8_t)(((dlc & 0x40) ? CAN_RX_RTR : 0) | ((sidl & 0x08) ? CAN_RX_EXT : 0));
    f->dlc = dlc & 0x0F;
    if (f->dlc > 8) f->dlc = 8;
    f->data = data;
    return TRUE;
}

void canHwRxRelease(void) {
    if (rxHwBuffer != NULL) {
        // Record and Clear any previous invalid message bit flag.
        if (IRXIF) {
#ifdef VLCB_DIAG
            canServiceDiagnostics[CAN_DIAG_RX_ERRORS].asUint++;
#endif
            IRXIF = 0;
        }
        rxHwBuffer[CON] &= 0x7f;    // clear RXFUL bit
        CAN_HW_SYNC();
        rxHwBuffer = NULL;
    } else {
        if (++rxRingRead >= CAN_HW_RX_RING) rxRingRead = 0;
        rxRingCount--;
    }
    FIFOWMIE = 1;   // Re-enable FIFO interrupts now out of critical section
}

Boolean canHwTxSpace(void) {
    return (quantity(&txQueue) < (uint8_t)(CAN_NUM_TXBUFFERS - 1)) ? TRUE : FALSE;
}

/**
 * Load a frame into TXB0 and start it.
 */
static void loadTxb0(const Message * mp) {
    uint8_t len = (mp->len > 8) ? 8 : mp->len;
    TXB0SIDH = canPri[priorities[mp->opc]] | ((txCanId & 0x78) >> 3);
    TXB0SIDL = (uint8_t)((txCanId & 0x07) << 5);
    TXB0D0 = mp->opc;
    TXB0D1 = mp->bytes[0];
    TXB0D2 = mp->bytes[1];
    TXB0D3 = mp->bytes[2];
    TXB0D4 = mp->bytes[3];
    TXB0D5 = mp->bytes[4];
    TXB0D6 = mp->bytes[5];
    TXB0D7 = mp->bytes[6];
    TXB0DLC = len & 0x0F;  // Ensure not RTR
    canTransmitTimeout.val = tickGet();
    canTransmitFailed = 0;
    TXB0CONbits.TXREQ = 1;    // Initiate transmission
}

void canHwTxData(const Message * mp, uint8_t canId, Boolean hold) {
    TXBnIE = 0;      // keep the interrupt off the queue
    txCanId = canId;
    if (hold) {
        txHold = TRUE;
    }
    if (!txHold && (quantity(&txQueue) == 0) && (TXB0CONbits.TXREQ == 0)) {
        loadTxb0(mp);           // nothing waiting: send now
    } else {
        (void)push(&txQueue, (Message *)mp);    // canHwTxSpace() was checked
        if (!txHold && (TXB0CONbits.TXREQ == 0)) {
            checkTxFifo();      // TXB0 idle (an aborted frame): start the queue; sets TXBnIE
            return;
        }
    }
    TXBnIE = 1;
}

void canHwTxRelease(uint8_t canId) {
    TXBnIE = 0;
    txCanId = canId;
    txHold = FALSE;
    if (TXB0CONbits.TXREQ == 0) {
        checkTxFifo();          // start the first; the interrupt sends the rest
    } else {
        TXBnIE = 1;
    }
}

Boolean canHwTxHeld(void) {
    return (txHold && (quantity(&txQueue) > 0)) ? TRUE : FALSE;
}

void canHwSetCanId(uint8_t canId) {
    // all seven CANID bits: SIDH bits 3-0 = CANID 6-3, SIDL bits 7-5 = CANID 2-0
    TXB1SIDH = canPri[pSUPER] | ((canId & 0x78) >> 3);
    TXB1SIDL = (uint8_t)((canId & 0x07) << 5);
    TXB2SIDH = canPri[pSUPER] | ((canId & 0x78) >> 3);
    TXB2SIDL = (uint8_t)((canId & 0x07) << 5);
    txCanId = canId;
}

void canHwSendRtr(uint8_t canId) {
    TXB1SIDH = canPri[pSUPER] | ((canId & 0x78) >> 3);
    TXB1SIDL = (uint8_t)((canId & 0x07) << 5);
    TXB1CONbits.TXREQ = 1;              // Send RTR frame to initiate self enumeration
}

void canHwSendRtrReply(void) {
    TXB2CONbits.TXREQ = 1;              // zero payload frame preloaded in TXB2
}

TxDrainResult canHwWaitTxDrain(void) {
    TickValue start;
    start.val = tickGet();

    while ((TXB0CONbits.TXREQ != 0) || (!txHold && (quantity(&txQueue) > 0))) {   // held frames wait for a CANID, as on the Q83
        VLCB_CLRWDT();
        if (tickTimeSince(start) > TX_DRAIN_TIMEOUT_MS * ONE_MILI_SECOND) return OVERALL_TIMEOUT;   // busy or stuck bus
    }
    return DRAIN_OK;
}

/**
 * Called by ISR (or the main loop with TXBnIE off) to load the next queued
 * frame into TXB0.
 */
static void checkTxFifo( void ) {
    Message * mp;

    TXBnIF = 0;                 // reset the interrupt flag
    if (!TXB0CONbits.TXREQ) {
        mp = txHold ? NULL : pop(&txQueue);
        if (mp != NULL) {
            loadTxb0(mp);
            TXBnIE = 1;  // enable transmit buffer interrupt
        } else {
            // nothing to send (or held)
            canTransmitTimeout.val = 0;
            TXB0CON = 0;
            TXBnIF = 0;
            TXBnIE = 0;
        }
    } else {
        // still sending previous
        TXBnIF = 0;
        TXBnIE = 1;
    }
}

/**
 * Called by ISR regularly to check for timeout. If a buffer has been waiting too long
 * (CAN_TX_TIMEOUT) then this is counted as a transmit error and the next
 * frame is tried.
 */
static void checkCANTimeout(void) {
    if (canTransmitTimeout.val != 0) {
        if (tickTimeSince(canTransmitTimeout) > CAN_TX_TIMEOUT) {
            canTransmitFailed = 1;
            TXB0CONbits.TXREQ = 0;  // abort timed out packet
            checkTxFifo();          //  See if another packet is waiting to be sent
#ifdef VLCB_DIAG
            canServiceDiagnostics[CAN_DIAG_TX_ERRORS].asUint++;
            updateModuleErrorStatus();
#endif
        }
    }
}

/**
 * Process transmit error interrupt (as can18_ecan.c).
 */
static void canTxError(void) {
    if (TXB0CONbits.TXLARB) {  // lost arbitration
        canTransmitFailed = 1;
        canTransmitTimeout.val = 0;
        TXB0CONbits.TXREQ = 0;
#ifdef VLCB_DIAG
        canServiceDiagnostics[CAN_DIAG_LOST_ARBITRATION].asUint++;
        updateModuleErrorStatus();
#endif
    }
    if (TXB0CONbits.TXERR) {	// bus error
        canTransmitFailed = 1;
        canTransmitTimeout.val = 0;
        TXB0CONbits.TXREQ = 0;
#ifdef VLCB_DIAG
        canServiceDiagnostics[CAN_DIAG_TX_ERRORS].asUint++;
        updateModuleErrorStatus();
#endif
    }
    if (canTransmitFailed) {
        if (TXBnIE) {
            checkTxFifo();  // Check to see if more to try and send
        } else {
            TXBnIF = 1;     // the main loop has the queue: run checkTxFifo() as soon as it re-enables TXBnIE
        }
    }
    ERRIF = 0;
}

#ifdef VLCB_SERVICE
uint8_t canHwEsdType(void) {
    return CAN_HW_PIC_ECAN;
}
#endif

#ifdef VLCB_DIAG
uint8_t canHwTxInUse(void) {
    return quantity(&txQueue);
}

uint8_t canHwRxInUse(void) {
    uint8_t no;
    uint8_t i;
    uint8_t * ptr;

    no = rxRingCount;
    for (i=0; i<8; i++) {
        ptr = getBufferPointer(i);
        if (ptr[CON] & 0x80) {
            no++;
        }
    }
    return no;
}

void canHwDiagRefresh(uint8_t index) {
    if (index == CAN_DIAG_STATUS) {
        canServiceDiagnostics[CAN_DIAG_STATUS].asUint = COMSTAT;
    }
    // TX and RX errors are counted as they happen (canTxError, IRXIF)
}
#endif
#endif /* CAN_SHARED_SERVICE && _CAN_H_ */
