#ifndef _VLCB_H_
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

  Ian Hogg Nov 2022
 */
#define _VLCB_H_

#include "romops.h"

/**
 * @file
 * Baseline functionality required by VLCB and entry points into the application.
 * @details
 * The application must provide a void setup(void) function which is called during module
 * initialisation. The application should perform ant initialisation it requires 
 * within this function.
 * 
 * The application must provide a void loop(void) function which is called 
 * repeatedly as quickly as possible. The module should perform a check of any
 * processing needed by the application and perform that processing before returning
 * to pass control back to VLCB.
 */

#define SERVICE_ID_NONE      0xFF
#define SERVICE_ID_ALL       0

//
// VLCB Service Types
//
#define SERVICE_ID_MNS      1   ///< The minimum node service. All modules must implement this.
#define SERVICE_ID_NV       2   ///< The NV service.
#define SERVICE_ID_CAN      3   ///< The PIC18 ECAN CAN service. Also implements the CAN transport.
#define SERVICE_ID_TEACH    4   ///< Event teaching service.
#define SERVICE_ID_PRODUCER 5   ///< Event producer service.
#define SERVICE_ID_CONSUMER 6   ///< Event comsumer service.
#define SERVICE_ID_EVENTACK 9   ///< Event acknowledge service. Useful for debugging event configuration.
#define SERVICE_ID_BOOT     10  ///< FCU/PIC bootloader service.

//
/// MANUFACTURER  - Used in the parameter block. 
#define MANU_MERG	165
/// MODULE ID    - Used in the parameter block. All VLCB modules have the same ID
#define MTYP_VLCB    0xFC

//
// Parameters
//
#define PAR_NUM 	0	///< Number of parameters
#define PAR_MANU	1	///< Manufacturer id
#define PAR_MINVER	2	///< Minor version letter
#define PAR_MTYP	3	///< Module type code
#define PAR_EVTNUM	4	///< Number of events supported
#define PAR_EVNUM	5	///< Event variables per event
#define PAR_NVNUM	6	///< Number of Node variables
#define PAR_MAJVER	7	///< Major version number
#define PAR_FLAGS	8	///< Node flags
#define PAR_CPUID	9	///< Processor type
#define PAR_BUSTYPE	10	///< Bus type
#define PAR_LOAD1	11	///< load address, 4 bytes
#define PAR_LOAD2	12
#define PAR_LOAD3	13
#define PAR_LOAD4	14
#define PAR_CPUMID	15	///< CPU manufacturer's id as read from the chip config space, 4 bytes (note - read from cpu at runtime, so not included in checksum)
#define PAR_CPUMAN	19	///< CPU manufacturer code
#define PAR_BETA	20	///< Beta revision (numeric), or 0 if release

// 
// BUS type that module is connected to
// 
#define PB_CAN	1	///< CAN interface
#define PB_ETH	2	///< Etrhernet interface
#define PB_MIWI	3	///< MIWI interface

// 
// Error codes for OPC_CMDERR
// 
#define CMDERR_INV_CMD          1	///< Invalid command
#define CMDERR_NOT_LRN          2	///< Not in learn mode
#define CMDERR_NOT_SETUP        3	///< Not in setup mode
#define CMDERR_TOO_MANY_EVENTS	4	///< Too many events
#define CMDERR_NO_EV            5	///< No EV
#define CMDERR_INV_EV_IDX       6	///< Invalid EV index
#define CMDERR_INVALID_EVENT	7	///< Invalid event
#define CMDERR_INV_EN_IDX       8	///< now reserved
#define CMDERR_INV_PARAM_IDX	9	///< Invalid param index
#define CMDERR_INV_NV_IDX       10	///< Invalid NV index
#define CMDERR_INV_EV_VALUE     11	///< Invalie EV value
#define CMDERR_INV_NV_VALUE     12	///< Invalid NV value

//
// GRSP codes
//
#define GRSP_OK                 0   ///< Success
#define GRSP_UNKNOWN_NVM_TYPE   254 ///< Unknown non valatile memory type
#define GRSP_INVALID_DIAGNOSTIC 253 ///< Invalid diagnostic
#define GRSP_INVALID_SERVICE    252 ///< Invalid service

//
// Modes
//
#define MODE_UNINITIALISED      0xFF   ///< Uninitialised mode
#define MODE_SETUP      0   ///< Setup mode
#define MODE_NORMAL     1   ///< Normal mode
#define MODE_LEARN      2   ///< Learn mode
#define MODE_EVENT_ACK  3   ///< Event acknowledgement mode
#define MODE_BOOT       4   ///< Boot mode for FCU compatible bootloader
#define MODE_BOOT2      5   ///< Boot mode for VLCB boot service
#define MODE_NOHEARTB   6   ///< No heartbeat mode

