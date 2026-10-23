/* K80 shared path: a transmit error interrupt that lands while the main loop
 * holds the transmit queue (TXBnIE off) must not leave a queued frame waiting
 * for the next send. The hook raises the error right after canHwTxData() has
 * read TXREQ = 1 (TXB0 busy) and before it re-enables TXBnIE. */
#include "node.c"
#include <stdio.h>
static int arm, inside, reads;
static volatile txcon_t snap;
volatile txcon_t * txb0_hook(void) {
    volatile txcon_t *real = (volatile txcon_t *)&k80.tx[0][0];
    if (!arm || inside) return real;
    if (++reads == 2) {             /* canHwTxData's second TXREQ read */
        snap.v = real->v;
        inside = 1;
        TXB0CONbits.TXERR = 1;      /* bus error on the frame in TXB0 ... */
        ERRIF = 1;
        canService.lowIsr();        /* ... its interrupt aborts it */
        inside = 0; arm = 0;
        return &snap;               /* the main loop still sees the value it read */
    }
    return real;
}
int main(void) {
    BusFrame bf; uint8_t d[7] = {0};
    eeprom[CANID_ADDRESS] = 5;
    api_powerup();
    api_send(0x90, 5, d);                       /* A: straight into TXB0 */
    arm = 1;
    api_send(0x91, 5, d);                       /* B: queued; the error lands in the window */
    for (int i = 0; i < 3; i++) { own_ticks += 10; if (hw_irq_pending()) api_isr(); api_poll(); }
    int ok = api_peek(&bf) && bf.d[0] == 0x91 && quantity(&txQueue) == 0;
    printf("%-62s %s\n", "k80_split transmit error while the queue is held", ok ? "ok" : "FAILED: frame stuck in the queue");
    return !ok;
}
