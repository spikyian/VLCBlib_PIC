/**
 * @copyright Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
 */
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
*/
/**
 * @author Ian Hogg 
 * @date Dec 2022
 * 
 */ 

/**
 *
 * @file
 * Implementation of the VLCB Minimum Module Service.
 * @details
 * MNS provides functionality required by all VLCB modules.
 *
 * The service definition object is called mnsService.
 * This is a large service as it handles mode transitions, including setting of 
 * node number. The module's LEDs and push button are also supported.
 * Service discovery and Diagnostics are also processed from this service.
 * 
 * # Dependencies on other Services
 * None.
 * 
 * MNS is a mandatory service for all VLCB modules and is therefore to be
 * included by all module applications.
 * 
 * # Module.h definitions required for the MNS service
 * - #define NUM_SERVICES to be the number of services implemented by the module.
 *                      The application must put the services into the services array.
 * - #define APP_NVM_VERSION the version number of the data structures stored in NVM
 *                      this is located where NV#0 is stored therefore NV_ADDRESS
 *                      and NV_NVM_TYPE must be defined even without the NV service.
 * - #define clkMHz       Must be set to the clock speed of the module. Typically 
 *                      this would be 4 or 16.
 * - #define NN_ADDRESS   This must be set to the address in non volatile memory
 *                      at which the node number is to be stored.
 * - #define NN_NVM_TYPE  This must be set to the type of the NVM where the node
 *                      number is to be stored.
 * - #define MODE_ADDRESS This must be set to the address in non volatile memory
 *                      at which the mode variable is to be stored.
 * - #define MODE_NVM_TYPE This must be set to the type of the NVM where the mode
 *                      variable is to be stored.
 * - #define NUM_LEDS     The application must set this to either 1 or 2 to 
 *                      indicate the number of LEDs on the module for indicating
 *                      operating mode.
 * - #define APP_setPortDirections() This macro must be set to configure the 
 *                      processor's pins for output to the LEDs and input from the
 *                      push button. It should also enable digital I/O if required 
 *                      by the processor.
 * - #define APP_writeLED1(state) This macro must be defined to set LED1 (normally
 *                      yellow) to the state specified. 1 is LED on.
 * - #define APP_writeLED2(state) This macro must be defined to set LED1 (normally
 *                      green) to the state specified. 1 is LED on.
 * - #define APP_pbState() This macro must be defined to read the push button
 *                      input, returning true when the push button is held down.
 * - #define NAME         The name of the module must be defined. Must be exactly 
 *                      7 characters. Shorter names should be padded on the right 
 *                      with spaces. The name must leave off the communications 
 *                      protocol e.g. the CANMIO module would be set to "MIO    ".
 * 
 * The following parameter values are required to be defined for use by MNS:
 * - #define PARAM_MANU              See the manufacturer settings in vlcb.h
 * - #define PARAM_MAJOR_VERSION     The major version number
 * - #define PARAM_MINOR_VERSION     The minor version character. E.g. 'a'
 * - #define PARAM_BUILD_VERSION     The build version number
 * - #define PARAM_MODULE_ID         The module ID. Normally set to MTYP_VLCB
 * - #define PARAM_NUM_NV            The number of NVs. Normally set to NV_NUM
 * - #define PARAM_NUM_EVENTS        The number of events.
 * - #define PARAM_NUM_EV_EVENT      The number of EVs per event
 * 
 */
#include <xc.h>
#include "vlcb.h"
#include "module.h"
#include "mns.h"

#include "ticktime.h"
#include "romops.h"
#include "devincs.h"
#include "timedResponse.h"

#define MNS_VERSION 1

// Forward declarations
static void mnsFactoryReset(void);
static void mnsPowerUp(void);
static void mnsPoll(void);
static Processed mnsProcessMessage(Message * m);
static void mnsLowIsr(void);
static DiagnosticVal * mnsGetDiagnostic(uint8_t index);
#ifdef PRODUCED_EVENTS
#include "event_teach.h"
#include "event_producer.h"
Boolean sendProducedEvent(Happening happening, EventState onOff);
#endif

void setLEDsByMode(void);

/**
 *  The descriptor for the MNS service.
 */
const Service mnsService = {
    SERVICE_ID_MNS,         // id
    1,                      // version
    mnsFactoryReset,        // factoryReset
    mnsPowerUp,             // powerUp
    mnsProcessMessage,      // processMessage
    mnsPoll,                // poll
    NULL,                   // highIsr
    mnsLowIsr,              // lowIsr
    NULL,                   // get ESD data
    mnsGetDiagnostic        // getDiagnostic
};

