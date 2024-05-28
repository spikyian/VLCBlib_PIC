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

#include <xc.h>
#include "module.h"
#include "vlcbdefs_enums.h"
#include "nvm.h"

/**
 * @file
 * @brief
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

// Normally all of VLCB extensions would be disabled/enabled at once but you can 
// fine tune which bits are enabled here
//
#ifdef VLCB
#define VLCB_DIAG
#define VLCB_SERVICE
#define VLCB_MODE
#define VLCB_ZERO_RESPONSES
#define VLCB_GRSP
#define VLCB_NVSETRD
#endif

// Additional Service definitions
#define SERVICE_ID_NOT_FOUND      0xFF      ///< Indicates that a service of the requested type was not found
#define SERVICE_ID_ALL       0              ///< References all services

// Additional parameter definitions
#define PAR_LOAD1   PAR_LOAD                ///< Address for the first byte of the load address paramter.
#define PAR_LOAD2   (PAR_LOAD+1)            ///< Address for the second byte of the load address paramter.
#define PAR_LOAD3   (PAR_LOAD+2)            ///< Address for the third byte of the load address paramter.
#define PAR_LOAD4   (PAR_LOAD+3)            ///< Address for the forth byte of the load address paramter.

//
/// MANUFACTURER  - Used in the parameter block. 
#define MANU_VLCB	250
/** MODULE ID for the 16 Input module   - Used in the parameter block. All VLCB modules have the same ID. */
#define MTYP_VLCB_16INP     0x01
/** MODULE ID for the 16 Output module   - Used in the parameter block. All VLCB modules have the same ID. */
#define MTYP_VLCB_16OUT     0x02


/**
 * Message priorities
 */
typedef enum Priority {
    pLOW=0,
    pNORMAL=1,
    pABOVE=2,
    pHIGH=3,
} Priority;

/**
 * Logical boolean type.
 */
typedef enum Boolean {
    FALSE,
    TRUE
} Boolean;

/**
 * Success or failure result.
 */
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
    uint8_t len;        ///< The message total length including opc.
    VlcbOpCodes opc;    ///< The opcode.
    uint8_t bytes[7];   ///< Any data bytes contained in the message.
} Message;

/**
 * A 16 bit value which may be accessed either as a uint16 or as a pair of uint8.
 */
typedef union Word {
    struct {
        uint8_t lo;     ///< The low byte.
        uint8_t hi;     ///< The high byte.
    } bytes;
    uint16_t word;      ///< The 16 bite value.
} Word;


/**
 *  Indicates whether on ON event or OFF event
 */
typedef enum {
    EVENT_UNKNOWN = 255,
    EVENT_OFF=0,
    EVENT_ON=1
} EventState;

#ifdef VLCB_DIAG
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
    uint16_t    asUint; ///< The diagnostic value as an unsigned 16bit value.
    int16_t     asInt;  ///< The diagnostic value as a signed 16bit value.
    struct {
        uint8_t lo;     ///< The low byte of the 16bit diagnostic value.
        uint8_t hi;     ///< The high byte of the 16bit diagnostic value.
    } asBytes;
} DiagnosticVal;

#endif

/**
 * Type used to indicate whether a message has been processed or not.
 */
typedef enum Processed {
    NOT_PROCESSED=0,
    PROCESSED=1
} Processed;

/** Base mode states. */
typedef enum Mode_state {
    EMODE_UNITIALISED,
    EMODE_SETUP,
    EMODE_NORMAL
} Mode_state;

// Mode flags
#define FLAG_MODE_LEARN     1            ///< flag representing Learn mode.
#define FLAG_MODE_EVENTACK  2            ///< flag representing Event Acknowledge mode.
#define FLAG_MODE_HEARTBEAT 4            ///< flag representing Heartbeat mode.

extern const Priority priorities[256];


/**
 * Helper function to check a received message.
 * 
 * @param m received message
 * @param needed number of bytes in message
 * @param service he service making the request
 * @return Process if message isn't valid, NOT_PROCESSED if ok
 */
extern Processed checkLen(Message * m, uint8_t needed, uint8_t service);
/**
 * Helper function to check for an event.
 * @param opc the opcode
 * @return TRUE for an event FALSE otherwise
 */
extern Boolean isEvent(uint8_t opc);

/**
 * Allows the application to control how quickly timed responses are sent.
 * @param delay time between timed responses
 */
extern void setTimedResponseDelay(uint8_t delay);

// Functions to send a VLCB message on any defined transport layer
// XC8 doesn't support function overloading nor varargs
/**
 * Send a message with just OPC.
 * @param opc opcode
 */
void sendMessage0(VlcbOpCodes opc);
/**
 * Send a message with OPC and 1 data byte.
 * @param opc opcode
 * @param data1 data byte
 */
void sendMessage1(VlcbOpCodes opc, uint8_t data1);
/**
 * Send a message with OPC and 2 data bytes.
 * @param opc opcode
 * @param data1 data byte 1
 * @param data2 data byte 2
 */