// 
// Processor manufacturer codes
// 
#define CPUM_MICROCHIP	1	// 
#define CPUM_ATMEL      2	// 
#define CPUM_ARM        3	// 

// 
// Microchip Processor type codes (identifies to FCU for bootload compatiblity)
// 
#define P18F2480	1	// 
#define P18F4480	2	// 
#define P18F2580	3	// 
#define P18F4580	4	// 
#define P18F2585	5	// 
#define P18F4585	6	// 
#define P18F2680	7	// 
#define P18F4680	8	// 
#define P18F2682	9	// 
#define P18F4682	10	// 
#define P18F2685	11	// 
#define P18F4685	12	// 
// 
#define P18F25K80	13	// 
#define P18F45K80	14	// 
#define P18F26K80	15	// 
#define P18F46K80	16	// 
#define P18F65K80	17	// 
#define P18F66K80	18	// 
#define P18F14K22	19	// 
#define P18F26K83	20	// 
#define P18F27Q84	21	// 
#define P18F47Q84	22	// 
#define P18F27Q83	23	// 
// 
#define P32MX534F064	30	// 
#define P32MX564F064	31	// 
#define P32MX564F128	32	// 
#define P32MX575F256	33	// 
#define P32MX575F512	34	// 
#define P32MX764F128	35	// 
#define P32MX775F256	36	// 
#define P32MX775F512	37	// 
#define P32MX795F512	38	// 
// 
// ARM Processor type codes (identifies to FCU for bootload compatiblity)
// 
#define ARM1176JZF_S	1	// As used in Raspberry Pi
#define ARMCortex_A7	2	// As Used in Raspberry Pi 2
#define ARMCortex_A53	3	// As used in Raspberry Pi 3


/*
 * Message priorities
 */
typedef enum Priority {
    pLOW=0,
    pNORMAL=1,
    pABOVE=2,
    pHIGH=3,
} Priority;