// General MNS variables
/**
 * Node number.
 */
Word nn;            // node number
/**
 * Module operating mode.
 */
uint8_t mode; // operational mode
/**
 * The module's name. The NAME macro should be specified by the application 
 * code in module.h.
 */
const char name[] __at(0x848) = NAME; // module name
// mode transition variables if timeout occurs
/**
 * The previous mode so it can be restored if a timeout occurs.
 */
static uint8_t setupModePreviousMode;
/**
 * The previous node number so it can be restored if a timeout occurs.
 */
static Word previousNN;

// LED handling
/**
 * NUM_LEDS must be specified by the application in module.h.
 * Each LED has a state here to indicate if it is on/off/flashing etc.
 */
LedState    ledState[NUM_LEDS];     // the requested state
/**
 * Counters to control on/off period.
 */
static uint8_t flashCounter[NUM_LEDS];     // update every 10ms
static TickValue ledTimer;
/**
 * Module's push button handling.
 * Other UI options are not currently supported.
 */
static uint8_t pbState;
static TickValue pbTimer;
/**
 * The diagnostic values supported by the MNS service.
 */
DiagnosticVal mnsDiagnostics[NUM_MNS_DIAGNOSTICS];

/* Heartbeat controls */
static uint8_t heartbeatSequence;
static TickValue heartbeatTimer;
static volatile uint8_t sendHeartbeatEventOn;

/* Uptime controls */
static TickValue uptimeTimer;

/*
 * Forward declaration for the TimedResponse callback function for sending
 * Service Discovery responses.
 * @param type type of TimedResponse
 * @param serviceIndex the service
 * @param step the TimedResponse step
 * @return indication if all the responses have been sent.
 */
TimedResponseResult mnsTRserviceDiscoveryCallback(uint8_t type, uint8_t serviceIndex, uint8_t step);
/*
 * Forward declaration for the TimedResponse callback function for sending
 * Diagnostic responses.
 * @param type type of TimedResponse
 * @param serviceIndex the service
 * @param step the TimedResponse step
 * @return indication if all the responses have been sent.
 */
TimedResponseResult mnsTRallDiagnosticsCallback(uint8_t type, uint8_t serviceIndex, uint8_t step);

/*
 * The Service functions
 */
/**
 * Perform the MNS factory reset. Just set the node number and mode to default.
 */
static void mnsFactoryReset(void) {
    nn.bytes.hi = NN_HI_DEFAULT;
    nn.bytes.lo = NN_LO_DEFAULT;
    writeNVM(NN_NVM_TYPE, NN_ADDRESS, nn.bytes.hi);
    writeNVM(NN_NVM_TYPE, NN_ADDRESS+1, nn.bytes.lo);
    
    mode = MODE_UNINITIALISED;
    writeNVM(MODE_NVM_TYPE, MODE_ADDRESS, mode);
}

/**
 * Perform the MNS power up.
 * Loads the node number and mode from non volatile memory. Initialises the LEDs 
 * clear the Diagnostics values.
 */
static void mnsPowerUp(void) {
    int temp;
    uint8_t i;
    
    sendHeartbeatEventOn = FALSE;
    temp = readNVM(NN_NVM_TYPE, NN_ADDRESS);
    if (temp < 0) {
        nn.bytes.hi = NN_HI_DEFAULT;
        nn.bytes.lo = NN_LO_DEFAULT;
    } else {
        nn.bytes.hi = (uint8_t)temp;
        temp = readNVM(NN_NVM_TYPE, NN_ADDRESS+1);
        if (temp < 0) {
            nn.bytes.hi = NN_HI_DEFAULT;
            nn.bytes.lo = NN_LO_DEFAULT;
        } else {
            nn.bytes.lo = (uint8_t)temp;
        }
    }
    temp = readNVM(MODE_NVM_TYPE, MODE_ADDRESS);
    if (temp < 0) {
        mode = MODE_DEFAULT;
    } else {
        mode = (uint8_t)temp;
    }
    
    // Set up the LEDs
    APP_setPortDirections();
#if ((NUM_LEDS == 1) || (NUM_LEDS == 2))
    flashCounter[GREEN_LED] = 0;
#endif
#if NUM_LEDS==2
    flashCounter[YELLOW_LED] = 0;
#endif
    ledTimer.val = 0;
    setLEDsByMode();

    pbState = 0;
    // Clear the diagnostics
    for (i=0; i< NUM_MNS_DIAGNOSTICS; i++) {
        mnsDiagnostics[i].asInt = 0;
    }
    heartbeatSequence = 0;
    heartbeatTimer.val = 0;
    uptimeTimer.val = 0;
}

