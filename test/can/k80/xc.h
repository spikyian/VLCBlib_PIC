/* Host register model for the PIC18F66K80-family ECAN in mode 2 - test only.
 *
 * NOT from the datasheet (the K80 datasheet is not in this project): every
 * behaviour here is the one can18_ecan.c relies on, and is therefore
 * inherited and unverified:
 *  - eight receive buffers (RXB0, RXB1, B0-B5) used as a FIFO; each is CON,
 *    SIDH, SIDL, EIDH, EIDL, DLC, D0-D7; CON bit 7 = RXFUL
 *  - CANCON bits 2-0 = index of the oldest full buffer (read pointer); clearing
 *    its RXFUL moves it on; COMSTAT.NOT_FIFOEMPTY while any buffer is full
 *  - FIFOWMIF set when four or fewer buffers are free ("FIFOWM = 1 (init when
 *    four spaces left)"), or one with K80_WM_FREE=1; a frame with all eight
 *    full is lost and RXB1OVFL set
 *  - SIDL bit 3 = EXIDE; DLC bit 6 = RTR
 *  - TXBn: CON (TXREQ, TXLARB, TXERR, TXPRI), SIDH, SIDL, EIDH, EIDL, DLC, D0-D7;
 *    TXREQ clears when sent; TXB0 completion sets TXBnIF; the highest TXPRI
 *    goes first
 *  - lost arbitration sets TXLARB and the buffer retries (no ERRIF)
 *  - CANCON bits 7-5 REQOP, CANSTAT bits 7-5 OPMODE
 * Effects of a write happen at k80_sync().
 */
#ifndef K80_XC_H
#define K80_XC_H
#include <stdint.h>
#include <stddef.h>
#include "pic_host.h"

#ifndef _18F66K80_FAMILY_
#define _18F66K80_FAMILY_
#endif
#define _PIC18

typedef union { uint8_t v; struct { unsigned TXPRI0:1; unsigned TXPRI1:1; unsigned :1; unsigned TXREQ:1; unsigned TXERR:1; unsigned TXLARB:1; unsigned TXABT:1; unsigned TXBIF:1; }; } txcon_t;
typedef union { uint8_t v; struct { unsigned :5; unsigned OPMODE0:1; unsigned OPMODE1:1; unsigned OPMODE2:1; }; } canstat_t;
typedef union { uint8_t v; struct { unsigned EWARN:1; unsigned RXWARN:1; unsigned TXWARN:1; unsigned RXBP:1; unsigned TXBP:1; unsigned TXBO:1; unsigned RXB1OVFL:1; unsigned NOT_FIFOEMPTY:1; }; } comstat_t;
typedef union { uint8_t v; struct { unsigned FIFOWMIF:1; unsigned RXBnIF:1; unsigned TXBnIF:1; unsigned :2; unsigned ERRIF:1; unsigned :1; unsigned IRXIF:1; }; } pir5_t;
typedef union { uint8_t v; struct { unsigned FIFOWMIE:1; unsigned RXBnIE:1; unsigned TXBnIE:1; unsigned :2; unsigned ERRIE:1; unsigned :1; unsigned IRXIE:1; }; } pie5_t;
typedef union { uint8_t v; struct { unsigned :2; unsigned TXB0IE:1; unsigned TXB1IE:1; unsigned TXB2IE:1; unsigned :3; }; } txbie_t;

typedef struct K80Regs {
    uint8_t rx[8][14];
    uint8_t tx[3][14];
    uint8_t cancon; canstat_t canstat; comstat_t comstat;
    uint8_t ecancon, bsel0, bie0, brgcon1, brgcon2, brgcon3, ciocon, ipr5;
    uint8_t rxm0sidh, rxm0sidl, rxm0eidh, rxm0eidl, rxf0sidl, rxfbcon0, msel0, rxfcon0;
    pir5_t pir5; pie5_t pie5; txbie_t txbie;
} K80Regs;
extern volatile K80Regs k80;
void k80_sync(void);

#define RXB0CON k80.rx[0][0]
#define RXB1CON k80.rx[1][0]
#define B0CON   k80.rx[2][0]
#define B1CON   k80.rx[3][0]
#define B2CON   k80.rx[4][0]
#define B3CON   k80.rx[5][0]
#define B4CON   k80.rx[6][0]
#define B5CON   k80.rx[7][0]
#define TXB_(n,i) k80.tx[n][i]
#define TXB0CON  k80.tx[0][0]
#define TXB0CONbits (*(volatile txcon_t *)&k80.tx[0][0])
#define TXB0SIDH k80.tx[0][1]
#define TXB0SIDL k80.tx[0][2]
#define TXB0DLC  k80.tx[0][5]
#define TXB0D0   k80.tx[0][6]
#define TXB0D1   k80.tx[0][7]
#define TXB0D2   k80.tx[0][8]
#define TXB0D3   k80.tx[0][9]
#define TXB0D4   k80.tx[0][10]
#define TXB0D5   k80.tx[0][11]
#define TXB0D6   k80.tx[0][12]
#define TXB0D7   k80.tx[0][13]
#define TXB1CON  k80.tx[1][0]
#define TXB1CONbits (*(volatile txcon_t *)&k80.tx[1][0])
#define TXB1SIDH k80.tx[1][1]
#define TXB1SIDL k80.tx[1][2]
#define TXB1DLC  k80.tx[1][5]
#define TXB2CON  k80.tx[2][0]
#define TXB2CONbits (*(volatile txcon_t *)&k80.tx[2][0])
#define TXB2SIDH k80.tx[2][1]
#define TXB2SIDL k80.tx[2][2]
#define TXB2DLC  k80.tx[2][5]
#define CANCON  k80.cancon
#define CANSTATbits k80.canstat
#define COMSTAT k80.comstat.v
#define COMSTATbits k80.comstat
#define ECANCON k80.ecancon
#define BSEL0   k80.bsel0
#define BIE0    k80.bie0
#define BRGCON1 k80.brgcon1
#define BRGCON2 k80.brgcon2
#define BRGCON3 k80.brgcon3
#define CIOCON  k80.ciocon
#define IPR5    k80.ipr5
#define RXM0SIDH k80.rxm0sidh
#define RXM0SIDL k80.rxm0sidl
#define RXM0EIDH k80.rxm0eidh
#define RXM0EIDL k80.rxm0eidl
#define RXF0SIDL k80.rxf0sidl
#define RXFBCON0 k80.rxfbcon0
#define MSEL0    k80.msel0
#define RXFCON0  k80.rxfcon0
#define PIR5bits k80.pir5
#define PIE5bits k80.pie5
#define TXBIEbits k80.txbie

extern volatile uint8_t TMR0L, TMR0H;
#endif