void sendMessage2(VlcbOpCodes opc, uint8_t data1, uint8_t data2);
/**
 * Send a message with OPC and 3 data bytes.
 * @param opc opcode
 * @param data1 data byte 1
 * @param data2 data byte 2
 * @param data3 data byte 3
 */
void sendMessage3(VlcbOpCodes opc, uint8_t data1, uint8_t data2, uint8_t data3);
/**
 * Send a message with OPC and 4 data bytes.
 * @param opc opcode
 * @param data1 data byte 1
 * @param data2 data byte 2
 * @param data3 data byte 3
 * @param data4 data byte 4
 */
void sendMessage4(VlcbOpCodes opc, uint8_t data1, uint8_t data2, uint8_t data3, uint8_t data4);
/**
 * Send a message with OPC and 5 data bytes.
 * @param opc opcode
 * @param data1 data byte 1
 * @param data2 data byte 2
 * @param data3 data byte 3
 * @param data4 data byte 4
 * @param data5 data byte 5
 */
void sendMessage5(VlcbOpCodes opc, uint8_t data1, uint8_t data2, uint8_t data3, uint8_t data4, uint8_t data5);
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
void sendMessage6(VlcbOpCodes opc, uint8_t data1, uint8_t data2, uint8_t data3, uint8_t data4, uint8_t data5, uint8_t data6);
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
void sendMessage7(VlcbOpCodes opc, uint8_t data1, uint8_t data2, uint8_t data3, uint8_t data4, uint8_t data5, uint8_t data6, uint8_t data7);
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
void sendMessage(VlcbOpCodes opc, uint8_t len, uint8_t data1, uint8_t data2, uint8_t data3, uint8_t data4, uint8_t data5, uint8_t data6, uint8_t data7);


/* SERVICE INTERFACE */
/**
 * This is the service descriptor and the main entry point to a service. The 
 * module's application code should add the service descriptors to the services
 * array for any of the services which it requires to be implemented.
 * The VLCB library code then uses the pointers in the service descriptor to
 * access the service's functionality.
 * It is the responsibility of the writer of the service to provide a populated
 * singleton for the service that can be used by the module's application code.
 */
typedef struct Service {
    uint8_t serviceNo;          ///< Identifies the type of service.
    uint8_t version;            ///< version of the service definition (not the version of code).
    void (* factoryReset)(void);///< function call for a new module.
    void (* powerUp)(void);     ///< function called upon module power up.
    Processed (* processMessage)(Message * m);    ///< process and handle any VLCB messages.
    void (* poll)(void);        ///< called regularly .
#if defined(_18F66K80_FAMILY_)
    void (* highIsr)(void);     ///< handle any service specific high priority  interrupt service routine.
    void (* lowIsr)(void);      ///< handle any service specific high priority  interrupt service routine.
#endif
#ifdef VLCB_SERVICE
    uint8_t (* getESDdata)(uint8_t id); ///< To obtain any ESD bytes for the service.
#endif
#ifdef VLCB_DIAG
    DiagnosticVal * (* getDiagnostic)(uint8_t index);   ///< pointer to function returning DiagnosticVal*.
#endif
} Service;

/**
 * The list of services supported by the module.
 */
extern const Service * const services[];

/**
 * Indicates whether a service is present.
 */
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
 * VLCB function to handle high priority interrupts. An App wanting 
 * to use interrupts should enable the interrupts in hardware and then provide 
 * a highIsr function. Care must to taken to ensure that the cause of the 
 * interrupt is cleared within the Isr and that minimum time is spent in the 
 * Isr. It is preferable to set a flag within the Isr and then perform longer
 * processing within poll().
 */
extern void APP_highIsr(void);

/**
 * VLCB function to handle low priority interrupts. An App wanting 
 * to use interrupts should enable the interrupts in hardware and then provide 
 * a lowIsr function. Care must to taken to ensure that the cause of the 
 * interrupt is cleared within the Isr and that minimum time is spent in the 
 * Isr. It is preferable to set a flag within the Isr and then perform longer
 * processing within poll().
 */
extern void APP_lowIsr(void);

/**
 * Indicates whether a message has been received and is ready to be processed.
 */
typedef enum MessageReceived {
    NOT_RECEIVED=0,
    RECEIVED=1
} MessageReceived;

/**
 * Indicates the result of attempting to send a message.
 */
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
    SendResult (* sendMessage)(Message * m);   ///< function call to send a message.
    MessageReceived (* receiveMessage)(Message * m); ///< check to see if message is available and return in the structure provided.
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

/*
 * The default value for the node number.
 */
#define NN_HI_DEFAULT  0    ///< Default node number high byte
#define NN_LO_DEFAULT  0    ///< Default node number low byte

/**
 * The default mode for a freshly installed module.
 */
#define MODE_DEFAULT    MODE_UNINITIALISED

/* Event opcs */
#define     EVENT_SET_MASK   0b10010000    ///< OPC bit mask to test for event.
#define     EVENT_CLR_MASK   0b00000110    ///< OPC bit mask to test for event.
#define     EVENT_ON_MASK    0b00000001    ///< OPC bit mask to test for ON events.
#define     EVENT_SHORT_MASK 0b00001000    ///< OPC bit mask to test for short events.

#endif
