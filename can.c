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

/**
 * @file
 * Implementation of the VLCB CAN service. 
 * @details
 * Uses Controller Area Network to carry VLCB messages.
 * This implementation works with the PIC18 ECAN.
 * The service definition object is called canService.
 * The transport interface is called canTransport.
 * Performs self enumeration and CANID collision detection and re-enumeration.
 * Performs loopback of events for Consumes Own Event behaviour.
 * Collects diagnostic data to aid communications fault finding.
 */
#include <xc.h>
#include <string.h> // for memcpy
#include "vlcb.h"
#include "module.h"
#include "can.h"
#include "mns.h"

#include "romops.h"
#include "ticktime.h"
#include "queue.h"

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

// Forward declarations
static void canFactoryReset(void);
static void canPowerUp(void);
static void canPoll(void);
static Processed canProcessMessage(Message * m);
static void canIsr(void);
static DiagnosticVal * canGetDiagnostic(uint8_t index);

/**
 * The service descriptor for the CAN service. The application must include this
 * descriptor within the const Service * const services[] array and include the
 * necessary settings within module.h in order to make use of the CAN
 * service.
 */
const Service canService = {
    SERVICE_ID_CAN,     // id
    1,                  // version
    canFactoryReset,    // factoryReset
    canPowerUp,         // powerUp
    canProcessMessage,  // processMessage
    NULL,               // poll
    canIsr,             // highIsr
    canIsr,             // lowIsr
    NULL,               // get ESD data
    canGetDiagnostic    // getDiagnostic
};

// forward declarations
static SendResult canSendMessage(Message * mp);
static MessageReceived canReceiveMessage(Message * m);

/**
 * The transport descriptor for the CAN service. The application must set
 * const Transport * transport to the address of the CAN transport descriptor 
 * thus: transport = &canTransport;
 * In order to make use of the CAN transport.
 */
const Transport canTransport = {
    canSendMessage,
    canReceiveMessage
};

/**
 * The CANID.
 */
static uint8_t canId;
/**
 * The set of diagnostics for the CAN service
 */
static DiagnosticVal canDiagnostics[NUM_CAN_DIAGNOSTICS];

static TickValue  canTransmitTimeout;
static uint8_t  canTransmitFailed;
/**
 *  Tx and Rx buffers
 */
static Message rxBuffers[CAN_NUM_RXBUFFERS];
static Queue rxQueue;
static Message txBuffers[CAN_NUM_TXBUFFERS];
static Queue txQueue;
//static Message message;

/**
 * Variables for self enumeration of CANID 
 */
static TickValue  enumerationStartTime;
static uint8_t    enumerationRequired;     // whether to send enumeration result message
static uint8_t    enumerationInProgress;
static uint8_t    enumerationResults[ENUM_ARRAY_SIZE];
#define arraySetBit( array, index ) ( array[index>>3] |= ( 1<<(index & 0x07) ) )

// forward declarations
static CanidResult setNewCanId(uint8_t newCanId);
static uint8_t * getBufferPointer(uint8_t b);
static void canInterruptHandler(void);
static void processEnumeration(void);
static MessageReceived handleSelfEnumeration(uint8_t * p);
static void canFillRxFifo(void);

/*
 * The VLCB opcodes define a set of priorities for each opcode.
 * Here we map these priorities to the CAN priority bits.
 */