// 
// VLCB opcodes list
//
typedef enum Opcode {
    OPC_ACK=0x00,
    OPC_NAK=0x01,
    OPC_HLT=0x02,
    OPC_BON=0x03,
    OPC_TOF=0x04,
    OPC_TON=0x05,
    OPC_ESTOP=0x06,
    OPC_ARST=0x07,
    OPC_RTOF=0x08,
    OPC_RTON=0x09,
    OPC_RESTP=0x0A,
    OPC_RSTAT=0x0C,
    OPC_QNN=0x0D,
    OPC_RQNP=0x10,
    OPC_RQMN=0x11,
    OPC_KLOC=0x21,
    OPC_QLOC=0x22,
    OPC_DKEEP=0x23,
    OPC_DBG1=0x30,
    OPC_EXTC=0x3F,
    OPC_RLOC=0x40,
    OPC_QCON=0x41,
    OPC_SNN=0x42,
    OPC_ALOC=0x43,
    OPC_STMOD=0x44,
    OPC_PCON=0x45,
    OPC_KCON=0x46,
    OPC_DSPD=0x47,
    OPC_DFLG=0x48,
    OPC_DFNON=0x49,
    OPC_DFNOF=0x4A,
    OPC_SSTAT=0x4C,
    OPC_NNRSM=0x4F,
    OPC_RQNN=0x50,
    OPC_NNREL=0x51,
    OPC_NNACK=0x52,
    OPC_NNLRN=0x53,
    OPC_NNULN=0x54,
    OPC_NNCLR=0x55,
    OPC_NNEVN=0x56,
    OPC_NERD=0x57,
    OPC_RQEVN=0x58,
    OPC_WRACK=0x59,
    OPC_RQDAT=0x5A,
    OPC_RQDDS=0x5B,
    OPC_BOOT=0x5C,
    OPC_ENUM=0x5D,
    OPC_NNRST=0x5E,
    OPC_EXTC1=0x5F,
    OPC_DFUN=0x60,
    OPC_GLOC=0x61,
    OPC_ERR=0x63,
//    OPC_SQU=0x66,
    OPC_CMDERR=0x6F,
    OPC_EVNLF=0x70,
    OPC_NVRD=0x71,
    OPC_NENRD=0x72,
    OPC_RQNPN=0x73,
    OPC_NUMEV=0x74,
    OPC_CANID=0x75,
    OPC_MODE=0x76,
    OPC_RQSD=0x78,
    OPC_EXTC2=0x7F,
    OPC_RDCC3=0x80,
    OPC_WCVO=0x82,
    OPC_WCVB=0x83,
    OPC_QCVS=0x84,
    OPC_PCVS=0x85,
    OPC_RDGN=0x87,
    OPC_NVSETRD=0x8E,
    OPC_ACON=0x90,
    OPC_ACOF=0x91,
    OPC_AREQ=0x92,
    OPC_ARON=0x93,
    OPC_AROF=0x94,
    OPC_EVULN=0x95,
    OPC_NVSET=0x96,
    OPC_NVANS=0x97,
    OPC_ASON=0x98,
    OPC_ASOF=0x99,
    OPC_ASRQ=0x9A,
    OPC_PARAN=0x9B,
    OPC_REVAL=0x9C,
    OPC_ARSON=0x9D,
    OPC_ARSOF=0x9E,
    OPC_EXTC3=0x9F,
    OPC_RDCC4=0xA0,
    OPC_WCVS=0xA2,
    OPC_SD=0xAC,
    OPC_HEARTB=0xAB,
    OPC_GRSP=0xAF,
    OPC_ACON1=0xB0,
    OPC_ACOF1=0xB1,
    OPC_REQEV=0xB2,
    OPC_ARON1=0xB3,
    OPC_AROF1=0xB4,
    OPC_NEVAL=0xB5,
    OPC_PNN=0xB6,
    OPC_ASON1=0xB8,
    OPC_ASOF1=0xB9,
    OPC_ARSON1=0xBD,
    OPC_ARSOF1=0xBE,
    OPC_EXTC4=0xBF,
    OPC_RDCC5=0xC0,
    OPC_WCVOA=0xC1,
    OPC_CABDAT=0xC2,
    OPC_DGN=0xC7,
    OPC_FCLK=0xCF,
    OPC_ACON2=0xD0,
    OPC_ACOF2=0xD1,
    OPC_EVLRN=0xD2,
    OPC_EVANS=0xD3,
    OPC_ARON2=0xD4,
    OPC_AROF2=0xD5,
    OPC_ASON2=0xD8,
    OPC_ASOF2=0xD9,
    OPC_ARSON2=0xDD,
    OPC_ARSOF2=0xDE,
    OPC_EXTC5=0xDF,
    OPC_RDCC6=0xE0,
    OPC_PLOC=0xE1,
    OPC_NAME=0xE2,
    OPC_STAT=0xE3,
    OPC_ENACK=0xE6,
    OPC_DTXC=0xE9,
    OPC_ESD=0xE7,
    OPC_PARAMS=0xEF,
    OPC_ACON3=0xF0,
    OPC_ACOF3=0xF1,
    OPC_ENRSP=0xF2,
    OPC_ARON3=0xF3,
    OPC_AROF3=0xF4,
    OPC_EVLRNI=0xF5,
    OPC_ACDAT=0xF6,
    OPC_ARDAT=0xF7,
    OPC_ASON3=0xF8,
    OPC_ASOF3=0xF9,
    OPC_DDES=0xFA,
    OPC_DDRS=0xFB,
    OPC_ARSON3=0xFD,
    OPC_ARSOF3=0xFE
} Opcode;

typedef enum Boolean {
    FALSE,
    TRUE
} Boolean;

typedef enum Result {
    RESULT_FAIL,
    RESULT_SUCCESS
} Result;

/**
 * The Message structure contains a VLCB message. It contain the opcode
 * any data and the length of the message.
 * len includes the opc and any bytes.
 */
typedef struct Message {
    uint8_t len;        // message total length including opc
    Opcode opc;        // The opcode
    uint8_t bytes[7];   // any data bytes
} Message;

/**
 * A 16 bit value which may be accessed either as a uint16 or as a pair of uint8.
 */
typedef union Word {
    struct {
        uint8_t lo;
        uint8_t hi;
    } bytes;
    uint16_t word;
} Word;

/**
 * Diagnostic value which may be accessed either as a uint16, int16 or a pair of
 * uint8s.
 * A collection (array) of DiagnosticVals are typically saved by a service and 
 * one of the values incremented when something of interest happens. 
 * The DiagnosticVals may then be accessed using the getDiagnostic() Service 
 * method. The MNS service RDGN opcode allows the diagnostic values to be reported
 * on the VLCB bus.
 */
