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
#if defined(_18FXXQ83_FAMILY_)
/**
 * @file
 * @brief
 * Implementation of the VLCB CAN Service. 
 * @details
 * Uses Controller Area Network to carry VLCB messages.
 * This implementation works with the PIC18 CAN_2.0 peripheral.
 * The service definition object is called canService.
 * The transport interface is called canTransport.
 * Performs self enumeration and CANID collision detection and re-enumeration.
 * Performs loopback of events for Consumes Own Event behaviour.
 * Collects diagnostic data to aid communications fault finding.
 * 
 * The CAN_2.0 peripheral has a TXQ and a TX FIFO. Here we don't use the TXQ since
 * frames are sent in ID (CANID) order which is not what is wanted for VLCB. We do
 * use the FIFO as frames are sent in order. 
 * We actually use 3 TX FIFOs. Two high priority FIFOs for sending self enumeration 
 * frames and a low priority FIFO for sending normal VLCB data frames.
 * 
 * We set up 1 RX Filters: One filter for all Normal (non extended frames) to 
 * put these frames into a RX FIFO.
 */
#include <xc.h>
#include <string.h> // for memcpy
#include "vlcb.h"
#include "module.h"
#include "can.h"
#include "mns.h"

#include "nvm.h"
#include "ticktime.h"
#include "messageQueue.h"

#define CAN1_BUFFERS_BASE_ADDRESS           0x3BB0  // Allows for 0x450 of CAN buffers (4+1+32+32)*(16) = 69*16 = 0x450
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


// Forward declarations
static void canFactoryReset(void);
static void canPowerUp(void);
static void canPoll(void);
static Processed canProcessMessage(Message * m);
static void canIsr(void);
static uint8_t canEsdData(uint8_t id);
static void prepareSelfEnumResponse(void);

enum CAN_OP_MODE_STATUS CAN1_OperationModeSet(const enum CAN_OP_MODES requestMode);

#ifdef VLCB_DIAG
static DiagnosticVal * canGetDiagnostic(uint8_t index);
/**
 * The set of diagnostics for the CAN service
 */
static DiagnosticVal canDiagnostics[NUM_CAN_DIAGNOSTICS+1];
#endif

/**
 * The service descriptor for the CAN service. The application must include this
 * descriptor within the const Service * const services[] array and include the
 * necessary settings within module.h in order to make use of the CAN
 * service.
 */
const Service canService = {
    SERVICE_ID_CAN,     // id
    2,                  // version
    canFactoryReset,    // factoryReset
    canPowerUp,         // powerUp
    canProcessMessage,  // processMessage
    canPoll,            // poll
#ifdef VLCB_SERVICE
    canEsdData,          // get ESD data
#endif
#ifdef VLCB_DIAG
    canGetDiagnostic    // getDiagnostic
#endif
};

// forward declarations
static SendResult canSendMessage(Message * mp);
static MessageReceived canReceiveMessage(Message * m);
static void canWaitForTxQueueToDrain(void);

/**
 * The transport descriptor for the CAN service. The application must set
 * const Transport * transport to the address of the CAN transport descriptor 
 * thus: transport = &canTransport;
 * In order to make use of the CAN transport.
 */
const Transport canTransport = {
    canSendMessage,
    canReceiveMessage,
    canWaitForTxQueueToDrain
};

/**
 * The CANID. This is persisted in non-volatile memory.
 */
static uint8_t canId;


static uint8_t  canTransmitFailed;

/**
 *  Rx buffers used to store self consumed events
 */
static Message rxBuffers[CAN_NUM_RXBUFFERS];
static MessageQueue rxQueue;

/**
 * Variables for self enumeration of CANID 
 */
enum EnumerationState {
    NO_ENUMERATION,
    ENUMERATION_REQUIRED,
    ENUMERATION_IN_PROGRESS,
    ENUMERATION_IN_PROGRESS_TX_WAITING
} EnumerationState;
static TickValue  enumerationStartTime;
static enum EnumerationState enumerationState; 
static uint8_t    enumerationResults[ENUM_ARRAY_SIZE];
#define arraySetBit( array, index ) ( array[index>>3] |= ( 1<<(index & 0x07) ) )

