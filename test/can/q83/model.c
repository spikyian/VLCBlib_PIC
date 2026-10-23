/* Q83 CAN register model behaviour (see xc.h for what is modelled and why). */
#include <string.h>
#include <stdio.h>
#include "xc.h"
#include "bus.h"

volatile Q83Regs q83;
volatile uint8_t TMR0L, TMR0H;
uint8_t q83_msgram[0x450 + 16];

#define OBJ 16
typedef struct { int size; uintptr_t base; int head, tail, count; } Fifo;
static Fifo txq, f1, f2, f3;
static int configured;
int model_faults;           /* things real hardware would not tolerate */
int model_uinc_full;        /* UINC on a full TX FIFO (the drivers do this on FIFO1 by design) */

static void fault(const char *what) { model_faults++; fprintf(stderr, "Q83 MODEL FAULT: %s\n", what); }

static void fifo_reset(Fifo *f, int size, uintptr_t base) { f->size = size; f->base = base; f->head = f->tail = f->count = 0; }

static void layout(void) {
    uintptr_t b = (uintptr_t)q83_msgram;
    if (q83.fifoba != b) fault("C1FIFOBA not the message RAM base");
    fifo_reset(&txq, (q83.txqcont & 0x1F) + 1, b);
    fifo_reset(&f1, (q83.con1t & 0x1F) + 1, txq.base + OBJ * txq.size);
    fifo_reset(&f2, (q83.con2t & 0x1F) + 1, f1.base + OBJ * f1.size);
    fifo_reset(&f3, (q83.con3t & 0x1F) + 1, f2.base + OBJ * f2.size);
    configured = 1;
}

static void flags(void) {
    q83.txqua = txq.base + OBJ * txq.head;
    q83.ua1 = f1.base + OBJ * f1.head;
    q83.ua2 = f2.base + OBJ * f2.head;
    q83.ua3 = f3.base + OBJ * f3.tail;
    q83.sta1l.TFNRFNIF = f1.count < f1.size;  q83.sta1l.TFERFFIF = f1.count == 0;
    q83.sta2l.TFNRFNIF = f2.count < f2.size;  q83.sta2l.TFERFFIF = f2.count == 0;
    q83.sta2h.FIFOCI = f2.tail;
    q83.sta3l.TFNRFNIF = f3.count > 0;        q83.sta3l.TFERFFIF = f3.count == f3.size;
    q83.sta3h.FIFOCI = f3.head;
    if (txq.count == 0) q83.txqconh.TXREQ = 0;
    if (f1.count == 0) q83.con1h.TXREQ = 0;
    if (f2.count == 0) q83.con2h.TXREQ = 0;
}

static void tx_uinc(Fifo *f, volatile fifoconh_t *c, int isf1) {
    if (!c->UINC) return;
    c->UINC = 0;
    if (f->count >= f->size) { if (isf1) model_uinc_full++; else fault("UINC on a full TX FIFO"); return; }
    f->head = (f->head + 1) % f->size;
    f->count++;
}

void q83_sync(void) {
    if (q83.cont.REQOP != q83.conu.OPMOD) {
        int was = q83.conu.OPMOD;
        q83.conu.OPMOD = q83.cont.REQOP;
        if (was == 4 && q83.conu.OPMOD != 4) layout();      /* leaving configuration mode */
    }
    if (!configured) { flags(); return; }
    if (q83.txqconh.FRESET) { q83.txqconh.FRESET = 0; txq.head = txq.tail = txq.count = 0; }
    if (q83.con1h.FRESET) { q83.con1h.FRESET = 0; f1.head = f1.tail = f1.count = 0; }
    if (q83.con2h.FRESET) { q83.con2h.FRESET = 0; f2.head = f2.tail = f2.count = 0; }
    if (q83.con3h.FRESET) { q83.con3h.FRESET = 0; f3.head = f3.tail = f3.count = 0; }
    tx_uinc(&txq, &q83.txqconh, 0);
    tx_uinc(&f1, &q83.con1h, 1);
    tx_uinc(&f2, &q83.con2h, 0);
    if (q83.con3h.UINC) {
        q83.con3h.UINC = 0;
        if (f3.count == 0) fault("UINC on an empty RX FIFO");
        else { f3.tail = (f3.tail + 1) % f3.size; f3.count--; }
    }
    flags();
}

/* ---- bus side ------------------------------------------------------------ */
static int obj_to_frame(Fifo *f, BusFrame *bf) {
    uint8_t *o = (uint8_t *)(f->base + OBJ * f->tail);
    bf->id = o[0] | ((o[1] & 7) << 8);
    bf->rtr = (o[4] & 0x20) != 0;
    bf->ext = (o[4] & 0x10) != 0;
    bf->dlc = o[4] & 0x0F;
    memcpy(bf->d, &o[8], 8);
    return 1;
}

static Fifo *pending(void) {
    q83_sync();
    if (!configured || q83.conu.OPMOD != 6) return NULL;
    if (q83.txqconh.TXREQ && txq.count) return &txq;    /* TXPRI 15 */
    if (q83.con1h.TXREQ && f1.count) return &f1;        /* TXPRI 15 */
    if (q83.con2h.TXREQ && f2.count) return &f2;        /* TXPRI 0 */
    return NULL;
}

int hw_bus_peek(BusFrame *bf) {
    Fifo *f = pending();
    return f ? obj_to_frame(f, bf) : 0;
}

void hw_bus_result(int won) {
    Fifo *f = pending();
    if (!f) return;
    if (!won) { if (f == &f2) q83.sta2l.TXLARB = 1; return; }
    if (f == &f2) q83.sta2l.TXLARB = 0;
    f->tail = (f->tail + 1) % f->size;
    f->count--;
    flags();
}

void hw_bus_rx(const BusFrame *bf) {
    q83_sync();
    if (!configured || q83.conu.OPMOD != 6) return;
    if (bf->ext) return;                                /* filter 0: standard only */
    if (f3.count == f3.size) { q83.sta3l.RXOVIF = 1; return; }
    uint8_t *o = (uint8_t *)(f3.base + OBJ * f3.head);
    memset(o, 0, OBJ);
    o[0] = bf->id & 0xFF;
    o[1] = (bf->id >> 8) & 7;
    o[4] = (uint8_t)((bf->rtr ? 0x20 : 0) | (bf->dlc & 0x0F));   /* the drivers' byte-4 layout */
    memcpy(&o[8], bf->d, 8);
    f3.head = (f3.head + 1) % f3.size;
    f3.count++;
    flags();
}

int hw_irq_pending(void) {
    return q83.sta3l.RXOVIF || q83.inth.IVMIF;
}

int hw_rx_count(void) { return f3.count; }
int hw_tx_count(void) { return f2.count; }