typedef union DiagnosticVal {
    uint16_t    asUint;
    int16_t     asInt;
    struct {
        uint8_t lo;
        uint8_t hi;
    } asBytes;
} DiagnosticVal;

typedef enum Processed {
    NOT_PROCESSED=0,
    PROCESSED=1
} Processed;

extern const Priority priorities[256];


/*
 * Helper function to check a received message.
 * 
 * @param m received message
 * @param needed number of bytes in message
 * @return Process if message isn't valid, NOT_PROCESSED if ok
 */
extern Processed checkLen(Message * m, uint8_t needed);


// Functions to send a VLCB message on any defined transport layer
// XC8 doesn't support function overloading nor varargs
/**
 * Send a message with just OPC.
 * @param opc opcode
 */
void sendMessage0(Opcode opc);
/**
 * Send a message with OPC and 1 data byte.
 * @param opc opcode
 * @param data1 data byte
 */
void sendMessage1(Opcode opc, uint8_t data1);
/**
 * Send a message with OPC and 2 data bytes.
 * @param opc opcode
 * @param data1 data byte 1
 * @param data2 data byte 2
 */
void sendMessage2(Opcode opc, uint8_t data1, uint8_t data2);
/**
 * Send a message with OPC and 3 data bytes.
 * @param opc opcode
 * @param data1 data byte 1
 * @param data2 data byte 2
 * @param data3 data byte 3
 */
void sendMessage3(Opcode opc, uint8_t data1, uint8_t data2, uint8_t data3);
/**
 * Send a message with OPC and 4 data bytes.
 * @param opc opcode
 * @param data1 data byte 1
 * @param data2 data byte 2
 * @param data3 data byte 3
 * @param data4 data byte 4
 */
void sendMessage4(Opcode opc, uint8_t data1, uint8_t data2, uint8_t data3, uint8_t data4);
/**
 * Send a message with OPC and 5 data bytes.
 * @param opc opcode
 * @param data1 data byte 1
 * @param data2 data byte 2
 * @param data3 data byte 3
 * @param data4 data byte 4
 * @param data5 data byte 5
 */
void sendMessage5(Opcode opc, uint8_t data1, uint8_t data2, uint8_t data3, uint8_t data4, uint8_t data5);
/**
 * Send a message with OPC and 6 data bytes.
 * @param opc opcode
 * @param data1 data byte 1
 * @param data2 data byte 2
 * @param data3 data byte 3
 * @param data4 data byte 4
 * @param data5 data byte 5
 * @param data6 data byte 6
 */
void sendMessage6(Opcode opc, uint8_t data1, uint8_t data2, uint8_t data3, uint8_t data4, uint8_t data5, uint8_t data6);
/**
 * Send a message with OPC and 7 data bytes.
 * @param opc opcode
 * @param data1 data byte 1
 * @param data2 data byte 2
 * @param data3 data byte 3
 * @param data4 data byte 4
 * @param data5 data byte 5
 * @param data6 data byte 6
 * @param data7 data byte 7
 */
void sendMessage7(Opcode opc, uint8_t data1, uint8_t data2, uint8_t data3, uint8_t data4, uint8_t data5, uint8_t data6, uint8_t data7);
/**
 * Send a message of variable length with OPC and up to 7 data bytes.
 * @param opc
 * @param len
 * @param data1
 * @param data2
 * @param data3
 * @param data4
 * @param data5
 * @param data6
 * @param data7
 */
void sendMessage(Opcode opc, uint8_t len, uint8_t data1, uint8_t data2, uint8_t data3, uint8_t data4, uint8_t data5, uint8_t data6, uint8_t data7);


/* SERVICE INTERFACE */
/*******************************************************************************
 * This is the service descriptor and the main entry point to a service. The 
 * module's application code should add the service descriptors to the services
 * array for any of the services which it requires to be implemented.
 * The VLCB library code then uses the pointers in the service descriptor to
 * access the service's functionality.
 * It is the responsibility of the writer of the service to provide a populated
 * singleton for the service that can be used by the module's application code.
 */
typedef struct Service {
    uint8_t serviceNo;      // Identifies the type of service
    uint8_t version;        // version of the service definition (not the version of code))
    void (* factoryReset)(void);    // function call for a new module
    void (* powerUp)(void);         // function called upon module power up
    Processed (* processMessage)(Message * m);    // process and handle any VLCB messages
    void (* poll)(void);    // called regularly 
    void (* highIsr)(void); // handle any service specific high priority  interrupt service routine
    void (* lowIsr)(void);  // handle any service specific high priority  interrupt service routine
    //void modes();
    //void statusCodes();
    uint8_t (* getESDdata)(uint8_t id);
    DiagnosticVal * (* getDiagnostic)(uint8_t index);   // pointer to function returning DiagnosticVal*
} Service;