// forward declarations
static CanidResult setNewCanId(uint8_t newCanId);
static void canInterruptHandler(void);
static void startEnumeration(Boolean txWaiting);
static void processEnumeration(void);
static void handleSelfEnumeration(uint8_t canid);
static void canFillRxFifo(void);
#ifdef VLCB_DIAG
static uint8_t getNumTxBuffersInUse(void);
static uint8_t getNumRxBuffersInUse(void);
#endif
/*
 * The VLCB opcodes define a set of priorities for each opcode.
 * Here we map these priorities to the CAN priority bits.
 */
// The CAN priorities
static const uint8_t canPri[] = {
    0b00001011, // pLOW
    0b00001010, // pNORMAL
    0b00001001, // pABOVE
    0b00001000, // pHIGH
    0b00000000  // pSUPER
};
#define pSUPER  4   // Not message priority so supply here

//
//CAN SERVICE
//
/**
 * Perform the CAN factory reset. Just set the CANID to the default of 1.
 */
static void canFactoryReset(void) {
    canId = CANID_DEFAULT;
    writeNVM(CANID_NVM_TYPE, CANID_ADDRESS, canId);
}

#pragma warning disable 759
/**
 * Do the CAN power up. Get the saved CANID, provision the CAN peripheral.
 * The CAN is configured for 125Kbs clocked from Fosc
 * 
 * The CAN bit timing is configured based on the base clock freq (Fosc) and divider.
 * So, for 125kbits/s, bit time = 8us, we need Tq = 500ns
 * To get 500nS, we set the CAN bit rate prescaler, to half the Fosc clock rate.
 * For example, 16MHz oscillator using PLL, Fosc is 64MHz, Tosc is 15.625nS, so 
 * we use prescaler of 1:63 to give Tq of 500nS  (15.625 x 63)
 * Having set Tq to 500nS, all other CAN timings are relative to Tq, so do not 
 * need changing with processor clock speed.
 * 
 * SJW is set to 1 Tq
 *  
 * Tq = 500ns, so 1 CAN bit-time equals 16Tq, Sync-segment = 1 Tq, Propagation 
 * time = 7 Tq  (this is based on bus length), Phase1 = 4 Tq, Phase2 = 4 Tq,
 * This results in giving a total bit time of (1+7+4+4) 16 Tq. 
 * 
 * The various FIFOs are configured thus:
 *   TEF - unused
 *   TXQ - size 4 used to transmit a RTR or potentially any other high priority message
 *   FIFO1 - size 1 used to transmit a high priority zero length message for RTR response.
 *   FIFO2 - Transmit FIFO size 32 low priority used to transmit regular data frames.
 *   FIFO3 - Receive FIFO size 32 used to receive regular data frames.
 *
 * Using FIFO sizes of 32 means there is no longer a need for a software
 * FIFO as used by the ECAN driver.
 * 
 * The following filter would also be configured:
 *   Filter 1. filters for normal (11 bit) data frames and store into FIFO3.
 *
 * The CAN 2.0 peripheral also has a TXBWS setting to force a delay between 
 * two consecutive transmissions. This is to prevent a single module swamping 
 * the bus. It is configured for 32 bit times i.e. a value of 5.
 * 
 * Interrupt of buffer overflow used to update diagnostics.
 * 
 */
