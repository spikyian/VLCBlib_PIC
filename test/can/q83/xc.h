/* Host register model for the PIC18F27Q83 CAN 2.0 peripheral - test only.
 *
 * Models the parts of DS40002265C chapter 38 the VLCB drivers rely on:
 *  - transmit FIFO / TXQ loading: write the object at CxFIFOUAy, then UINC
 *    advances the head and CxFIFOUAy (38.5.2, 38.5.3); TFNRFNIF = not full,
 *    TFERFFIF = empty for a TX FIFO (Register CxFIFOSTAy bits 0, 2)
 *  - TXREQ requests transmission of every queued object and clears when the
 *    FIFO empties (38.5.4); FIFOCI is the index of the next object to send
 *  - receive FIFO: TFNRFNIF = not empty, TFERFFIF = full; the application
 *    reads at CxFIFOUAy and UINC advances the tail (38.8.1.1); FIFOCI is the
 *    index the next frame is written to (Figure 38-24: unchanged by a read)
 *  - REQOP / OPMOD mode change (38.2.1)
 * Inherited, not from the datasheet: the drivers read RTR/IDE/DLC of a
 * received object from byte 4 (the driver's comment says the datasheet's
 * byte 5 is wrong); the model writes them there.
 * Effects of a write happen at q83_sync(): the drivers' CAN_HW_SYNC() seam
 * and the test call it.
 */
#ifndef Q83_XC_H
#define Q83_XC_H
#include <stdint.h>
#include <stddef.h>
#include "pic_host.h"

#ifndef _18FXXQ83_FAMILY_
#define _18FXXQ83_FAMILY_
#endif
#define _PIC18
#define IRQ_CAN 0
#define IVT_BASE 0

typedef union { uint8_t v; struct { unsigned UINC:1; unsigned TXREQ:1; unsigned FRESET:1; unsigned :5; }; } fifoconh_t;
typedef union { uint8_t v; struct { unsigned TFNRFNIF:1; unsigned TFHRFHIF:1; unsigned TFERFFIF:1; unsigned RXOVIF:1; unsigned TXATIF:1; unsigned TXERR:1; unsigned TXLARB:1; unsigned TXABT:1; }; } fifostal_t;
typedef union { uint8_t v; struct { unsigned FIFOCI:5; unsigned :3; }; } fifostah_t;
typedef union { uint8_t v; struct { unsigned :4; unsigned ON:1; unsigned :3; }; } c1conh_t;
typedef union { uint8_t v; struct { unsigned :5; unsigned OPMOD:3; }; } c1conu_t;
typedef union { uint8_t v; struct { unsigned REQOP:3; unsigned ABAT:1; unsigned TXBWS:4; }; } c1cont_t;
typedef union { uint8_t v; struct { unsigned :4; unsigned SERRIF:1; unsigned :2; unsigned IVMIF:1; }; } c1inth_t;
typedef union { uint8_t v; struct { unsigned :3; unsigned RXOVIE:1; unsigned :3; unsigned IVMIE:1; }; } c1intt_t;
typedef union { uint8_t v; struct { unsigned :1; unsigned TXIE:1; unsigned :6; }; } c1intu_t;
typedef union { uint8_t v; struct { unsigned :3; unsigned TXBO:1; unsigned TXBP:1; unsigned :3; }; } c1trecu_t;
typedef union { uint8_t v; struct { unsigned NTERRCNT:8; }; } bdiag0h_t;
typedef union { uint8_t v; struct { unsigned NRERRCNT:8; }; } bdiag0l_t;
typedef union { uint8_t v; struct { unsigned CANIE:1; unsigned :7; }; } pie0_t;
typedef union { uint8_t v; struct { unsigned CANIF:1; unsigned :7; }; } pir0_t;
typedef union { uint8_t v; struct { unsigned CANIP:1; unsigned :7; }; } ipr0_t;
typedef union { uint8_t v; struct { unsigned TRISB0:1; unsigned TRISB1:1; unsigned TRISB2:1; unsigned TRISB3:1; unsigned TRISB4:1; unsigned TRISB5:1; unsigned TRISB6:1; unsigned TRISB7:1; }; } trisb_t;