/**
 * Minimum Node Specification service MERGCB message processing.
 * This handles all the opcodes for the MNS service. Also handles the mode
 * state transitions and LED changes.
 * 
 * @param m the VLCB message to be processed
 * @return PROCESSED if the message was processed, NOT_PROCESSED otherwise
 */
static Processed mnsProcessMessage(Message * m) {
    uint8_t i;
    uint8_t flags;
    //const Service * s;
    uint8_t newMode;

    // Now do the MNS opcodes

    // SETUP mode messages
    if (mode == MODE_SETUP) {
        switch (m->opc) {
            case OPC_SNN:   // Set node number
                if (m->len < 3) {
                    sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_NVRD, SERVICE_ID_MNS, CMDERR_INV_CMD);
                } else {    
                    nn.bytes.hi = m->bytes[0];
                    nn.bytes.lo = m->bytes[1];
                    writeNVM(NN_NVM_TYPE, NN_ADDRESS, nn.bytes.hi);
                    writeNVM(NN_NVM_TYPE, NN_ADDRESS+1, nn.bytes.lo);
                    
                    mode = MODE_NORMAL;
                    writeNVM(MODE_NVM_TYPE, MODE_ADDRESS, mode);
                    
                    sendMessage2(OPC_NNACK, nn.bytes.hi, nn.bytes.lo);
                    mnsDiagnostics[MNS_DIAGNOSTICS_NNCHANGE].asUint++;
                    // Update the LEDs
                    setLEDsByMode();
                }
                return PROCESSED;
            case OPC_RQNP:  // request parameters
                sendMessage7(OPC_PARAMS, PARAM_MANU, PARAM_MINOR_VERSION, 
                        PARAM_MODULE_ID, PARAM_NUM_EVENTS, PARAM_NUM_EV_EVENT, 
                        PARAM_NUM_NV, PARAM_MAJOR_VERSION);
                return PROCESSED;
            case OPC_RQMN:  // Request name
                sendMessage7(OPC_NAME, name[0], name[1], name[2], name[3],  
                        name[4], name[5], name[6]);
                return PROCESSED;
            case OPC_QNN:   // Query nodes
                flags = 0;
                if (have(SERVICE_ID_CONSUMER)) {
                    flags |= 1;
                }
                if (have(SERVICE_ID_PRODUCER)) {
                    flags |= 2;
                }
                if (flags == 3) flags |= 8;     // CoE
                if (have(SERVICE_ID_BOOT)) {
                    flags |= 16;
                }
                sendMessage5(OPC_PNN, 0,0, PARAM_MANU, PARAM_MODULE_ID, flags);
                return PROCESSED;
            default:
                break;
        }
        return NOT_PROCESSED;
    }
    // No NN but in Normal mode or equivalent message processing
    switch (m->opc) {
        case OPC_QNN:   // Query node
            flags = 0;
            if (have(SERVICE_ID_CONSUMER)) {
                flags |= 1; // CONSUMER BIT
            }
            if (have(SERVICE_ID_PRODUCER)) {
                flags |= 2; // PRODUCER BIT
            }
            if (flags == 3) flags |= 8;     // CoE BIT
            flags |= 4; // NORMAL BIT
            if (have(SERVICE_ID_BOOT)) {
                flags |= 16;    // BOOTABLE BIT
            }
            if (mode == MODE_LEARN) {
                flags |= 32;    // LEARN BIT
            }
            sendMessage5(OPC_PNN, nn.bytes.hi,nn.bytes.lo, MANU_MERG, MTYP_VLCB, flags);
            return PROCESSED;
        default:
            break;
    }
    if (m->len < 3) {
        return NOT_PROCESSED;
    }
    // With NN - check it is us
    if (m->bytes[0] != nn.bytes.hi) return NOT_PROCESSED;
    if (m->bytes[1] != nn.bytes.lo) return NOT_PROCESSED;
    // NN message processing
    switch (m->opc) {
        case OPC_RQNPN: // request node parameter
            if (m->len < 4) {
                sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_RQNPN, SERVICE_ID_MNS, CMDERR_INV_CMD);
                return PROCESSED;
            }
            if (m->bytes[2] > 20) {
                sendMessage3(OPC_CMDERR, nn.bytes.hi, nn.bytes.lo, CMDERR_INV_PARAM_IDX);
                sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_RQNPN, SERVICE_ID_MNS, CMDERR_INV_PARAM_IDX);
                return PROCESSED;
            }
            switch(m->bytes[2]) {
                case PAR_NUM:       // Number of parameters
                    i=20;
                    break;
                case PAR_MANU:      // Manufacturer id
                    i=PARAM_MANU;
                    break;
                case PAR_MINVER:	// Minor version letter
                    i=PARAM_MINOR_VERSION;
                    break;
                case PAR_MTYP:	 	// Module type code
                    i=PARAM_MODULE_ID;
                    break;
                case PAR_EVTNUM:	// Number of events supported
                    i=PARAM_NUM_EVENTS;
                    break;
                case PAR_EVNUM:		// Event variables per event
                    i=PARAM_NUM_EV_EVENT;
                    break;
                case PAR_NVNUM:		// Number of Node variables
                    i=PARAM_NUM_NV;
                    break;
                case PAR_MAJVER:	// Major version number
                    i=PARAM_MAJOR_VERSION;
                    break;
                case PAR_FLAGS:		// Node flags
                    i = 0;
                    if (have(SERVICE_ID_CONSUMER)) {
                        i |= 1;
                    }
                    if (have(SERVICE_ID_PRODUCER)) {
                        i |= 2;
                    }
                    if (i == 3) i |= 8;     // CoE
                    i |= 4; // NORMAL BIT
                    if (have(SERVICE_ID_BOOT)) {
                        i |= 16;
                    }
                    if (mode == MODE_LEARN) {
                        i |= 32;
                    }
                    break;
                case PAR_CPUID:		// Processor type
                    i=CPU;
                    break;
                case PAR_BUSTYPE:	// Bus type
                    i=0;
                    if (have(SERVICE_ID_CAN)) {
                        i=PB_CAN;
                    }
                    break;
                case PAR_LOAD1:		// load address, 4 bytes
                    i=0x00;
                    break;
                case PAR_LOAD2:		// load address, 4 bytes
                    i=0x08;
                    break;
                case PAR_LOAD3:		// load address, 4 bytes
                    i=0x00;
                    break;
                case PAR_LOAD4:		// load address, 4 bytes
                    i=0x00;
                    break;
                case PAR_CPUMID:	// CPU manufacturer's id as read from the chip config space, 4 bytes (note - read from cpu at runtime, so not included in checksum)
#ifdef _PIC18
                    i=0;
#else
                    i = (*(const uint8_t*)0x3FFFFC; // Device revision byte 0
#endif
                    break;
                case PAR_CPUMID+1:
#ifdef _PIC18
                    i=0;
#else
                    i = (*(const uint8_t*)0x3FFFFD; // Device recision byte 1
#endif
                    break;
                case PAR_CPUMID+2:
                    i = *(const uint8_t*)0x3FFFFE;  // Device ID byte 0
                    break;
                case PAR_CPUMID+3:
                    i = *(const uint8_t*)0x3FFFFF;  // Device ID byte 1
                    break;
                case PAR_CPUMAN:	// CPU manufacturer code
                    i=CPUM_MICROCHIP;
                    break;
                case PAR_BETA:		// Beta revision (numeric), or 0 if release
                    i=PARAM_BUILD_VERSION;
                    break;
                default:
                    i=0;
            }
            sendMessage4(OPC_PARAN, nn.bytes.hi, nn.bytes.lo, m->bytes[2], i);
            return PROCESSED;
        case OPC_NNRSM: // reset to manufacturer defaults
            factoryReset();
            return PROCESSED;
        case OPC_RDGN:  // diagnostics
            if (m->len < 5) {
                sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_RDGN, SERVICE_ID_MNS, CMDERR_INV_CMD);
                return PROCESSED;
            }
            if (m->bytes[2] == 0) {
                // a DGN response for all of the services
                startTimedResponse(TIMED_RESPONSE_RDGN, SERVICE_ID_ALL, mnsTRallDiagnosticsCallback);
            } else {
                // bytes[2] is a serviceIndex
                if (m->bytes[2] > NUM_SERVICES) {
                    sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_RDGN, 1, GRSP_INVALID_SERVICE);
                    return PROCESSED;
                }
                if (services[m->bytes[2]-1]->getDiagnostic == NULL) {
                    // the service doesn't support diagnostics
                    sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_RDGN, 1, GRSP_INVALID_DIAGNOSTIC);
                } 
                if (m->bytes[3] == 0) {
                    // a DGN for all diagnostics for a particular service
                    startTimedResponse(TIMED_RESPONSE_RDGN, m->bytes[2], mnsTRallDiagnosticsCallback);
                    return PROCESSED;
                } else {
                    DiagnosticVal * d = services[m->bytes[2]-1]->getDiagnostic(m->bytes[3]);
                    if (d == NULL) {
                        // the requested diagnostic doesn't exist
                        sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_RDGN, 1, GRSP_INVALID_DIAGNOSTIC);
                    } else {
                        // it was a request for a single diagnostic from a single service
                        sendMessage6(OPC_DGN, nn.bytes.hi, nn.bytes.lo, m->bytes[2], m->bytes[3],d->asBytes.hi, d->asBytes.lo);
                    }
                }
            }
            return PROCESSED;
        case OPC_RQSD:  // service discovery
            if (m->len < 4) {
                sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_RQSD, SERVICE_ID_MNS, CMDERR_INV_CMD);
                return PROCESSED;
            }
            if (m->bytes[2] == 0) {
                // start with the number of services
                sendMessage5(OPC_SD, nn.bytes.hi, nn.bytes.lo, 0, 0, NUM_SERVICES);
                // now a SD response for all of the services
                startTimedResponse(TIMED_RESPONSE_RQSD, SERVICE_ID_MNS, mnsTRserviceDiscoveryCallback);
            } else if (m->bytes[2] > NUM_SERVICES) {
                sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_RQSD, SERVICE_ID_MNS, GRSP_INVALID_SERVICE);
                return PROCESSED;
            }else {
                
                if (services[m->bytes[2]-1]->getESDdata == NULL) {
                    sendMessage7(OPC_ESD, nn.bytes.hi, nn.bytes.lo, m->bytes[2], services[m->bytes[2]-1]->serviceNo, 0,0,0);
                } else {
                    sendMessage7(OPC_ESD, nn.bytes.hi, nn.bytes.lo, m->bytes[2], services[m->bytes[2]-1]->serviceNo, 
                            services[m->bytes[2]-1]->getESDdata(1),
                            services[m->bytes[2]-1]->getESDdata(2),
                            services[m->bytes[2]-1]->getESDdata(3));
                }
            }
            return PROCESSED;
        case OPC_MODE:  // set operating mode
            if (m->len < 4) {
                sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_MODE, SERVICE_ID_MNS, CMDERR_INV_CMD);
                return PROCESSED;
            }
            newMode = m->bytes[2];
            // check current mode
            switch (mode) {
                case MODE_UNINITIALISED:
                    if (newMode == MODE_SETUP) {
                        mode = MODE_SETUP;
                        setupModePreviousMode = MODE_UNINITIALISED;
                        pbTimer.val = tickGet();
                        //start the request for NN
                        sendMessage2(OPC_RQNN, nn.bytes.hi, nn.bytes.lo);
                        // Update the LEDs
                        setLEDsByMode();
                    }
                    break;
                case MODE_SETUP:
                    break;
                default:    // NORMAL modes
                    if (newMode == MODE_SETUP) {
                        // Do State transition from Normal to Setup
                        // release the NN
                        sendMessage2(OPC_RQNN, nn.bytes.hi, nn.bytes.lo);
                        previousNN.word = nn.word;  // save the old NN
                        nn.bytes.lo = nn.bytes.hi = 0;
                        //return to setup
                        mode = MODE_SETUP;
                        setupModePreviousMode = MODE_NORMAL;
                        pbTimer.val = tickGet();
                        // Update the LEDs
                        setLEDsByMode();
                    } else if (newMode != MODE_UNINITIALISED) {
                        // change between other modes
                        // No special handling - JFDI
                        mode = newMode;
                        writeNVM(MODE_NVM_TYPE, MODE_ADDRESS, mode);
                    }
                    break;
            }
            sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_MODE, SERVICE_ID_MNS, GRSP_OK);
            return PROCESSED;