static void canPowerUp(void) {
    int temp;
    uint8_t* txFifoObj;
    
    // initialise the RX buffers
    rxQueue.readIndex = 0;
    rxQueue.writeIndex = 0;
    rxQueue.messages = rxBuffers;
    rxQueue.size = CAN_NUM_RXBUFFERS;
    
    temp = readNVM(CANID_NVM_TYPE, CANID_ADDRESS);
    if (temp < 0) {
        // Unsure what to do
        canId = CANID_DEFAULT;
    } else {
        canId = (uint8_t)temp;
    }
#ifdef VLCB_DIAG
    // clear the diagnostic stats
    for (temp=1; temp<NUM_CAN_DIAGNOSTICS; temp++) {
        canDiagnostics[temp].asUint = 0;
    }
    canDiagnostics[CAN_DIAG_COUNT].asUint = NUM_CAN_DIAGNOSTICS;
#endif
    
    canTransmitFailed=0;
        
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
        C1CONT = 0x50;      // TXBWS=5; ABAT=0; REQOP=0
        C1NBTCFGL = 0x00;   // SJW 1;
        C1NBTCFGH = 0x03;   // TSEG2 4;
        C1NBTCFGU = 0x02;   // TSEG1 3;
        C1NBTCFGT = 0x3F;   // BRP 15;
        // Used to transmit the RTR self enum request
        C1TXQCONL = 0x10;   // TXATIE enabled; TXQEIE disabled; TXQNIE disabled;
        C1TXQCONH = 0x04;   // FRESET enabled; UINC disabled;
        C1TXQCONU = 0x6F;   // TXAT unlimited ; TXPRI 15 (high);
        C1TXQCONT = (((CAN1_TXQ_PAYLOAD_SIZE<32) ? (CAN1_TXQ_PAYLOAD_SIZE/4)-2 : 
                        (CAN1_TXQ_PAYLOAD_SIZE==32) ? 5 : 
                                                (CAN1_TXQ_PAYLOAD_SIZE/16)+3) <<5 ) | (CAN1_TXQ_SIZE-1);   // PLSIZE 8; FSIZE 4;

        // used to transmit the zero data length response to RTR
        C1FIFOCON1L = 0x80; // TXEN enabled; RTREN enabled; RXTSEN disabled; TXATIE disabled; RXOVIE disabled; TFERFFIE enabled; TFHRFHIE disabled; TFNRFNIE disabled;
        C1FIFOCON1H = 0x04; // FRESET enabled; TXREQ disabled; UINC disabled;
        C1FIFOCON1U = 0x6F; // TXAT unlimited retransmission attempts; TXPRI 15 (high);
        C1FIFOCON1T = (((CAN1_FIFO1_PAYLOAD_SIZE<32) ? (CAN1_FIFO1_PAYLOAD_SIZE/4)-2 : 
                        (CAN1_FIFO1_PAYLOAD_SIZE==32) ? 5 : 
                                                (CAN1_FIFO1_PAYLOAD_SIZE/16)+3) << 5) | (CAN1_FIFO1_SIZE-1);// PLSIZE 8; FSIZE 1;

        // Normal TX FIFO
        C1FIFOCON2L = 0x80; // TXEN enabled; RTREN disabled; RXTSEN disabled; TXATIE disabled; RXOVIE disabled; TFERFFIE disabled; TFHRFHIE disabled; TFNRFNIE disabled;
        C1FIFOCON2H = 0x04; // FRESET enabled; TXREQ disabled; UINC disabled;
        C1FIFOCON2U = 0x60; // TXAT unlimited retransmission attempts; TXPRI 0 (low);
        C1FIFOCON2T = (((CAN1_FIFO2_PAYLOAD_SIZE<32) ? (CAN1_FIFO2_PAYLOAD_SIZE/4)-2 : 
                        (CAN1_FIFO2_PAYLOAD_SIZE==32) ? 5 : 
                                                (CAN1_FIFO2_PAYLOAD_SIZE/16)+3) << 5) | (CAN1_FIFO2_SIZE-1);// PLSIZE 8; FSIZE 32;

        // Normal RX FIFO
        C1FIFOCON3L = 0x08; // TXEN disabled; RTREN disabled; RXTSEN disabled; TXATIE disabled; RXOVIE enabled; TFERFFIE disabled; TFHRFHIE disabled; TFNRFNIE disabled;
        C1FIFOCON3H = 0x04; // FRESET enabled; TXREQ disabled; UINC disabled;
        C1FIFOCON3U = 0x00; // TXAT retransmission disabled; TXPRI 0;
        C1FIFOCON3T = (((CAN1_FIFO3_PAYLOAD_SIZE<32) ? (CAN1_FIFO3_PAYLOAD_SIZE/4)-2 : 
                        (CAN1_FIFO3_PAYLOAD_SIZE==32) ? 5 : 
                                                (CAN1_FIFO3_PAYLOAD_SIZE/16)+3) << 5) | (CAN1_FIFO3_SIZE-1); // PLSIZE 8; FSIZE 32;

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
    prepareSelfEnumResponse();
    // Initialise enumeration control variables
    enumerationState = NO_ENUMERATION;
    enumerationStartTime.val = tickGet();
    
    IPR0bits.CANIP = 0;
    PIR0bits.CANIF = 0;
    C1INTUbits.TXIE = 1;          // enable main interrupt
    C1INTTbits.RXOVIE = 1;        // enable receive overrun interrupt
    C1INTTbits.IVMIE = 1;         // Enable interrupts for invalid message

    PIE0bits.CANIE = 1; // Enable the CAN master interrupt
}

/**
 * Handle the RX overrun and receive error interrupts.
 */
