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
 * CAN controller layer for the CAN 2.0 peripheral on the Q series (PIC18F27Q83 family).
 * @details
 * Included by can18_can_2.c, after its #includes,
 * ahead of can_service.c. Moves frames and counts what only the hardware sees;
 * every VLCB rule is in can_service.c. Interface: can_hw.h.
 *
 * The peripheral is set up exactly as the earlier Q83 driver set it up (same FIFOs,
 * filter, bit timing and interrupts):
 *   TXQ   - 4 deep, the self-enumeration RTR
 *   FIFO1 - 1 deep, the zero-length reply to another node's RTR
 *   FIFO2 - 32 deep, VLCB data frames, sent in order
 *   FIFO3 - 32 deep, received standard frames
 * One difference: a data frame that is not held is queued with UINC and
 * TXREQ in a single write, as DS40002265C 38.5.4 requires ("UINC and TXREQ
 * ... must be set at the same time after appending a message").
 */
/* Compiled only through can18_can_2.c / can18_ecan.c;
 * empty if a project builds this file on its own. */
#if defined(_CAN_H_)
#include "can_hw.h"

#if !defined(CAN_CLOCK_MHz) || ((CAN_CLOCK_MHz != 16) && (CAN_CLOCK_MHz != 32) && (CAN_CLOCK_MHz != 64))
#error "CAN_CLOCK_MHz must be 16, 32 or 64"
#endif

/* Host test seam: lets a register model act on a write at once (UINC, TXREQ,
 * a mode request), as the peripheral does. Empty on the PIC. */
#ifndef CAN_HW_SYNC
#define CAN_HW_SYNC()
#endif

#ifndef CAN1_BUFFERS_BASE_ADDRESS
#define CAN1_BUFFERS_BASE_ADDRESS           0x3BB0  // Allows for 0x450 of CAN buffers (4+1+32+32)*(16) = 69*16 = 0x450
#endif
// High priority transmit queue
#define CAN1_TXQ_BUFFERS_BASE_ADDRESS       CAN1_BUFFERS_BASE_ADDRESS
#define CAN1_TXQ_PAYLOAD_SIZE       8
#define CAN1_TXQ_SIZE               4
// RTR response FIFO
#define CAN1_FIFO1_BUFFERS_BASE_ADDRESS     (CAN1_TXQ_BUFFERS_BASE_ADDRESS+((CAN1_TXQ_PAYLOAD_SIZE+8)*CAN1_TXQ_SIZE))
#define CAN1_FIFO1_PAYLOAD_SIZE     8
#define CAN1_FIFO1_SIZE             1
// Transmit FIFO
#define CAN1_FIFO2_BUFFERS_BASE_ADDRESS     (CAN1_FIFO1_BUFFERS_BASE_ADDRESS+((CAN1_FIFO1_PAYLOAD_SIZE+8)*CAN1_FIFO1_SIZE))
#define CAN1_FIFO2_PAYLOAD_SIZE     8
#define CAN1_FIFO2_SIZE             32
// Receive FIFO
#define CAN1_FIFO3_BUFFERS_BASE_ADDRESS     (CAN1_FIFO2_BUFFERS_BASE_ADDRESS+((CAN1_FIFO2_PAYLOAD_SIZE+8)*CAN1_FIFO2_SIZE))
#define CAN1_FIFO3_PAYLOAD_SIZE     8
#define CAN1_FIFO3_SIZE             32

#define PLSIZE_BITS(n)  (((n)<32) ? ((n)/4)-2 : ((n)==32) ? 5 : ((n)/16)+3)

/*
 * VLCB priority to the two identifier bytes: SID bits 7-10 (MjPri, MinPri).
 * Same table as can18_can_2.c.
 */
static const uint8_t canPri[] = {
    0b00001011, // pLOW
    0b00001010, // pNORMAL
    0b00001001, // pABOVE
    0b00001000, // pHIGH
    0b00000000  // pSUPER
};

/** Refill FIFO1 with the zero-length RTR reply carrying canId. */
static void prepareSelfEnumResponse(uint8_t canId) {
    uint8_t* txFifoObj = (uint8_t*) C1FIFOUA1;

    while (C1FIFOSTA1Lbits.TFNRFNIF == 1) { // FIFO1 not full
        txFifoObj[0] = (canId & 0x7F);      // Put ID
        txFifoObj[1] = 0;       // high priority
        txFifoObj[4] = 0;       // Standard frame, Zero data length DLC
        txFifoObj[5] = 0;       // No sequence number
        txFifoObj[6] = 0;       // No sequence number
        txFifoObj[7] = 0;       // No sequence number
        C1FIFOCON1Hbits.UINC = 1; // ready for future transmit
        CAN_HW_SYNC();
    }
}

#pragma warning disable 759
void canHwInit(uint8_t canId) {
    // initialise the CAN peripheral
    RB2PPS = 0x46;      // CANTX
    CANRXPPS = 013 ;    // octal 1=PORTB 3 = port B3
    TRISBbits.TRISB2 = 0;  // CAN TX output
    TRISBbits.TRISB3 = 1;  // CAN RX input
    IPR5 = CAN_INTERRUPT_PRIORITY;    // CAN interrupts priority
    /* Enable the CAN module */
    C1CONHbits.ON = 1;

    // Put module into Configuration mode.
    if (CAN_OP_MODE_REQUEST_SUCCESS == CAN1_OperationModeSet(CAN_CONFIGURATION_MODE)) {
        /* Initialise the C1FIFOBA with the start address of the CAN FIFO message object area. */
        C1FIFOBA = CAN1_BUFFERS_BASE_ADDRESS;

        C1CONL = 0x00;      // CLKSEL0 disabled; DeviceNet filter disabled
        C1CONH = 0x87;      // ON enabled; SIDL disabled; BUSY disabled; WFT T11 Filter; WAKFIL enabled;
        C1CONU = 0x10;      // TXQEN enabled; STEF disabled; SERR2LOM disabled; ESIGM disabled; RTXAT disabled;
        C1CONT = 0x54;      // TXBWS=5; ABAT=0; REQOP=4 (stay in Configuration mode)
        C1NBTCFGL = 0x00;   // SJW 1;
        C1NBTCFGH = 0x03;   // TSEG2 4;
        C1NBTCFGU = 0x02;   // TSEG1 3;
        C1NBTCFGT = (uint8_t)(CAN_CLOCK_MHz - 1);   // BRP: 1 us Tq, 8 Tq = 125 kbit/s (0x3F at 64 MHz)
        // Used to transmit the RTR self enum request
        C1TXQCONL = 0x10;   // TXATIE enabled; TXQEIE disabled; TXQNIE disabled;
        C1TXQCONH = 0x04;   // FRESET enabled; UINC disabled;
        C1TXQCONU = 0x6F;   // TXAT unlimited ; TXPRI 15 (high);
        C1TXQCONT = (PLSIZE_BITS(CAN1_TXQ_PAYLOAD_SIZE) << 5) | (CAN1_TXQ_SIZE-1);   // PLSIZE 8; FSIZE 4;

        // used to transmit the zero data length response to RTR
        C1FIFOCON1L = 0x80; // TXEN enabled; RTREN enabled; RXTSEN disabled; TXATIE disabled; RXOVIE disabled; TFERFFIE enabled; TFHRFHIE disabled; TFNRFNIE disabled;
        C1FIFOCON1H = 0x04; // FRESET enabled; TXREQ disabled; UINC disabled;
        C1FIFOCON1U = 0x6F; // TXAT unlimited retransmission attempts; TXPRI 15 (high);
        C1FIFOCON1T = (PLSIZE_BITS(CAN1_FIFO1_PAYLOAD_SIZE) << 5) | (CAN1_FIFO1_SIZE-1);// PLSIZE 8; FSIZE 1;

        // Normal TX FIFO
        C1FIFOCON2L = 0x80; // TXEN enabled; RTREN disabled; RXTSEN disabled; TXATIE disabled; RXOVIE disabled; TFERFFIE disabled; TFHRFHIE disabled; TFNRFNIE disabled;
        C1FIFOCON2H = 0x04; // FRESET enabled; TXREQ disabled; UINC disabled;
        C1FIFOCON2U = 0x60; // TXAT unlimited retransmission attempts; TXPRI 0 (low);
        C1FIFOCON2T = (PLSIZE_BITS(CAN1_FIFO2_PAYLOAD_SIZE) << 5) | (CAN1_FIFO2_SIZE-1);// PLSIZE 8; FSIZE 32;

        // Normal RX FIFO
        C1FIFOCON3L = 0x08; // TXEN disabled; RTREN disabled; RXTSEN disabled; TXATIE disabled; RXOVIE enabled; TFERFFIE disabled; TFHRFHIE disabled; TFNRFNIE disabled;
        C1FIFOCON3H = 0x04; // FRESET enabled; TXREQ disabled; UINC disabled;
        C1FIFOCON3U = 0x00; // TXAT retransmission disabled; TXPRI 0;
        C1FIFOCON3T = (PLSIZE_BITS(CAN1_FIFO3_PAYLOAD_SIZE) << 5) | (CAN1_FIFO3_SIZE-1); // PLSIZE 8; FSIZE 32;

        // Filter 0 for All Normal messages
        C1FLTOBJ0L = 0x00;
        C1FLTOBJ0H = 0x00;
        C1FLTOBJ0U = 0x00;
        C1FLTOBJ0T = 0x00;  // EXIDE clear: allow standard ID only
        C1MASK0L = 0x00;
        C1MASK0H = 0x00;
        C1MASK0U = 0x00;
        C1MASK0T = 0x40;    // MIDE set: filter on EXIDE
        C1FLTCON0L = 0x83;  // FLTEN0 enabled; F1BP FIFO 3 - the normal RX FIFO

        /* Place CAN1 module in Normal Operation mode */
        (void)CAN1_OperationModeSet(CAN_NORMAL_2_0_MODE);
    }

    // Preload FIFO1 with a zero length packet containing CANID for  use in self enumeration
    prepareSelfEnumResponse(canId);

    IPR0bits.CANIP = 0;
    PIR0bits.CANIF = 0;
    C1INTTbits.RXOVIE = 1;        // enable receive overrun interrupt
    C1INTTbits.IVMIE = 1;         // Enable interrupts for invalid message

    PIE0bits.CANIE = 1; // Enable the CAN master interrupt
}

/**
 * Handle the RX overrun and receive error interrupts.
 */
void __interrupt(irq(IRQ_CAN), base(IVT_BASE), low_priority) receiveOverrun(void) {
    if (C1FIFOSTA3Lbits.RXOVIF == 1) {
#ifdef VLCB_DIAG
        canServiceDiagnostics[CAN_DIAG_RX_BUFFER_OVERRUN].asUint++;
#endif
        C1FIFOSTA3Lbits.RXOVIF = 0;
    }
    if (C1INTHbits.IVMIF == 1) {
#ifdef VLCB_DIAG
        canServiceDiagnostics[CAN_DIAG_RX_ERRORS].asUint++;
#endif
        C1INTHbits.IVMIF = 0;
    }
}

Boolean canHwRxPeek(CanRxFrame * f) {
    uint8_t* rxFifoObj;

    if (! C1FIFOSTA3Lbits.TFNRFNIF) {   // RX FIFO empty (DS40002265C 38.8.1.1)
        return FALSE;
    }
    rxFifoObj = (uint8_t*) C1FIFOUA3;   // Pointer to FIFO entry
    f->canId = rxFifoObj[0] & 0x7F;
    /* !!! As can18_can_2.c: RTR, IDE and DLC are read from byte 4, although
     * Table 38-8 places them in byte 5. Inherited from the working driver. */
    f->flags = (uint8_t)(((rxFifoObj[4] & 0x20) ? CAN_RX_RTR : 0) | ((rxFifoObj[4] & 0x10) ? CAN_RX_EXT : 0));
    f->dlc = rxFifoObj[4] & 0x0F;
    if (f->dlc > 8) f->dlc = 8;
    f->data = &rxFifoObj[8];
    return TRUE;
}

void canHwRxRelease(void) {
    C1FIFOCON3Hbits.UINC = 1;   // Indicate that we have got the message from FIFO
    CAN_HW_SYNC();
}

Boolean canHwTxSpace(void) {
    return C1FIFOSTA2Lbits.TFNRFNIF ? TRUE : FALSE;
}

void canHwTxData(const Message * mp, uint8_t canId, Boolean hold) {
    uint8_t* txFifoObj;
    uint8_t pri;

#ifdef VLCB_DIAG
    // Did previous TX fail arbitration? (as can18_can_2.c)
    if (C1FIFOSTA2Lbits.TXLARB == 1) {
        canServiceDiagnostics[CAN_DIAG_LOST_ARBITRATION].asUint++;
    }
#endif
    txFifoObj = (uint8_t*) C1FIFOUA2;
    pri = canPri[priorities[mp->opc]];
    txFifoObj[0] = (uint8_t)((pri & 1) << 7) | (canId & 0x7F);      // Put ID
    txFifoObj[1] = pri >> 1;
    txFifoObj[4] = (mp->len&0xF);       // Standard frame, length in DLC
    txFifoObj[5] = 0;       // No sequence number
    txFifoObj[6] = 0;       // No sequence number
    txFifoObj[7] = 0;       // No sequence number
    txFifoObj[8] = (uint8_t)(mp->opc);  // opcode
    txFifoObj[9]  = mp->bytes[0];  // data1
    txFifoObj[10] = mp->bytes[1];  // data2
    txFifoObj[11] = mp->bytes[2];  // data3
    txFifoObj[12] = mp->bytes[3];  // data4
    txFifoObj[13] = mp->bytes[4];  // data5
    txFifoObj[14] = mp->bytes[5];  // data6
    txFifoObj[15] = mp->bytes[6];  // data7
    if (hold) {
        C1FIFOCON2H |= _C1FIFOCON2H_UINC_MASK;                          // queued, not requested
    } else {
        C1FIFOCON2H |= (_C1FIFOCON2H_UINC_MASK | _C1FIFOCON2H_TXREQ_MASK);   // 38.5.4: together
    }
    CAN_HW_SYNC();
}

void canHwTxRelease(uint8_t canId) {
    uint8_t i;
    // put the CANID into all the transmit buffers, keeping the priority bit
    for (i=0; i< CAN1_FIFO2_SIZE; i++) {
        uint8_t * p = (uint8_t*)(CAN1_FIFO2_BUFFERS_BASE_ADDRESS + (i* (8 + CAN1_FIFO2_PAYLOAD_SIZE)));
        *p = (uint8_t)((*p & 0x80) | (canId & 0x7f));
    }
    C1FIFOCON2H |= _C1FIFOCON2H_TXREQ_MASK; // now send them all
    CAN_HW_SYNC();
}

Boolean canHwTxHeld(void) {
    return (!C1FIFOSTA2Lbits.TFERFFIF && !C1FIFOCON2Hbits.TXREQ) ? TRUE : FALSE;
}

void canHwSetCanId(uint8_t canId) {
    // FIFO1 has one slot, so the reply is always at its base address. It stays
    // loaded (full) between replies, so patch the CANID in place.
    *((uint8_t*)CAN1_FIFO1_BUFFERS_BASE_ADDRESS) = (uint8_t)(canId & 0x7F);
}

void canHwSendRtr(uint8_t canId) {
    uint8_t* txFifoObj = (uint8_t*) C1TXQUA;
    txFifoObj[0] = (canId & 0x7F);      // Put ID
    txFifoObj[1] = 0;       // high priority
    txFifoObj[4] = 0x20;    // Standard frame, RTR, Zero data length DLC
    C1TXQCONH |= (_C1TXQCONH_TXREQ_MASK | _C1TXQCONH_UINC_MASK); // transmit
    CAN_HW_SYNC();
}

void canHwSendRtrReply(void) {
    C1FIFOCON1H |= (_C1FIFOCON1H_TXREQ_MASK | _C1FIFOCON1H_UINC_MASK); // transmit
    CAN_HW_SYNC();
}

TxDrainResult canHwWaitTxDrain(void) {
    TickValue start;
#ifdef STRINGENT_TX_QUEUE_DRAIN
    TickValue lastProgress;
    uint8_t lastCi = C1FIFOSTA2Hbits.FIFOCI;       // index of next frame to transmit
#endif
    start.val = tickGet();
#ifdef STRINGENT_TX_QUEUE_DRAIN
    lastProgress.val = start.val;
#endif
    while (C1FIFOCON2Hbits.TXREQ) {
        VLCB_CLRWDT();
#ifdef STRINGENT_TX_QUEUE_DRAIN
        if (C1FIFOSTA2Hbits.FIFOCI != lastCi) {     // a frame went out
            lastCi = C1FIFOSTA2Hbits.FIFOCI;
            lastProgress.val = tickGet();
        }
        if (C1TRECUbits.TXBO) return BUS_OFF_ERROR;                // bus-off: nothing will be sent
        if (C1TRECUbits.TXBP && (tickTimeSince(lastProgress) > 2 * ONE_MILI_SECOND)) {
            return MESSAGE_TIMEOUT;                                  // error-passive and stalled: no ACK
        }
#endif
        if (tickTimeSince(start) > TX_DRAIN_TIMEOUT_MS * ONE_MILI_SECOND) return OVERALL_TIMEOUT;   // busy or stuck bus
    }
    return DRAIN_OK;
}

#ifdef VLCB_SERVICE
uint8_t canHwEsdType(void) {
    return CAN_HW_PIC_CAN_2_0;
}
#endif

#ifdef VLCB_DIAG
uint8_t canHwTxInUse(void) {
    if (! C1FIFOSTA2Lbits.TFNRFNIF) {   // FIFO full
        return CAN1_FIFO2_SIZE;
    } else {
        int16_t i16;

        i16 = (int16_t)((C1FIFOUA2 - CAN1_FIFO2_BUFFERS_BASE_ADDRESS)/(8+CAN1_FIFO2_PAYLOAD_SIZE)); // write index
        i16 = (int16_t)(i16 - C1FIFOSTA2Hbits.FIFOCI); // quantity in buffer
        if (i16 < 0) i16 += CAN1_FIFO2_SIZE;
        return (uint8_t)i16;
    }
}

uint8_t canHwRxInUse(void) {
    if (C1FIFOSTA3Lbits.TFERFFIF) {   // FIFO full
        return CAN1_FIFO3_SIZE;
    } else {
        int16_t i16;

        i16 = (int16_t)((CAN1_FIFO3_BUFFERS_BASE_ADDRESS - C1FIFOUA3)/(8+CAN1_FIFO3_PAYLOAD_SIZE)); // minus read index
        i16 += C1FIFOSTA3Hbits.FIFOCI; // write index
        if (i16 < 0) i16 += CAN1_FIFO3_SIZE;
        return (uint8_t) i16;
    }
}

void canHwDiagRefresh(uint8_t index) {
    switch (index) {
        case CAN_DIAG_STATUS:
            canServiceDiagnostics[CAN_DIAG_STATUS].asUint = C1TRECU;
            break;
        case CAN_DIAG_TX_ERRORS:
            // 4c2 despite what datasheet says this error count seem to be zero'ed
            // after reading so here we add to previous count.
            canServiceDiagnostics[CAN_DIAG_TX_ERRORS].asUint += C1BDIAG0Hbits.NTERRCNT;
            break;
        case CAN_DIAG_RX_ERRORS:
            canServiceDiagnostics[CAN_DIAG_RX_ERRORS].asUint += C1BDIAG0Lbits.NRERRCNT;
            break;
        default:
            break;
    }
}
#endif

/************************************
 * Section copied from MCC generated code (as can18_can_2.c)
 *************************************/
enum CAN_OP_MODE_STATUS CAN1_OperationModeSet(const enum CAN_OP_MODES requestMode)
{
    enum CAN_OP_MODE_STATUS status = CAN_OP_MODE_REQUEST_SUCCESS;

    if ((CAN1_OperationModeGet() == CAN_CONFIGURATION_MODE)
        || (requestMode == CAN_DISABLE_MODE)
        || (requestMode == CAN_CONFIGURATION_MODE))
    {
        C1CONTbits.REQOP = (unsigned char)requestMode;

        while (C1CONUbits.OPMOD != requestMode)
        {
            CAN_HW_SYNC();
            //This condition is avoiding the system error case endless loop
            if (C1INTHbits.SERRIF == 1)
            {
                status = CAN_OP_MODE_SYS_ERROR_OCCURED;
                break;
            }
        }
    }
    else
    {
        status = CAN_OP_MODE_REQUEST_FAIL;
    }

    return status;
}

enum CAN_OP_MODES CAN1_OperationModeGet(void)
{
    return C1CONUbits.OPMOD;
}
#endif /* _CAN_H_ */