/*        case OPC_SQU:   // squelch
            // TO DO     Handle Squelch - no longer required
            if (m->len < 4) {
                sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_NVRD, SERVICE_ID_MNS, CMDERR_INV_CMD);
                return PROCESSED;
            }
            if (m->bytes[2] >100) {
                sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_NVRD, SERVICE_ID_MNS, CMDERR_INV_PARAM_IDX);
                return PROCESSED;
            }
            return PROCESSED; */
        case OPC_NNRST: // reset CPU
            RESET();
            return PROCESSED;
        default:
            break;
    }
    return NOT_PROCESSED;
}

/**
 * Update the module status with an error.
 * This is safe to be called from the CAN send function.
 */
void updateModuleErrorStatus(void) {
#ifdef PRODUCED_EVENTS
    if (mnsDiagnostics[MNS_DIAGNOSTICS_STATUS].asBytes.lo == 0) {
        // indicate that Heartbeat event ON needs to be sent
        sendHeartbeatEventOn = TRUE;
    }
#endif
    if (mnsDiagnostics[MNS_DIAGNOSTICS_STATUS].asBytes.lo < 0xFF) {
        mnsDiagnostics[MNS_DIAGNOSTICS_STATUS].asBytes.lo++;
    }
}

/**
 * Called regularly, processing for LED flashing and mode state transition 
 * timeouts.
 */