void __interrupt(irq(IRQ_CAN), base(IVT_BASE)) receiveOverrun(void) {
    if (C1FIFOSTA3Lbits.RXOVIF == 1) {
#ifdef VLCB_DIAG
        canDiagnostics[CAN_DIAG_RX_BUFFER_OVERRUN].asUint++;
#endif
        C1FIFOSTA3Lbits.RXOVIF = 0;
    }
    if (C1INTHbits.IVMIF == 1) {
#ifdef VLCB_DIAG
        canDiagnostics[CAN_DIAG_RX_ERRORS].asUint++;
#endif
        C1INTHbits.IVMIF = 0;
    }
}


/**
 * Put a self enum response into FIFO1. This is just a zero length message.
 */
void prepareSelfEnumResponse(void) {
    uint8_t* txFifoObj = (uint8_t*) C1FIFOUA1;
    
    while (C1FIFOSTA1Lbits.TFNRFNIF == 1) { // FIFO1 not full
        // Refill with self enum response
        txFifoObj[0] = (canId & 0x7F);      // Put ID
        txFifoObj[1] = 0;       // high priority
        txFifoObj[4] = 0;       // Standard frame, Zero data length DLC
        txFifoObj[5] = 0;       // No sequence number
        txFifoObj[6] = 0;       // No sequence number
        txFifoObj[7] = 0;       // No sequence number
        C1FIFOCON1Hbits.UINC = 1; // ready for future transmit
    }
}

/**
 * Process the CAN specific VLCB messages. The VLCB CAN specification
 * describes two opcodes ENUM and CANID but this implementation does not 
 * require these as both are handled automatically.
 * @param m the message to be processed
 * @return PROCESSED is the message is processed, NOT_PROCESSED otherwise
 */
static Processed canProcessMessage(Message * m) {
    // check NN matches us
    if (m->len < 3) return NOT_PROCESSED;
    if (m->bytes[0] != nn.bytes.hi) return NOT_PROCESSED;
    if (m->bytes[1] != nn.bytes.lo) return NOT_PROCESSED;
    
    // Handle any CAN specific OPCs
    switch (m->opc) {
        case OPC_ENUM:
            // ignore request
            return PROCESSED;
        case OPC_CANID:
            if (m->len < 4) {
#ifdef VLCB_GRSP
                sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_CANID, SERVICE_ID_MNS, CMDERR_INV_CMD);
#endif
                return PROCESSED;
            }
            // ignore request
            return PROCESSED;
        default:
            break;
    }
    return NOT_PROCESSED;
}

/**
 * The poll routine just continues any self enumeration that is in progress.
 * I originally also tried to extend the error counters from 8bit to 16bit for
 * the diagnostic counters but this is probably unnecessary and takes too much
 * CPU time.
 */
void canPoll() {
    uint8_t t8;
    
    processEnumeration();   // Continue or finish CANID enumeration if required
/*    
    // copy the counts to diagnostic data and extend to 16bits
    t8 = C1BDIAG0Hbits.NTERRCNT; // TX_ERRORS
    if (canDiagnostics[CAN_DIAG_TX_ERRORS].asBytes.lo > t8) {
        canDiagnostics[CAN_DIAG_TX_ERRORS].asBytes.hi++;
    }
    canDiagnostics[CAN_DIAG_TX_ERRORS].asBytes.lo = t8;
    
    t8 = C1BDIAG0Lbits.NRERRCNT; // RX_ERRORS
    if (canDiagnostics[CAN_DIAG_RX_ERRORS].asBytes.lo > t8) {
        canDiagnostics[CAN_DIAG_RX_ERRORS].asBytes.hi++;
    }
    canDiagnostics[CAN_DIAG_RX_ERRORS].asBytes.lo = t8;*/
}

#ifdef VLCB_SERVICE
/**
 * Return the service extended definition bytes.
 * @param id identifier for the extended service definition data
 * @return the ESD data
 */
