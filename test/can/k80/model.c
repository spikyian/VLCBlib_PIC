/* K80 ECAN register model behaviour (see xc.h: inherited from can18_ecan.c). */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "xc.h"
#include "bus.h"

volatile K80Regs k80;
volatile uint8_t TMR0L, TMR0H;
int model_faults;
int model_uinc_full;

static int rd, wr;          /* FIFO read and write pointers */
static int full_was[8];

static int nfull(void) { int n = 0; for (int i = 0; i < 8; i++) n += (k80.rx[i][0] & 0x80) != 0; return n; }

void k80_sync(void) {
    /* mode request */
    int req = (k80.cancon >> 5) & 7;
    k80.canstat.v = (uint8_t)((k80.canstat.v & 0x1F) | (req << 5));
    /* a released buffer moves the read pointer on (only the oldest may be released) */
    for (int i = 0; i < 8; i++) {
        int f = (k80.rx[i][0] & 0x80) != 0;
        if (full_was[i] && !f && i != rd) { model_faults++; fprintf(stderr, "K80 MODEL FAULT: released buffer %d, oldest is %d\n", i, rd); }
        full_was[i] = f;
    }
    while (nfull() && !(k80.rx[rd][0] & 0x80)) rd = (rd + 1) & 7;
    if (!nfull()) rd = wr;
    k80.cancon = (uint8_t)((k80.cancon & 0xE0) | rd);       /* FP bits are the read pointer */
    k80.comstat.NOT_FIFOEMPTY = nfull() != 0;
}

static int normal(void) { return ((k80.canstat.v >> 5) & 7) == 0; }

static int pending_buf(void) {
    int best = -1, bp = -1;
    for (int b = 2; b >= 0; b--) {                 /* higher TXPRI first; equal: higher buffer */
        txcon_t *c = (txcon_t *)&k80.tx[b][0];
        if (!c->TXREQ) continue;
        int p = c->TXPRI1 * 2 + c->TXPRI0;
        if (p > bp) { bp = p; best = b; }
    }
    return best;
}

int hw_bus_peek(BusFrame *bf) {
    k80_sync();
    if (!normal()) return 0;
    int b = pending_buf();
    if (b < 0) return 0;
    uint8_t *t = (uint8_t *)k80.tx[b];
    bf->id = (uint16_t)((t[1] << 3) | (t[2] >> 5));
    bf->ext = (t[2] & 0x08) != 0;
    bf->rtr = (t[5] & 0x40) != 0;
    bf->dlc = t[5] & 0x0F;
    memcpy(bf->d, &t[6], 8);
    return 1;
}

void hw_bus_result(int won) {
    int b = pending_buf();
    if (b < 0) return;
    txcon_t *c = (txcon_t *)&k80.tx[b][0];
    if (!won) { c->TXLARB = 1; return; }
    c->TXLARB = 0;
    c->TXREQ = 0;
    if (b == 0) k80.pir5.TXBnIF = 1;
}

void hw_bus_rx(const BusFrame *bf) {
    k80_sync();
    if (!normal()) return;
    if (bf->ext) return;                            /* filter 0: standard only */
    if (nfull() == 8) { k80.comstat.RXB1OVFL = 1; return; }
    uint8_t *r = (uint8_t *)k80.rx[wr];
    memset(r, 0, 14);
    r[1] = (uint8_t)(bf->id >> 3);
    r[2] = (uint8_t)((bf->id & 7) << 5);
    r[5] = (uint8_t)((bf->rtr ? 0x40 : 0) | (bf->dlc & 0x0F));
    memcpy(&r[6], bf->d, 8);
    r[0] = 0x80;
    full_was[wr] = 1;
    if (nfull() == 1) rd = wr;
    wr = (wr + 1) & 7;
    k80.pir5.RXBnIF = 1;
    /* can18_ecan.c says FIFOWM = 1 interrupts with four buffers left; unverified,
     * so run.sh also runs everything with K80_WM_FREE=1 (one left) */
    static int wm = -1;
    if (wm < 0) wm = getenv("K80_WM_FREE") ? atoi(getenv("K80_WM_FREE")) : 4;
    if (8 - nfull() <= wm) k80.pir5.FIFOWMIF = 1;
    k80_sync();
}

int hw_irq_pending(void) {
    return (k80.pir5.FIFOWMIF && k80.pie5.FIFOWMIE) || (k80.pir5.TXBnIF && k80.pie5.TXBnIE)
        || (k80.pir5.ERRIF && k80.pie5.ERRIE);
}

int hw_rx_count(void) { return nfull(); }
int hw_tx_count(void) { return 0; }