static void mnsPoll(void) {
    // Heartbeat message
    if ((mode != MODE_SETUP) && (mode != MODE_UNINITIALISED) && (mode != MODE_NOHEARTB)) {
        // don't send in NOHEARTB mode - or any others
        if (tickTimeSince(heartbeatTimer) > 5*ONE_SECOND) {
            if (mode != MODE_NOHEARTB) {
                sendMessage5(OPC_HEARTB, nn.bytes.hi,nn.bytes.lo,heartbeatSequence++,mnsDiagnostics[MNS_DIAGNOSTICS_STATUS].asBytes.lo,0);
            }
            heartbeatTimer.val = tickGet();
            if (mnsDiagnostics[MNS_DIAGNOSTICS_STATUS].asBytes.lo > 0) {
                mnsDiagnostics[MNS_DIAGNOSTICS_STATUS].asBytes.lo--;
#ifdef PRODUCED_EVENTS
                if (mnsDiagnostics[MNS_DIAGNOSTICS_STATUS].asBytes.lo == 0) {
                    // Heartbeat event OFF
                    sendProducedEvent(HEARTBEAT_HAPPENING, EVENT_OFF);
                }
#endif
            }
        }
    }
    // Heartbeat event On
    if (sendHeartbeatEventOn) {
        // Heartbeat event OFF
        sendProducedEvent(HEARTBEAT_HAPPENING, EVENT_ON);
        sendHeartbeatEventOn = FALSE;
    }
    
    // Module uptime
    if (tickTimeSince(uptimeTimer) > ONE_SECOND) {
        uptimeTimer.val = tickGet();
        mnsDiagnostics[MNS_DIAGNOSTICS_UPTIMEL].asUint++;
        if (mnsDiagnostics[MNS_DIAGNOSTICS_UPTIMEL].asUint == 0) {
            mnsDiagnostics[MNS_DIAGNOSTICS_UPTIMEH].asUint++;
        }
    }
    
    // update the actual LEDs based upon their state
    if (tickTimeSince(ledTimer) > TEN_MILI_SECOND) {
#if ((NUM_LEDS == 1) || (NUM_LEDS == 2))
        flashCounter[GREEN_LED]++;
#endif
#if NUM_LEDS==2
        flashCounter[YELLOW_LED]++;
#endif
        ledTimer.val = tickGet();
        
    }
#if NUM_LEDS == 2
    switch (ledState[YELLOW_LED]) {
        case ON:
            APP_writeLED2(1);
            flashCounter[YELLOW_LED] = 0;
            break;
        case OFF:
            APP_writeLED2(0);
            flashCounter[YELLOW_LED] = 0;
            break;
        case FLASH_50_1HZ:
            // 1Hz (500ms per on or off is a count of 50 
            APP_writeLED2(flashCounter[YELLOW_LED]/50); 
            if (flashCounter[YELLOW_LED] >= 100) {
                flashCounter[YELLOW_LED] = 0;
            }
            break;
        case FLASH_50_HALF_HZ:
            APP_writeLED2(flashCounter[YELLOW_LED]/100);
            if (flashCounter[YELLOW_LED] >= 200) {
                flashCounter[YELLOW_LED] = 0;
            }
            break;
        case SINGLE_FLICKER_ON:
            APP_writeLED2(1);
            if (flashCounter[YELLOW_LED] >= 25) {     // 250ms
                flashCounter[YELLOW_LED] = 0;
                setLEDsByMode();
            }
            break;
        case SINGLE_FLICKER_OFF:
            APP_writeLED2(0);
            if (flashCounter[YELLOW_LED] >= 25) {     // 250ms
                flashCounter[YELLOW_LED] = 0;
                setLEDsByMode();
            }
            break;
        case LONG_FLICKER_ON:
            APP_writeLED2(1);
            if (flashCounter[YELLOW_LED] >= 50) {     // 500ms
                flashCounter[YELLOW_LED] = 0;
                setLEDsByMode();
            }
            break;
        case LONG_FLICKER_OFF:
            APP_writeLED2(0);
            if (flashCounter[YELLOW_LED] >= 50) {     // 500ms
                flashCounter[YELLOW_LED] = 0;
                setLEDsByMode();
            }
            break;
    }
#endif
#if ((NUM_LEDS == 1) || (NUM_LEDS == 2))
    switch (ledState[GREEN_LED]) {
        case ON:
            APP_writeLED1(1);
            flashCounter[GREEN_LED] = 0;
            break;
        case OFF:
            APP_writeLED1(0);
            flashCounter[GREEN_LED] = 0;
            break;
        case FLASH_50_1HZ:
            // 1Hz (500ms per cycle is a count of 50 
            APP_writeLED1(flashCounter[GREEN_LED]/50); 
            if (flashCounter[GREEN_LED] >= 100) {
                flashCounter[GREEN_LED] = 0;
            }
            break;
        case FLASH_50_HALF_HZ:
            APP_writeLED1(flashCounter[GREEN_LED]/100);
            if (flashCounter[GREEN_LED] >= 200) {
                flashCounter[GREEN_LED] = 0;
            }
            break;
        case SINGLE_FLICKER_ON:
            APP_writeLED1(1);
            if (flashCounter[GREEN_LED] >= 25) {     // 250ms
                flashCounter[GREEN_LED] = 0;
                setLEDsByMode();
            }
            break;
        case SINGLE_FLICKER_OFF:
            APP_writeLED1(0);
            if (flashCounter[GREEN_LED] >= 25) {     // 250ms
                flashCounter[GREEN_LED] = 0;
                setLEDsByMode();
            }
            break;
        case LONG_FLICKER_ON:
            APP_writeLED1(1);
            if (flashCounter[GREEN_LED] >= 50) {     // 500ms
                flashCounter[GREEN_LED] = 0;
                setLEDsByMode();
            }
            break;
        case LONG_FLICKER_OFF:
            APP_writeLED1(0);
            if (flashCounter[GREEN_LED] >= 50) {     // 500ms
                flashCounter[GREEN_LED] = 0;
                setLEDsByMode();
            }
            break;
    }
#endif
    // Do the mode changes by push button
    switch(mode) {
        case MODE_UNINITIALISED:
            // check the PB status
            if (APP_pbState() == 0) {
                // pb has not been pressed
                pbTimer.val = tickGet();
            } else {
                if (tickTimeSince(pbTimer) > 4*ONE_SECOND) {
                    // Do state transition from Uninitialised to Setup
                    mode = MODE_SETUP;
                    setupModePreviousMode = MODE_UNINITIALISED;
                    pbTimer.val = tickGet();
                    //start the request for NN
                    sendMessage2(OPC_RQNN, nn.bytes.hi, nn.bytes.lo);
                }
            }
            break;
        case MODE_SETUP:
            // check for 30secs in SETUP
            if (tickTimeSince(pbTimer) > 3*TEN_SECOND) {
                // return to previous mode
                mode = setupModePreviousMode;
                writeNVM(MODE_NVM_TYPE, MODE_ADDRESS, mode);
                // restore the NN
                if (mode == MODE_NORMAL) {
                    nn.word = previousNN.word;
                    sendMessage2(OPC_NNACK, nn.bytes.hi, nn.bytes.lo);
                    mnsDiagnostics[MNS_DIAGNOSTICS_NNCHANGE].asUint++;
                }
            }
            break;
        default:    // Normal modes
            // check the PB status
            if (APP_pbState() == 0) {
                // pb has not been pressed
                pbTimer.val = tickGet();
            } else {
                if (tickTimeSince(pbTimer) > 8*ONE_SECOND) {
                    // Do State transition from Normal to Setup
                    previousNN.word = nn.word;  // save the old NN
                    nn.bytes.lo = nn.bytes.hi = 0;
                    //return to setup
                    mode = MODE_SETUP;
                    setupModePreviousMode = MODE_NORMAL;
                    pbTimer.val = tickGet();
                    //start the request for NN
                    sendMessage2(OPC_RQNN, previousNN.bytes.hi, previousNN.bytes.lo);
                }
            } 
    }
}