/**
 * The list of services supported by the module.
 */
extern const Service * const services[];

typedef enum ServicePresent {
    NOT_PRESENT=0,
    PRESENT=1
}ServicePresent; 
/*
 * Helper function to obtain a service descriptor given a service type identifier.
 * @param id the service type identifier
 * @return the service descriptor of NULL if the module doesn't support the 
 * specified service type.
 */
extern const Service * findService(uint8_t id);

/*
 * Tests whether a module supports the specified service.
 * @param id the service type identifier
 * @return 1 if the service is supported, 0 otherwise
 */
extern ServicePresent have(uint8_t id);

/*
 * Returns the index into the services array of the specified service.
 * @param id the service type identifier
 * @return the array index or SERVICE_ID_NONE if the service is not present
 */
extern uint8_t findServiceIndex(uint8_t id);

/* Service function declarations */
/*
 * VLCB function to perform necessary factory reset functionality and also
 * call the factoryReset function for each service.
 */
extern void factoryReset(void);

/**
 * VLCB function to perform necessary power up functionality and also
 * call the powerUp function for each service.
 */
//extern void powerUp(void);

/*
 * VLCB function to call the processMessage function for each service
 * until one of the services has managed to handle the message.
 * Also calls back to the application to module specific handling.
 */
//extern void processMessage(Message *);

/*
 * VLCB function to perform necessary poll functionality and regularly 
 * poll each service.
 * Polling occurs as frequently as possible. It is the responsibility of the
 * service's poll function to ensure that any actions are performed at the 
 * correct rate, for example by using tickTimeSince(lastTime).
 */
//extern void poll(void);

/*
 * VLCB function to handle high priority interrupts. A service wanting 
 * to use interrupts should enable the interrupts in hardware and then provide 
 * a highIsr function. Care must to taken to ensure that the cause of the 
 * interrupt is cleared within the Isr and that minimum time is spent in the 
 * Isr. It is preferable to set a flag within the Isr and then perform longer
 * processing within poll().
 */
//extern void highIsr(void);

/*
 * VLCB function to handle low priority interrupts. A service wanting 
 * to use interrupts should enable the interrupts in hardware and then provide 
 * a lowIsr function. Care must to taken to ensure that the cause of the 
 * interrupt is cleared within the Isr and that minimum time is spent in the 
 * Isr. It is preferable to set a flag within the Isr and then perform longer
 * processing within poll().
 */
//extern void lowIsr(void);

typedef enum MessageReceived {
    NOT_RECEIVED=0,
    RECEIVED=1
} MessageReceived;

typedef enum SendResult {
    SEND_FAILED=0,
    SEND_OK
} SendResult;

/* TRANSPORT INTERFACE */
/**
 * Transport interface to provide access to a communications bus.
 * 
 */
typedef struct Transport {
    SendResult (* sendMessage)(Message * m);   // function call to send a message
    MessageReceived (* receiveMessage)(Message * m); // check to see if message is available and return in the structure provided
 //   void (* releaseMessage)(Message * m);   // App has finished with message
} Transport;
/**
 * The application must set const Transport * transport to the particular transport
 * interface to be used by the VLCBlib.
 * 
 * VLCB supports a single transport. However it does support a bridge or routing
 * type transports that can then support multiple underlying transports
 */
extern const Transport * transport;


/**
 * Reference to a function which must be provided by the application to determine
 * whether it is a suitable time to write to Flash or EEPROM. Writing can suspend
 * the CPU for up to 2ms so this function should return 0 if the application is 
 * about to start time sensitive operations.
 * The VLCB library will busy-wait whilst this function returns 0. The 
 * application must therefore use interrupts to change the conditions returned 
 * by this function. 
 * @return whether it is a valid time
 */
extern ValidTime APP_isSuitableTimeToWriteFlash(void);

/**
 * The default value for the node number.
 */
#define NN_HI_DEFAULT  0
#define NN_LO_DEFAULT  0

/**
 * The default mode for a freshly installed module.
 */
#define MODE_DEFAULT    MODE_UNINITIALISED

/* Event opcs */
#define     EVENT_SET_MASK   0b10010000
#define     EVENT_CLR_MASK   0b00000110
#define     EVENT_ON_MASK    0b00000001
#define     EVENT_SHORT_MASK 0b00001000

#endif