uint8_t canEsdData(uint8_t id) {
    switch(id) {
        case 1:
            return CAN_HW_PIC_CAN_2_0;
        default:
            return 0;
    }
}
#endif
#ifdef VLCB_DIAG
/**
 * Provide the means to return the diagnostic data. Diagnostics supported:
 *   RX_ERRORS          0x00  CAN RX error counter.             Supported
 *   TX_ERRORS          0x01  CAN TX error counter.             Supported
 *   STATUS             0x02  last CAN status byte (TBA)        Supported
 *   TX_BUFFER_USAGE    0x03  Tx buffer usage count             Supported
 *   TX_BUFFER_OVERRUN  0x04  Tx buffer overrun count           Supported
 *   TX_MESSAGES        0x05  TX message count                  Supported
 *   RX_BUFFER_USAGE    0x06  RX buffer usage count             Supported
 *   RX_BUFFER_OVERRUN  0x07  RX buffer overrun count           Supported
 *   RX_MESSAGES        0x08  RX message counter                Supported
 *   ERROR_FRAMES_DET   0x09  CAN error frames detected 
 *   ERROR_FRAMES_GEN   0x0A  CAN error frames generated (both active and passive ?)
 *   LOST_ARRBITARTAION 0x0B  CAN arbitration was lost count    Supported
 *   CANID_ENUMS        0x0C  number of CANID enumerations      Supported
 *   CANID_CONFLICTS    0x0D  number of CANID conflicts         Supported
 *   CANID_CHANGES      0x0E  Number of CANID changes           Supported
 *   CANID_ENUMS_FAIL   0x0F  Number of CANID enumeration fails Supported
 * 
 * @param index the diagnostic index 1..NUM_CAN_DIAGNOSTSICS
 * @return a pointer to the diagnostic data or NULL if the data isn't available
 */
static DiagnosticVal * canGetDiagnostic(uint8_t index) {
    int16_t i16;
    
    if (index > NUM_CAN_DIAGNOSTICS) {
        return NULL;
    }
    switch (index) {
        case CAN_DIAG_STATUS:
            canDiagnostics[CAN_DIAG_STATUS].asUint = C1TRECU;
            break;
        case CAN_DIAG_TX_BUFFER_USAGE:
            canDiagnostics[CAN_DIAG_TX_BUFFER_USAGE].asUint = getNumTxBuffersInUse();
            break;
        case CAN_DIAG_RX_BUFFER_USAGE:
            canDiagnostics[CAN_DIAG_RX_BUFFER_USAGE].asUint = getNumRxBuffersInUse();
            break;
        case CAN_DIAG_TX_ERRORS:
            canDiagnostics[CAN_DIAG_TX_ERRORS].asUint = C1BDIAG0Hbits.NTERRCNT; // TX_ERRORS
            break;
        case CAN_DIAG_RX_ERRORS:
            canDiagnostics[CAN_DIAG_RX_ERRORS].asUint = C1BDIAG0Lbits.NRERRCNT; // RX_ERRORS
            break;
    }

    return &(canDiagnostics[index]);
}

/**
 * Determine the number of transmit buffers currently being used.
 * 
 * @return number of TX buffers in use
 */
