/* One VLCB node for the CAN host tests: the real driver (legacy or shared
 * service, Q83 or K80) compiled into this unit with the register model, and
 * the few library functions it calls, faked. Built as a shared object; the
 * bus loads one copy per node, so each has its own statics and registers. */
#include <string.h>
#include "bus.h"
#include DRIVER         /* "can18_can_2.c" or "can18_ecan.c" */

/* ---- what the driver needs from the rest of the library ---------------- */
static volatile uint32_t *clock_ticks;
static uint32_t own_ticks;
uint32_t tickGet(void) { return clock_ticks ? *clock_ticks : own_ticks; }
TickValue tickNow;

static uint8_t eeprom[0x400];
static int eeprom_writes;
int16_t readNVM(NVMtype type, uint24_t index) { (void)type; return eeprom[index & 0x3FF]; }
uint8_t writeNVM(NVMtype type, uint24_t index, uint8_t value) { (void)type; eeprom[index & 0x3FF] = value; eeprom_writes++; return 0; }

Word nn = { .word = 0x0101 };
const Priority priorities[256] = { [0 ... 255] = pNORMAL };
static int module_errors, grsp_sent;
void updateModuleErrorStatus(void) { module_errors++; }
void sendMessage5(VlcbOpCodes opc, uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e; if (opc == OPC_GRSP) grsp_sent++;
}
static int coe = 1;
ServicePresent have(uint8_t id) { return (id == SERVICE_ID_CONSUME_OWN_EVENTS && coe) ? PRESENT : NOT_PRESENT; }
Boolean isEvent(uint8_t opc) { return (opc == OPC_ACON || opc == OPC_ACOF || opc == OPC_ASON || opc == OPC_ASOF) ? TRUE : FALSE; }

/* ---- the node API ------------------------------------------------------- */
#if defined(_18FXXQ83_FAMILY_)
#define SYNC() q83_sync()
#else
#define SYNC() k80_sync()
#endif

void api_set_clock(volatile uint32_t *t) { clock_ticks = t; }
void api_factory(void) { SYNC(); canService.factoryReset(); SYNC(); }
void api_powerup(void) { SYNC(); canService.powerUp(); SYNC(); }
void api_poll(void) {
    SYNC();
    if (canService.poll) canService.poll();
#if defined(_18F66K80_FAMILY_) && defined(CAN_TEST_LEGACY)
    /* legacy K80 has no poll: it runs processEnumeration() from receive */
#endif
    SYNC();
}
int api_send(uint8_t opc, uint8_t len, const uint8_t *d) {
    Message m; memset(&m, 0, sizeof m);
    m.opc = (VlcbOpCodes)opc; m.len = len;
    for (int i = 0; i < 7 && i + 1 < len; i++) m.bytes[i] = d[i];
    SYNC(); int r = canTransport.sendMessage(&m) == SEND_OK; SYNC();
    return r;
}
int api_recv(uint8_t *opc, uint8_t *len, uint8_t *d) {
    Message m; memset(&m, 0, sizeof m);
    SYNC(); int r = canTransport.receiveMessage(&m) == RECEIVED; SYNC();
    if (r) { *opc = (uint8_t)m.opc; *len = m.len; memcpy(d, m.bytes, 7); }
    return r;
}
void api_isr(void) {
    SYNC();
#if defined(_18FXXQ83_FAMILY_)
    receiveOverrun();
#else
    canService.lowIsr();
#endif
    SYNC();
}
int api_irq_pending(void) { SYNC(); return hw_irq_pending(); }
uint16_t api_diag(uint8_t index) { DiagnosticVal *v = canService.getDiagnostic(index); return v ? v->asUint : 0xFFFF; }
uint8_t api_canid(void) { return canId; }
uint8_t api_nvm_canid(void) { return eeprom[CANID_ADDRESS]; }
void api_set_nvm_canid(uint8_t id) { eeprom[CANID_ADDRESS] = id; }
int api_peek(BusFrame *bf) { SYNC(); memset(bf, 0, sizeof *bf); return hw_bus_peek(bf); }
void api_result(int won) { hw_bus_result(won); SYNC(); }
void api_rx(const BusFrame *bf) { hw_bus_rx(bf); SYNC(); }
int api_faults(void) { return model_faults; }
int api_rx_count(void) { return hw_rx_count(); }
int api_tx_count(void) { return hw_tx_count(); }
int api_drain(void) { return canTransport.waitForTxQueueToDrain() == DRAIN_OK; }
uint8_t api_esd(uint8_t id) { return canService.getESDdata(id); }
/* register snapshot after power-up, addresses made relative to the message RAM */
int api_regs(uint8_t *out, int max) {
#if defined(_18FXXQ83_FAMILY_)
    Q83Regs r; memcpy(&r, (const void *)&q83, sizeof r);
    uintptr_t b = (uintptr_t)q83_msgram;
    r.ua1 -= b; r.ua2 -= b; r.ua3 -= b; r.txqua -= b; r.fifoba -= b;
    int n = sizeof r < (size_t)max ? (int)sizeof r : max; memcpy(out, &r, n); return n;
#else
    int n = sizeof k80 < (size_t)max ? (int)sizeof k80 : max; memcpy(out, (const void *)&k80, n); return n;
#endif
}
#ifdef CAN_VIEW_SERVICE
uint8_t APP_canViewEsdData(uint8_t id) { return (uint8_t)(0x70 + id); }
DiagnosticVal * APP_canViewDiagnostic(uint8_t index) { static DiagnosticVal v; v.asUint = (uint16_t)(index * 3); return &v; }
int api_view(uint8_t index) {
    if (canViewService.serviceNo != SERVICE_ID_CAN || canViewService.version != canService.version) return -1;
    if (canViewService.getESDdata(1) != 0x71) return -2;
    return canViewService.getDiagnostic(index)->asUint;
}
#endif