/* the register file */
typedef struct Q83Regs {
    fifoconh_t con1h, con2h, con3h, txqconh;
    uint8_t con1l, con1u, con1t, con2l, con2u, con2t, con3l, con3u, con3t;
    uint8_t txqconl, txqconu, txqcont;
    fifostal_t sta1l, sta2l, sta3l, txqstal;
    fifostah_t sta1h, sta2h, sta3h;
    volatile uintptr_t ua1, ua2, ua3, txqua, fifoba;
    uint8_t conl, conu_w; c1conh_t conh; c1conu_t conu; c1cont_t cont;   /* C1CONU written as a byte: OPMOD is read-only */
    uint8_t nbtl, nbth, nbtu, nbtt;
    c1inth_t inth; c1intt_t intt; c1intu_t intu;
    c1trecu_t trecu; bdiag0h_t bdiag0h; bdiag0l_t bdiag0l;
    uint8_t fltobj0l, fltobj0h, fltobj0u, fltobj0t, mask0l, mask0h, mask0u, mask0t, fltcon0l;
    pie0_t pie0; pir0_t pir0; ipr0_t ipr0; uint8_t ipr5; trisb_t trisb; uint8_t rb2pps, canrxpps;
} Q83Regs;
extern volatile Q83Regs q83;
void q83_sync(void);
extern uint8_t q83_msgram[];

#define C1FIFOCON1H      q83.con1h.v
#define C1FIFOCON1Hbits  q83.con1h
#define C1FIFOCON2H      q83.con2h.v
#define C1FIFOCON2Hbits  q83.con2h
#define C1FIFOCON3H      q83.con3h.v
#define C1FIFOCON3Hbits  q83.con3h
#define C1TXQCONH        q83.txqconh.v
#define C1FIFOCON1L q83.con1l
#define C1FIFOCON1U q83.con1u
#define C1FIFOCON1T q83.con1t
#define C1FIFOCON2L q83.con2l
#define C1FIFOCON2U q83.con2u
#define C1FIFOCON2T q83.con2t
#define C1FIFOCON3L q83.con3l
#define C1FIFOCON3U q83.con3u
#define C1FIFOCON3T q83.con3t
#define C1TXQCONL q83.txqconl
#define C1TXQCONU q83.txqconu
#define C1TXQCONT q83.txqcont
#define C1FIFOSTA1Lbits q83.sta1l
#define C1FIFOSTA2Lbits q83.sta2l
#define C1FIFOSTA3Lbits q83.sta3l
#define C1FIFOSTA2Hbits q83.sta2h
#define C1FIFOSTA3Hbits q83.sta3h
#define C1FIFOUA1 q83.ua1
#define C1FIFOUA2 q83.ua2
#define C1FIFOUA3 q83.ua3
#define C1TXQUA   q83.txqua
#define C1FIFOBA  q83.fifoba
#define C1CONL q83.conl
#define C1CONH q83.conh.v
#define C1CONHbits q83.conh
#define C1CONU q83.conu_w
#define C1CONUbits q83.conu
#define C1CONT q83.cont.v
#define C1CONTbits q83.cont
#define C1NBTCFGL q83.nbtl
#define C1NBTCFGH q83.nbth
#define C1NBTCFGU q83.nbtu
#define C1NBTCFGT q83.nbtt
#define C1INTH q83.inth.v
#define C1INTHbits q83.inth
#define C1INTTbits q83.intt
#define C1INTUbits q83.intu
#define C1TRECU q83.trecu.v
#define C1TRECUbits q83.trecu
#define C1BDIAG0Hbits q83.bdiag0h
#define C1BDIAG0Lbits q83.bdiag0l
#define C1FLTOBJ0L q83.fltobj0l
#define C1FLTOBJ0H q83.fltobj0h
#define C1FLTOBJ0U q83.fltobj0u
#define C1FLTOBJ0T q83.fltobj0t
#define C1MASK0L q83.mask0l
#define C1MASK0H q83.mask0h
#define C1MASK0U q83.mask0u
#define C1MASK0T q83.mask0t
#define C1FLTCON0L q83.fltcon0l
#define PIE0bits q83.pie0
#define PIR0bits q83.pir0
#define IPR0bits q83.ipr0
#define IPR5 q83.ipr5
#define TRISBbits q83.trisb
#define RB2PPS q83.rb2pps
#define CANRXPPS q83.canrxpps

#define _C1FIFOCON1H_UINC_MASK  0x01
#define _C1FIFOCON1H_TXREQ_MASK 0x02
#define _C1FIFOCON2H_UINC_MASK  0x01
#define _C1FIFOCON2H_TXREQ_MASK 0x02
#define _C1TXQCONH_UINC_MASK    0x01
#define _C1TXQCONH_TXREQ_MASK   0x02

/* other SFRs the library headers name */
extern volatile uint8_t TMR0L, TMR0H;
#endif
