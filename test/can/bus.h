/* Frame on the simulated bus, and the per-node API the bus drives. */
#ifndef BUS_H
#define BUS_H
#include <stdint.h>

typedef struct BusFrame {
    uint16_t id;        /* 11-bit identifier */
    uint8_t rtr, ext, dlc;
    uint8_t d[8];
} BusFrame;

/* register model (q83/model.c, k80/model.c) */
int  hw_bus_peek(BusFrame *bf);     /* frame this node would send next */
void hw_bus_result(int won);        /* it went (won) or lost arbitration */
void hw_bus_rx(const BusFrame *bf); /* a frame another node sent */
int  hw_irq_pending(void);
int  hw_rx_count(void);
int  hw_tx_count(void);
extern int model_faults;
extern int model_uinc_full;

/* node API (node.c), resolved per loaded copy with dlsym */
typedef struct NodeApi {
    void (*set_clock)(volatile uint32_t *ticks);
    void (*factory)(void);
    void (*powerup)(void);
    void (*poll)(void);
    int  (*send)(uint8_t opc, uint8_t len, const uint8_t *d);   /* 1 = SEND_OK */
    int  (*recv)(uint8_t *opc, uint8_t *len, uint8_t *d);       /* 1 = RECEIVED */
    void (*isr)(void);
    int  (*irq_pending)(void);
    uint16_t (*diag)(uint8_t index);
    uint8_t (*canid)(void);
    uint8_t (*nvm_canid)(void);
    void (*set_nvm_canid)(uint8_t id);
    int  (*peek)(BusFrame *bf);
    void (*result)(int won);
    void (*rx)(const BusFrame *bf);
    int  (*faults)(void);
    int  (*rx_count)(void);
    int  (*tx_count)(void);
    int  (*drain)(void);
    uint8_t (*esd)(uint8_t id);
} NodeApi;
#endif