/**
 * The MNS interrupt service routine. Handles the tickTime overflow to update
 * the extension bytes.
 */
static void mnsLowIsr(void) {
    // Tick Timer interrupt
    //check to see if the symbol timer overflowed
    if(TMR_IF)
    {
        /* there was a timer overflow */
        TMR_IF = 0;
        timerExtension1++;
        if(timerExtension1 == 0) {
            timerExtension2++;
        }
    }
    return;
}

/**
 * Get the MNS diagnostic values.
 * @param index the index indicating which diagnostic is required. 1..NUM_MNS_DIAGNOSTICS
 * @return the Diagnostic value or NULL if the value does not exist.
 */
static DiagnosticVal * mnsGetDiagnostic(uint8_t index) {
    if ((index<1) || (index>NUM_MNS_DIAGNOSTICS)) {
        return NULL;
    }
    return &(mnsDiagnostics[index-1]);
}

/**
 * Set the LEDs according to the current mode.
 */
void setLEDsByMode(void) {
       switch (mode) {
        case MODE_UNINITIALISED:
 #if NUM_LEDS == 2
            ledState[GREEN_LED] = ON;
            ledState[YELLOW_LED] = OFF;
#endif
#if NUM_LEDS == 1
            ledState[0] = FLASH_50_HALF_HZ;
#endif
            break;
        case MODE_SETUP:
#if NUM_LEDS == 2
            ledState[GREEN_LED] = OFF;
            ledState[YELLOW_LED] = FLASH_50_1HZ;
#endif
#if NUM_LEDS == 1
            ledState[0] = FLASH_50_1HZ;
#endif
            break;
        default:
#if NUM_LEDS == 2
            ledState[GREEN_LED] = OFF;
            ledState[YELLOW_LED] = ON;
#endif
#if NUM_LEDS == 1
            ledState[0] = ON;
#endif
            break;
    }
}