// The CAN priorities
static const uint8_t canPri[] = {
    0b01110000, // pLOW
    0b01100000, // pNORMAL
    0b01010000, // pABOVE
    0b01000000, // pHIGH
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

/**
 * Do the CAN power up. Get the saved CANID, provision the ECAN peripheral 
 * of the PIC and set the buffers up.
 * TXB0 is used as the main transmit buffer
 * TXB1 is used used to request a self enumeration
 * TXB2 is used for enumeration replies
 * RX buffers are organised as a FIFO
 */
static void canPowerUp(void) {
    int temp;
        
    // initialise the RX buffers
    rxQueue.readIndex = 0;
    rxQueue.writeIndex = 0;
    rxQueue.messages = rxBuffers;
    rxQueue.size = CAN_NUM_RXBUFFERS;
    // initialise the TX buffers
    txQueue.readIndex = 0;
    txQueue.writeIndex = 0;
    txQueue.messages = txBuffers;
    txQueue.size = CAN_NUM_TXBUFFERS;
    
    // initialise the CAN peripheral
    
    temp = readNVM(CANID_NVM_TYPE, CANID_ADDRESS);
    if (temp < 0) {
        // Unsure what to do
        canId = CANID_DEFAULT;
    } else {
        canId = (uint8_t)temp;
    }
    // clear the diagnostic stats
    for (temp=0; temp<NUM_CAN_DIAGNOSTICS; temp++) {
        canDiagnostics[temp].asInt = 0;
    }
    
    canTransmitFailed=0;
    IPR5 = CAN_INTERRUPT_PRIORITY;    // CAN interrupts priority
    // Put module into Configuration mode.
    CANCON = 0b10000000;
    // Wait for config mode
    while (CANSTATbits.OPMODE2 == 0);

    ECANCON   = 0b10110000;   // ECAN mode 2 with FIFO, FIFOWM = 1 (init when four spaces left), init to first RX buffer
    BSEL0     = 0;            // Use all 8 buffers for receive FIFO, as they do not work as a FIFO for transmit
  
    /*  The CAN bit rates used for VLCB are calculated as follows:
     * Sync segment is fixed at 1 Tq
     * We are using propogation time of 7tq, phase 1 of 4Tq and phase2 of 4Tq.
     * Total bit time is Sync + prop + phase 1 + phase 2
     * or 16 * Tq in our case
     * So, for 125kbits/s, bit time = 8us, we need Tq = 500ns
     * To get 500nS, we set the CAN bit rate prescaler, in BRGCON1, to half the FOsc clock rate.
     * For example, 16MHz oscillator using PLL, Fosc is 64MHz, Tosc is 15.625nS, so we use prescaler of 1:32 to give Tq of 500nS  (15.625 x 32)
     * Having set Tq to 500nS, all other CAN timings are relative to Tq, so do not need changing with processor clock speed
     */
    // BRGCON values used are as follows
    //BRGCON1 = 0b00000011; // 4MHz resonator + PLL = 16MHz clock (or 16MHz resonator no PLL)
    //BRGCON1 = 0b00000111; // 8MHz resonator + PLL = 32MHz clock`
    //BRGCON1 = 0b00001111; // 16MHz resonator + PLL = 64MHz clock 
    
#if CAN_CLOCK_MHz==16
      BRGCON1 = 0B00000011;                           // ??? Set for 16MHz clock for the moment, will be 64MHz when PLL used, then remove from here when set in bootloader
#endif
#if CAN_CLOCK_MHz==32
      BRGCON1 = 0b00000111;                         // 16MHz resonator + PLL = 64MHz clock
#endif
#if CAN_CLOCK_MHz==64      
      BRGCON1 = 0b00001111;                         // 16MHz resonator + PLL = 64MHz clock
#endif
    
    BRGCON2 = 0b10011110; // freely programmable, sample once, phase 1 = 4xTq, prop time = 7xTq
    BRGCON3 = 0b00000011; // Wake-up enabled, wake-up filter not used, phase 2 = 4xTq
    CIOCON    = 0b00100000;    // TX drives Vdd when recessive, CAN capture to CCP1 disabled

    // Setup masks so all filter bits are ignored apart from EXIDEN
    RXM0SIDH = 0;
    RXM0SIDL = 0x08;
    RXM0EIDH = 0;
    RXM0EIDL = 0;
    RXM1SIDH = 0;
    RXM1SIDL = 0x08;
    RXM1EIDH = 0;
    RXM1EIDL = 0;

    // Set filter 0 for standard ID only to reject bootloader messages
    RXF0SIDL = 0x80;

    // Link all filters to RXB0 - maybe only necessary to link 1
    RXFBCON0 = 0;
    RXFBCON1 = 0;
    RXFBCON2 = 0;
    RXFBCON3 = 0;
    RXFBCON4 = 0;
    RXFBCON5 = 0;
    RXFBCON6 = 0;
    RXFBCON7 = 0;

    // Link all filters to mask 0
    MSEL0 = 0;
    MSEL1 = 0;
    MSEL2 = 0;
    MSEL3 = 0;

    // Configure the buffers to receive all valid std length messages
    // RXB0CON = RXB1CON = 0x20; B0CON = B1CON = B2CON = B3CON = B4CON = B5CON = 0;

    // Clear RXFUL bits
    RXB0CON = 0x20;     // IH changed from 0 so that RXM = 01. Receive only std messages
    RXB1CON = 0x20;     // IH changed from 0 so that RXM = 01. Receive only std messages
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

    // Preload TXB0 with parameters ready for sending VLCB data packets

    TXB0CON = 0;
    TXB0CONbits.TXPRI0 = 0;                           // Set buffer priority, so will be sent after any self enumeration packets
    TXB0CONbits.TXPRI1 = 0;
    TXB0DLC = 0;                                      // Not RTR, payload length will be set by transmit routine
    
    TXB0SIDH = 0;     // The CANID and priority will be set properly when actually sending a frame.
    TXB0SIDL = 0;     

    // Preload TXB1 with RTR frame to initiate self enumeration when required

    TXB1CON = 0;
    TXB1CONbits.TXPRI0 = 0;                           // Set buffer priority, so will be sent before any VLCB data packets but after any enumeration replies
    TXB1CONbits.TXPRI1 = 1;
    TXB1DLC = 0x40;                                   // RTR packet with zero payload
    TXB1SIDH = canPri[pSUPER] | ((canId & 0x78) >> 3);    // Set CAN priority and ms 4 bits of can id
    TXB1SIDL = TXB0SIDL;                              // LS 3 bits of can id and extended id to zero

    // Preload TXB2 with a zero length packet containing CANID for  use in self enumeration

    TXB2CON = 0;
    TXB2CONbits.TXPRI0 = 1;                           // Set high buffer priority, so will be sent before any VLCB packets
    TXB2CONbits.TXPRI1 = 1;
    TXB2DLC = 0;                                      // Not RTR, zero payload
    TXB2SIDH = canPri[pSUPER] | ((canId & 0x78) >> 3);    // Set CAN priority and ms 8 bits of can id
    TXB2SIDL = TXB0SIDL;                   // LS 3 bits of can id and extended id to zero

    // Initialise enumeration control variables

    enumerationRequired = enumerationInProgress = 0;
    enumerationStartTime.val = tickGet();

    // Initialisation complete, enable CAN interrupts
    canTransmitTimeout.val = enumerationStartTime.val;

    FIFOWMIE = 1;    // Enable Fifo 1 space left interrupt
    TXBnIE = 1;      // Enable the TX buffer transmission complete interrupt
    ERRIE = 1;       // Enable error interrupts
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
                sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_NVRD, SERVICE_ID_MNS, CMDERR_INV_CMD);
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
 * Handle the interrupts from the CAN peripheral. 
 */
static void canIsr(void) {
    // If RX then transfer frame from CAN peripheral to RX message buffer
    // handle enumeration frame
    // check for CANID clash and start self enumeration process
    
    // If TX then transfer next frame from TX buffer to CAN peripheral 
    canInterruptHandler();
}

/**
 * Provide the means to return the diagnostic data.
 * @param index the diagnostic index 1..NUM_CAN_DIAGNOSTSICS
 * @return a pointer to the diagnostic data or NULL if the data isn't available
 */
static DiagnosticVal * canGetDiagnostic(uint8_t index) {
    if ((index<1) || (index>NUM_CAN_DIAGNOSTICS)) {
        return NULL;
    }
    return &(canDiagnostics[index-1]);
}

static uint8_t isEvent(uint8_t opc) {
    return (((opc & EVENT_SET_MASK) == EVENT_SET_MASK) && ((~opc & EVENT_CLR_MASK)== EVENT_CLR_MASK));
}

/*            TRANSPORT INTERFACE             */
/**
 * Send a message on the CAN interface. If there is nothing waiting in the transmit
 * buffer then send the message immediately otherwise add it to the end of the buffer.
 * @param m the message to be sent
 * @return SEND_OK if a message was sent, SEND_FAIL if buffer was full
 */
static SendResult canSendMessage(Message * mp) {
#ifdef CONSUMED_EVENTS
    Message * m;
#endif
    // first check to see if there are messages waiting in the TX queue
    TXBnIE = 0;      // disable the TX buffer transmission complete interrupt
    if (quantity(&txQueue) == 0) {
        // next check that the transmitter isn't busy
        if (TXB0CONbits.TXREQ == 0) {
            // ECAN transmit buffer is free so nothing waiting and can send immediately
            TXBnIE = 1;      // re-enable TX buffer interrupt
            // write to ECAN
            if (mp->len >8) mp->len = 8;
            // TXB0 is the normal message transmit buffer
            TXB0SIDH = canPri[priorities[mp->opc]] | ((canId & 0x78) >> 3);
            TXB0SIDL = (uint8_t)((canId & 0x07) << 5);        
            TXB0D0 = mp->opc;
            TXB0D1 = mp->bytes[0];
            TXB0D2 = mp->bytes[1];
            TXB0D3 = mp->bytes[2];
            TXB0D4 = mp->bytes[3];
            TXB0D5 = mp->bytes[4];
            TXB0D6 = mp->bytes[5];
            TXB0D7 = mp->bytes[6];
            TXB0DLC = mp->len & 0x0F;  // Ensure not RTR

            TXB0CONbits.TXREQ = 1;    // Initiate transmission
            canDiagnostics[CAN_DIAG_TX_MESSAGES].asUint++;
#ifdef CONSUMED_EVENTS
                // If this is an event we are sending then put it onto the rx queue so
                // we can consume our own events.
            if (isEvent(mp->opc)) {
                if (have(SERVICE_ID_CONSUME_OWN_EVENTS)) {
                    // we can consume our own events.
                    m = getNextWriteMessage(&rxQueue);
                    if (m == NULL) {
                        canDiagnostics[CAN_DIAG_RX_BUFFER_OVERRUN].asUint++;
                        updateModuleErrorStatus();
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
                }
            }
#endif
            return SEND_OK;
        }
    }
    // Add to transmit Queue
    if (push(&txQueue, mp) == QUEUE_FAIL) {
        canDiagnostics[CAN_DIAG_TX_BUFFER_OVERRUN].asUint++;
        updateModuleErrorStatus();
        TXBnIE = 1;      // re-enable interrupt
        return SEND_FAILED;
    }
    TXBnIE = 1;      // re-enable interrupt
    return SEND_OK;
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
    uint8_t * p;
    MessageReceived messageAvailable;
    uint8_t incomingCanId;
 
    FIFOWMIE = 0;  // Disable high watermark interrupt so ISR cannot fiddle with FIFOs or enumeration map
    processEnumeration();  // Start or finish canid enumeration if required

    // Check for any messages in the software fifo, which the ISR will have filled if there has been a high watermark interrupt
    mp = pop(&rxQueue);
    if (mp != NULL) {
        memcpy(m, mp, sizeof(Message));
        FIFOWMIE = 1; // Re-enable FIFO interrupts now out of critical section
        return RECEIVED;      // message available
    } else { // Nothing in software FIFO, so now check for message in hardware FIFO
        if (COMSTATbits.NOT_FIFOEMPTY) {
            p = getBufferPointer(CANCON & 0x07);
            if (p == NULL) {
                // how to handle this error should never happen
                FIFOWMIE = 1; // Re-enable FIFO interrupts now out of critical section
                return NOT_RECEIVED;
            }
            RXBnIF = 0;
            if (handleSelfEnumeration(p) == RECEIVED) {
                // It is a message that will need to be processed so return it
                //mp = getNextWriteMessage(&rxQueue);
                //if (mp != NULL) {
                    // copy the ECAN buffer to the Message
                    m->opc = p[D0];
                    m->len = p[DLC] & 0xF;
                    if  (m->len > 8) {
                        m->len = 8; // Limit buffer size to 8 bytes (defensive coding - it should not be possible for it to ever be more than 8, but just in case
                    }
                    m->bytes[0] = p[D1];
                    m->bytes[1] = p[D2];
                    m->bytes[2] = p[D3];
                    m->bytes[3] = p[D4];
                    m->bytes[4] = p[D5];
                    m->bytes[5] = p[D6];
                    m->bytes[6] = p[D7];
                    messageAvailable = RECEIVED;
            }
            // Record and Clear any previous invalid message bit flag.
            if (IRXIF) {
                IRXIF = 0;
            }
            // Mark that this buffer is read and empty.
            p[CON] &= 0x7f;    // clear RXFUL bit
            FIFOWMIE = 1; // Re-enable FIFO interrupts now out of critical section
            return messageAvailable;   // message available
        } else {
            // hardware FIFO empty - no messages available
            FIFOWMIE = 1; // Re-enable FIFO interrupts now out of critical section
            return NOT_RECEIVED;
        }
    }
}

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

/**
 *  Called by ISR to handle tx buffer interrupt.
 * If there is another message waiting in the TX buffers then copy that to the
 * TXB0 ECAN and start the transmission.
 */
static void checkTxFifo( void ) {
    Message * mp;
#ifdef CONSUMED_EVENTS
    Message * m;
#endif

    TXBnIF = 0;                 // reset the interrupt flag
    if (!TXB0CONbits.TXREQ) {
        mp = pop(&txQueue);
        if (mp != NULL) {  // If data waiting in software fifo, and buffer ready
            // TXB0 is the normal message transmit buffer
            TXB0SIDH = canPri[priorities[mp->opc]] | ((canId & 0x78) >> 3);
            TXB0SIDL = (uint8_t)((canId & 0x07) << 5);            
            TXB0D0 = mp->opc;
            TXB0D1 = mp->bytes[0];
            TXB0D2 = mp->bytes[1];
            TXB0D3 = mp->bytes[2];
            TXB0D4 = mp->bytes[3];
            TXB0D5 = mp->bytes[4];
            TXB0D6 = mp->bytes[5];
            TXB0D7 = mp->bytes[6];
            TXB0DLC = mp->len;

            canTransmitTimeout.val = tickGet();
            canTransmitFailed = 0;
            TXB0CONbits.TXREQ = 1;    // Initiate transmission
            TXBnIE = 1;  // enable transmit buffer interrupt
            canDiagnostics[CAN_DIAG_TX_MESSAGES].asUint++;
#ifdef CONSUMED_EVENTS
                // If this is an event we are sending then put it onto the rx queue so
            if (isEvent(mp->opc)) {
                // we can consume our own events.
                m = getNextWriteMessage(&rxQueue);
                if (m == NULL) {
                    canDiagnostics[CAN_DIAG_RX_BUFFER_OVERRUN].asUint++;
                    updateModuleErrorStatus();
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
            }
#endif
        } else {
            // nothing to send
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
} // checkTxFifo

/**
 * Called by ISR regularly to check for timeout. If a buffer has been waiting too long
 * (CAN_TX_TIMEOUT) then this is counted as a transmit error and update the
 * diagnostics and move on to another packet,
 */
static void checkCANTimeout(void) {
    if (canTransmitTimeout.val != 0) {
        if (tickTimeSince(canTransmitTimeout) > CAN_TX_TIMEOUT) {    
            canTransmitFailed = 1;
            TXB0CONbits.TXREQ = 0;  // abort timed out packet
            checkTxFifo();          //  See if another packet is waiting to be sent
            canDiagnostics[CAN_DIAG_TX_ERRORS].asUint++;
            updateModuleErrorStatus();
        }
    }
}


/**
 * Process transmit error interrupt.
 * Checks for arbitration, timeouts and bus errors.
 */
static void canTxError(void) {
    if (TXB0CONbits.TXLARB) {  // lost arbitration
        canTransmitFailed = 1;
        canTransmitTimeout.val = 0;
        TXB0CONbits.TXREQ = 0;
        canDiagnostics[CAN_DIAG_LOST_ARRBITARTAION].asUint++;
        updateModuleErrorStatus();
    }
    if (TXB0CONbits.TXERR) {	// bus error
        canTransmitFailed = 1;
        canTransmitTimeout.val = 0;
        TXB0CONbits.TXREQ = 0;
        canDiagnostics[CAN_DIAG_TX_ERRORS].asUint++;
        updateModuleErrorStatus();
    }
    if (canTransmitFailed) {
        checkTxFifo();  // Check to see if more to try and send
    }
    ERRIF = 0;
}

/**
 * This routine is called to manage the CAN interrupts.
 */
static void canInterruptHandler(void) {
    if (FIFOWMIF) {      // Receive buffer high water mark, so move data into software fifo
        canFillRxFifo();
    }
    if (ERRIF) {
        canTxError();
    }
    if (TXBnIF) {
        checkTxFifo();
    }
    checkCANTimeout();
}

/**
 * Start or respond to self-enumeration process.
 * @param p pointer to the ECAN registers.
 * @return RECEIVED if it is a message still to be processed, NOT RECEIVED if it was a self enumeration packet and needs no further processing
 */
static MessageReceived handleSelfEnumeration(uint8_t * p) {
    uint8_t incomingCanId;

    canDiagnostics[CAN_DIAG_RX_MESSAGES].asUint++;
    // Check incoming Canid and initiate self enumeration if it is the same as our own
    if (enumerationInProgress) {
        arraySetBit( enumerationResults, incomingCanId);
    } else {
        incomingCanId = (p[SIDH] << 3) + (p[SIDL] >> 5) & 0x7f;
        if (!enumerationRequired && (incomingCanId == canId)) {
            // If we receive a packet with our own canid, initiate enumeration as automatic conflict resolution (Thanks to Bob V for this idea)
            // we know enumerationInProgress = FALSE here
            enumerationRequired = 1;
            canDiagnostics[CAN_DIAG_CANID_CONFLICTS].asUint++;
            enumerationStartTime.val = tickGet();  // Start hold off time for self enumeration - start after 200ms delay
        }
    }

    // Check for RTR - self enumeration request from another module
    if (p[DLC] & 0x40 ) {
        // RTR bit set
        TXB2CONbits.TXREQ = 1;                  // Send enumeration response (zero payload frame preloaded in TXB2)
        enumerationStartTime.val = tickGet();   // re-Start hold off time for self enumeration
        return NOT_RECEIVED;                               // wasn't a proper message
    }
    return (p[DLC] & 0x0F) ? RECEIVED:NOT_RECEIVED;       // Check not zero payload
}

/**
 * Called from ISR when high water mark interrupt received.
 * Clears ECAN FIFO into software FIFO.
 */
static void canFillRxFifo(void) {
    uint8_t *ptr;
    uint8_t  hiIndex;
    Message * m;

    while (COMSTATbits.NOT_FIFOEMPTY) {
        ptr = getBufferPointer(CANCON & 0x07U);
        RXBnIF = 0;
        if (RXBnOVFL) {
            RXBnOVFL = 0;
        }

        if (handleSelfEnumeration(ptr) == RECEIVED) {
            // copy message into the rx Queue
            m = getNextWriteMessage(&rxQueue);
            if (m == NULL) {
                canDiagnostics[CAN_DIAG_RX_BUFFER_OVERRUN].asUint++;
                updateModuleErrorStatus();
                // Record and Clear any previous invalid message bit flag.
                if (IRXIF) {
                    IRXIF = 0;
                }
                return;
            } else {
                // copy ECAN buffer to message
                m->opc = ptr[D0];
                m->bytes[0] = ptr[D1];
                m->bytes[1] = ptr[D2];
                m->bytes[2] = ptr[D3];
                m->bytes[3] = ptr[D4];
                m->bytes[4] = ptr[D5];
                m->bytes[5] = ptr[D6];
                m->bytes[6] = ptr[D7];
                m->len = ptr[DLC]&0xF;
            }
        }
        // Record and Clear any previous invalid message bit flag.
        if (IRXIF) {
            IRXIF = 0;
        }
        // Mark that this buffer is read and empty.
        ptr[CON] &= 0x7f;
        FIFOWMIF = 0;
    }  // While hardware FIFO not empty
} // canFillRxFifo

/**
 * Check if enumeration pending, if so kick it off providing hold off time has expired.
 * If enumeration complete, find and set new can id.
 */
static void processEnumeration(void) {
    uint8_t i, newCanId, enumResult;

    if (enumerationRequired && (tickTimeSince(enumerationStartTime) > ENUMERATION_HOLDOFF )) {
        for (i=1; i< ENUM_ARRAY_SIZE; i++) {
            enumerationResults[i] = 0;
        }
        enumerationResults[0] = 1;  // Don't allocate canid 0

        enumerationInProgress = 1;
        enumerationRequired = 0;
        enumerationStartTime.val = tickGet();
        canDiagnostics[CAN_DIAG_CANID_ENUMS].asUint++;
        TXB1CONbits.TXREQ = 1;              // Send RTR frame to initiate self enumeration
    } else {
        if (enumerationInProgress && (tickTimeSince(enumerationStartTime) > ENUMERATION_TIMEOUT )) {
            // Enumeration complete, find first free canid
        
            // Find byte in array with first free flag. Skip over 0xFF bytes
            for (i=0; (enumerationResults[i] == 0xFF) && (i < ENUM_ARRAY_SIZE); i++) {
                ;
            } 
            if ((enumResult = enumerationResults[i]) != 0xFF) {
                for (newCanId = i*8; (enumResult & 0x01); newCanId++) {
                    enumResult >>= 1;
                }
                if ((newCanId >= 1) && (newCanId <= 99)) {
                    canId = newCanId;
                    setNewCanId(canId);
                } else {
                    canDiagnostics[CAN_DIAG_CANID_ENUMS_FAIL].asUint++;
                    updateModuleErrorStatus();
                }
            } else {
                canDiagnostics[CAN_DIAG_CANID_ENUMS_FAIL].asUint++;
                updateModuleErrorStatus();
                /* if (resultRequired) {
                    doError(CMDERR_INVALID_EVENT);  // seems a strange error code but that's what the spec says...
                } */
            }
        }
        enumerationRequired = enumerationInProgress = 0;
    }
}  // Process enumeration
    

/**
 * Set a new can id. Update the diagnostic statistics.
 * @return CANID_OK upon success CANID_FAIL otherwise
 */
static CanidResult setNewCanId(uint8_t newCanId) {
    if ((newCanId >= 1) && (newCanId <= 99)) {
        canId = newCanId;
        // Update SIDH and SIDL for CANID in TXB1 and TXB2

        TXB1SIDH &= 0b11110000;                // Clear canid bits
        TXB1SIDH |= ((newCanId & 0x78) >>3);  // Set new can id for self enumeration frame transmission
        TXB1SIDL = TXB0SIDL;

        TXB2SIDH &= 0b11110000;               // Clear canid bits
        TXB2SIDH |= ((newCanId & 0x78) >>3);  // Set new can id for self enumeration frame transmission
        TXB2SIDL = TXB0SIDL;

        writeNVM(CANID_NVM_TYPE, CANID_ADDRESS, newCanId );       // Update saved value
        canDiagnostics[CAN_DIAG_CANID_CHANGES].asUint++;        
        return CANID_OK;
    } else {
        return CANID_FAIL;
    }
}