static uint8_t getNumTxBuffersInUse(void) {
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

/**
 * Determine the number of receive buffers currently being used.
 * 
 * @return number of RX buffers in use
 */
static uint8_t getNumRxBuffersInUse(void) {
    if (C1FIFOSTA3Lbits.TFERFFIF) {   // FIFO full
        return CAN1_FIFO3_SIZE;
    } else {
        int16_t i16;
        
        i16 = (int16_t)((CAN1_FIFO3_BUFFERS_BASE_ADDRESS - C1FIFOUA3)/(8+CAN1_FIFO3_PAYLOAD_SIZE)); // write index
        i16 += C1FIFOSTA3Hbits.FIFOCI; // read index
        if (i16 < 0) i16 += CAN1_FIFO3_SIZE;
        return (uint8_t) i16;
    }
}
#endif

/*            TRANSPORT INTERFACE             */
/**
 * Send a message on the CAN interface. Put the message into transmit FIFO2.
 * @param m the message to be sent
 * @return SEND_OK if a message was sent, SEND_FAIL if FIFO was full
 */
static SendResult canSendMessage(Message * mp) {
    uint8_t i;
    uint8_t* txFifoObj;
#ifdef VLCB_DIAG
    uint16_t temp;
#endif
#ifdef CONSUMED_EVENTS
    Message * m;

    // If this is an event we are sending then put it onto the rx queue so
    // we can consume our own events.
    if (isEvent((uint8_t)(mp->opc))) {
        if (have(SERVICE_ID_CONSUME_OWN_EVENTS)) {
            // we can consume our own events.
            m = getNextWriteMessage(&rxQueue);
            if (m == NULL) {
#ifdef VLCB_DIAG
                canDiagnostics[CAN_DIAG_RX_BUFFER_OVERRUN].asUint++;
                updateModuleErrorStatus();
#endif
            } else {
                // copy ECAN buffer to message
                m->opc = mp->opc;
                m->len = mp->len;
                m->bytes[0] = mp->bytes[0];
                m->bytes[1] = mp->bytes[1];
                m->bytes[2] = mp->bytes[2];
                m->bytes[3] = mp->bytes[3];
                m->bytes[4] = mp->bytes[4];
                m->bytes[5] = mp->bytes[5];
                m->bytes[6] = mp->bytes[6];
            }
#ifdef VLCB_DIAG
            temp = getNumRxBuffersInUse();
            if (temp > canDiagnostics[CAN_DIAG_RX_HIGH_WATERMARK].asUint) {
                canDiagnostics[CAN_DIAG_RX_HIGH_WATERMARK].asUint = temp;
            }
#endif
        }
    }
#endif
    // Done if FIFO full
    if (!C1FIFOSTA2Lbits.TFNRFNIF) {
#ifdef VLCB_DIAG
        canDiagnostics[CAN_DIAG_TX_BUFFER_OVERRUN].asUint++;
        updateModuleErrorStatus();
#endif
        return SEND_FAILED;
    }
#ifdef VLCB_DIAG
    // Did previous TX fail arbitration?
    // This is really not the best place to detect arbitration failure but
    // I can't think of a better way.
    if (C1FIFOSTA2Lbits.TXLARB == 1) {
        canDiagnostics[CAN_DIAG_LOST_ARBITRATION].asUint++;
    }
#endif
    
    // start an enumeration on first transmit if we are still using canId=0
    if ((canId == 0) && (enumerationState == NO_ENUMERATION)) {
        enumerationState = ENUMERATION_REQUIRED;
        canId = 1;
    }
    
    // Pointer to FIFO entry
    txFifoObj = (uint8_t*) C1FIFOUA2;
    txFifoObj[0] = (uint8_t)((canPri[priorities[mp->opc]] & 1) << 7) | (canId & 0x7F);      // Put ID
    txFifoObj[1] = canPri[priorities[mp->opc]] >> 1;
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
    
#ifdef VLCB_DIAG
    canDiagnostics[CAN_DIAG_TX_MESSAGES].asUint++;
#endif
    C1FIFOCON2H |= _C1FIFOCON2H_UINC_MASK; // add to TX queue
#ifdef VLCB_DIAG
    temp = getNumTxBuffersInUse();
    if (temp > canDiagnostics[CAN_DIAG_TX_HIGH_WATERMARK].asUint) {
        canDiagnostics[CAN_DIAG_TX_HIGH_WATERMARK].asUint = temp;
    }
#endif
    if (canId == 0) {
        // Not ready to send as we don't yet have a CANID so start the self enumeration
        startEnumeration(1);
    } else {
        // ready to send as we have a CANID
        C1FIFOCON2H |= _C1FIFOCON2H_TXREQ_MASK; // transmit
    }
    return SEND_OK;
}

static void canWaitForTxQueueToDrain(void) {
    while (C1FIFOCON2H & _C1FIFOCON2H_TXREQ_MASK) {
        ;
    }
}

/** 
 * Initiate a self enumeration by sending a RTR frame using the TXQ.
 */
static void sendRTR(void) {
    uint8_t* txFifoObj = (uint8_t*) C1TXQUA;
    txFifoObj[0] = (canId & 0x7F);      // Put ID
    txFifoObj[1] = 0;       // high priority
    txFifoObj[4] = 0x20;    // Standard frame, RTR, Zero data length DLC
    C1TXQCONH |= (_C1TXQCONH_TXREQ_MASK | _C1TXQCONH_UINC_MASK); // transmit
#ifdef VLCB_DIAG
    canDiagnostics[CAN_DIAG_TX_MESSAGES].asUint++;
#endif
}

/**
 * Check to see if there are any received messages available returning the first
 * one.
 * If there are messages waiting in the receive buffer then return the oldest entry.
 * Sends self enumeration reply if a request has been received.
 * Collects the self enumeration replies if we sent a request.
 * Any received message is copied to the location pointed by m.
 * @return RECEIVED if message received NOT_RECEIVED otherwise
 */
static MessageReceived canReceiveMessage(Message * m){
    Message * mp;
    uint8_t incomingCanId;
    uint8_t* rxFifoObj;
#ifdef VLCB_DIAG
    uint16_t temp;
#endif

    // Check for any messages in the software fifo, which will be self-consumed events
    mp = pop(&rxQueue);
    if (mp != NULL) {
        memcpy(m, mp, sizeof(Message));
        return RECEIVED;      // message available
    } else { // Nothing in software FIFO, so now check for message in hardware FIFO
        if (! C1FIFOSTA3Lbits.TFNRFNIF) {
            // No messages
            return NOT_RECEIVED;
        }
        // message in hardware FIFO
#ifdef VLCB_DIAG
        temp = getNumRxBuffersInUse();
        if (temp > canDiagnostics[CAN_DIAG_RX_HIGH_WATERMARK].asUint) {
            canDiagnostics[CAN_DIAG_RX_HIGH_WATERMARK].asUint = temp;
        }
#endif
        // get message
        rxFifoObj = (uint8_t*) C1FIFOUA3;   // Pointer to FIFO entry
        incomingCanId = rxFifoObj[0] & 0x7F;
        handleSelfEnumeration(incomingCanId);

#ifdef VLCB_DIAG
        canDiagnostics[CAN_DIAG_RX_MESSAGES].asUint++;
#endif
        /* !!! Note that we access RTR and DLC from index 4 whereas Datasheet incorrectly says 5 !!!!!! */
        if (rxFifoObj[4] & 0x20) {
            //send the RTR response
            C1FIFOCON1H |= (_C1FIFOCON1H_TXREQ_MASK | _C1FIFOCON1H_UINC_MASK); // transmit
            C1FIFOCON3Hbits.UINC = 1;   // Indicate that we have got the message from FIFO
            return NOT_RECEIVED;
        }
        m->len = (rxFifoObj[4] & 0x0F);
        if (m->len == 0) {
            // message was a RTR response so no need to process further
            C1FIFOCON3Hbits.UINC = 1;   // Indicate that we have got the message from FIFO
            return NOT_RECEIVED;
        }
        m->opc = rxFifoObj[8];
        m->bytes[0] = rxFifoObj[9];
        m->bytes[1] = rxFifoObj[10];
        m->bytes[2] = rxFifoObj[11];
        m->bytes[3] = rxFifoObj[12];
        m->bytes[4] = rxFifoObj[13];
        m->bytes[5] = rxFifoObj[14];
        m->bytes[6] = rxFifoObj[15];

        C1FIFOCON3Hbits.UINC = 1;   // Indicate that we have got the message from FIFO
        return RECEIVED;   // message available
    }
}


/*
C1INTH.RXOVIE  // Receive Buffer Overflow       RX_BUFFER_OVERRUN
C1INTH.IVMIF   // Invalid Message               RX_ERRORS
C1INTH.CERRIF    // Bus Error                   ERROR_FRAMES_DET
C1INTH.SERRIF    // System error
C1FIFOSTA2L.C1TXLARB // Loss of arbitration     LOST_ARRBITARTAION
C1BDIAG0H.NTERRCNT   // TX error                TX_ERRORS
C1BDIAG0L.NRERRCNT   // RX error                RX_ERRORS
C1FIFOSTA3.RXOVIF // RX Overruns
C1FIFOSTA2.TXATIF // TX attempts
 */

/**
 * Start a Self Enumeration.
 * @param txWaiting set to true if this self enumeration was triggered by a transmit request without a valid CANID.
 */
static void startEnumeration(Boolean txWaiting) {
    uint8_t i;
    
    for (i=1; i< ENUM_ARRAY_SIZE; i++) {
        enumerationResults[i] = 0;
    }
    enumerationResults[0] = 1;  // Don't allocate canid 0

    enumerationState = txWaiting ? ENUMERATION_IN_PROGRESS_TX_WAITING : ENUMERATION_IN_PROGRESS;
    enumerationStartTime.val = tickGet();
#ifdef VLCB_DIAG
    canDiagnostics[CAN_DIAG_CANID_ENUMS].asUint++;
#endif
    sendRTR();              // Send RTR frame to initiate self enumeration
}

/**
 * Start or respond to self-enumeration process.
 * Checks received frame in case CANID matches our own then will start a self enum process.
 * If self enum is in progress then save the CANIDs from any responses.
 * 
 * @param receivedCanId a CANID received from another module.
 */
static void handleSelfEnumeration(uint8_t receivedCanId) {
    // Check incoming Canid and initiate self enumeration if it is the same as our own
    switch (enumerationState) {
        case ENUMERATION_IN_PROGRESS:
        case ENUMERATION_IN_PROGRESS_TX_WAITING:
            arraySetBit(enumerationResults, receivedCanId);
            break;
        case NO_ENUMERATION:
            if (receivedCanId == canId) {
                // If we receive a packet with our own canid, initiate enumeration as automatic conflict resolution (Thanks to Bob V for this idea)
                // we know enumerationInProgress = FALSE here
                enumerationState = ENUMERATION_REQUIRED;
#ifdef VLCB_DIAG
                canDiagnostics[CAN_DIAG_CANID_CONFLICTS].asUint++;
#endif
                enumerationStartTime.val = tickGet();  // Start hold off time for self enumeration - start after 200ms delay
            }
            break;
        default:
            break;
    }
}


/**
 * Check if enumeration pending, if so kick it off providing hold off time has expired.
 * If enumeration complete, find and set new can id.
 */
static void processEnumeration(void) {
    uint8_t i, newCanId, enumResult;

    switch (enumerationState) {
        case ENUMERATION_REQUIRED:
            // start after a 200ms delay
            if (tickTimeSince(enumerationStartTime) > ENUMERATION_HOLDOFF ) {
                /*
                 * Start a Self Enumeration
                 */
                startEnumeration(0);
            }
            break;
        case ENUMERATION_IN_PROGRESS:
        case ENUMERATION_IN_PROGRESS_TX_WAITING:
            /*
             * Continue Self Enumeration
             */
            if (tickTimeSince(enumerationStartTime) > ENUMERATION_TIMEOUT ) {
                /*
                 * Enumeration complete, find first free canid
                 */
                // Find byte in array with first free flag. Skip over 0xFF bytes
                for (i=0; (enumerationResults[i] == 0xFF) && (i < ENUM_ARRAY_SIZE); i++) {
                    ;
                } 
                if ((enumResult = enumerationResults[i]) != 0xFF) {
                    for (newCanId = i*8; (enumResult & 0x01); newCanId++) {
                        enumResult >>= 1;
                    }
                    if ((newCanId >= 1) && (newCanId <= 99)) {
                        // found a new CANID
                        canId = newCanId;
                        setNewCanId(canId);
                    } else {
#ifdef VLCB_DIAG
                        canDiagnostics[CAN_DIAG_CANID_ENUMS_FAIL].asUint++;
                        updateModuleErrorStatus();
#endif
                    }
                } else {
#ifdef VLCB_DIAG
                    canDiagnostics[CAN_DIAG_CANID_ENUMS_FAIL].asUint++;
                    updateModuleErrorStatus();
#endif
                }
                // If there are TX messages waiting then enable them now
                if (enumerationState == ENUMERATION_IN_PROGRESS_TX_WAITING) {
                    // put our new CANID into all the transmit buffers
                    for (i=0; i< CAN1_FIFO2_SIZE; i++) {
                        *((uint8_t*)(CAN1_FIFO2_BUFFERS_BASE_ADDRESS + (i* (8 + CAN1_FIFO2_PAYLOAD_SIZE)))) = canId & 0x7f;
                    }
                    // now send them all
                    C1FIFOCON2H |= _C1FIFOCON2H_TXREQ_MASK; // transmit
                }
                enumerationState = NO_ENUMERATION;
            }
            break;
        default:
            break;
    }
}  // Process enumeration
    

/**
 * Set a new can id. Update the diagnostic statistics.
 * @return CANID_OK upon success CANID_FAIL otherwise
 */
static CanidResult setNewCanId(uint8_t newCanId) {
    if ((newCanId >= 1) && (newCanId <= 99)) {
        canId = newCanId;
        // Update CANID in FIFO1 waiting message
        prepareSelfEnumResponse();
        writeNVM(CANID_NVM_TYPE, CANID_ADDRESS, newCanId );       // Update saved value
#ifdef VLCB_DIAG
        canDiagnostics[CAN_DIAG_CANID_CHANGES].asUint++;
#endif
        return CANID_OK;
    } else {
        return CANID_FAIL;
    }
}

/************************************
 * Section copied from MCC generated code
 *************************************/
/**
 * Set the mode of the CAN peripheral.
 * @param requestMode
 * @return status result of the request
 */
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

/**
 * Get the current operating mode of the CAN peripheral.
 * 
 * @return the current operating mode 
 */
enum CAN_OP_MODES CAN1_OperationModeGet(void)
{
    return C1CONUbits.OPMOD;
}
#endif