/**
 * This is the callback used by the service discovery responses.
 * @param type always set to TIMED_RESPONSE_RQSD
 * @param serviceIndex indicates the service requesting the responses
 * @param step loops through each service to be discovered
 * @return whether all of the responses have been sent yet.
 */
TimedResponseResult mnsTRserviceDiscoveryCallback(uint8_t type, uint8_t serviceIndex, uint8_t step) {
    if (step >= NUM_SERVICES) {
        return TIMED_RESPONSE_RESULT_FINISHED;
    }
//    if (services[step] != NULL) {
        sendMessage5(OPC_SD, nn.bytes.hi, nn.bytes.lo, step+1, services[step]->serviceNo, services[step]->version);
//    }
    return TIMED_RESPONSE_RESULT_NEXT;
}

/**
 * This is the callback used by the diagnostic responses. 
 * @param type always set to TIMED_RESPONSE_RDNG
 * @param serviceIndex indicates the service requesting the responses
 * @param step loops through each of the diagnostics
 * @return whether all of the responses have been sent yet.
 */
TimedResponseResult mnsTRallDiagnosticsCallback(uint8_t type, uint8_t serviceIndex, uint8_t step) {
    if (services[serviceIndex]->getDiagnostic == NULL) {
        return TIMED_RESPONSE_RESULT_FINISHED;
    }
    DiagnosticVal * d = services[serviceIndex]->getDiagnostic(step+1);   // steps start at 0 whereas diagnostics start at 1
    if (d == NULL) {
        // the requested diagnostic doesn't exist
        return TIMED_RESPONSE_RESULT_FINISHED;
    }
    // it was a request for a single diagnostic from a single service
    sendMessage6(OPC_DGN, nn.bytes.hi, nn.bytes.lo, serviceIndex+1, step+1, d->asBytes.hi, d->asBytes.lo);
    return TIMED_RESPONSE_RESULT_NEXT;
}
