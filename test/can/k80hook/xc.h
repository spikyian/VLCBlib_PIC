/* The K80 model with TXB0CON reads routed through a hook, so a test can raise
 * an interrupt between two instructions of the driver. */
#include "../k80/xc.h"
volatile txcon_t * txb0_hook(void);
#undef TXB0CONbits
#define TXB0CONbits (*txb0_hook())
