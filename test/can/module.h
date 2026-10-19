/* module.h for the CAN driver host tests. Options come from -D flags:
 *   CAN_SHARED_SERVICE, CAN_ADDITIONAL_CANID_CHECKS, CONSUMED_EVENTS, CAN_RX_ACCEPT */
#ifndef _MODULE_H_
#define _MODULE_H_
#define VLCB
#define CANID_ADDRESS           0x3FE
#define CANID_NVM_TYPE          EEPROM_NVM_TYPE
#define CAN_INTERRUPT_PRIORITY  0
#define CAN_CLOCK_MHz           64
#ifndef CAN_NUM_RXBUFFERS
#define CAN_NUM_RXBUFFERS       32
#endif
#define CAN_NUM_TXBUFFERS       8
#define NUM_SERVICES            2
#define NV_NUM                  1
#define PARAM_MANU              165
#define PARAM_MAJOR_VERSION     1
#define PARAM_MINOR_VERSION     'a'
#define PARAM_BUILD_VERSION     1
#define PARAM_MODULE_ID         1
#define PARAM_NUM_NV            1
#define PARAM_NUM_EVENTS        0
#define PARAM_NUM_EV_EVENT      0
#define NAME                    "TEST   "
#endif
