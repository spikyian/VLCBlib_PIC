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
 * @brief
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
 * - \#define NUM_SERVICES with the number of service pointers in the services array.
 * - \#define APP_NVM_VERSION the version number of the data structures stored in NVM
 *                      this is located where NV#0 is stored therefore NV_ADDRESS
 *                      and NV_NVM_TYPE must be defined even without the NV service.
 * - \#define clkMHz       Must be set to the clock speed of the module. Typically 
 *                      this would be 4 or 16.
 * - \#define NN_ADDRESS   This must be set to the address in non volatile memory
 *                      at which the node number is to be stored.
 * - \#define NN_NVM_TYPE  This must be set to the type of the NVM where the node
 *                      number is to be stored.
 * - \#define MODE_ADDRESS This must be set to the address in non volatile memory
 *                      at which the mode variable is to be stored.
 * - \#define MODE_NVM_TYPE This must be set to the type of the NVM where the mode
 *                      variable is to be stored.
 * - \#define APP_setPortDirections() This macro must be set to configure the 
 *                      processor's pins for output to the LEDs and input from the
 *                      push button. It should also enable digital I/O if required 
 *                      by the processor.
 * - \#define APP_writeLED1(state) This macro must be defined to set LED1 (normally
 *                      yellow) to the state specified. 1 is LED on.
 * - \#define APP_writeLED2(state) This macro must be defined to set LED1 (normally
 *                      green) to the state specified. 1 is LED on.
 * - \#define APP_pbPressed() This macro must be defined to read the push button
 *                      input, returning true when the push button is held down.
 * - \#define NAME         The name of the module must be defined. Must be exactly 
 *                      7 characters. Shorter names should be padded on the right 
 *                      with spaces. The name must leave off the communications 
 *                      protocol e.g. the CANMIO module would be set to "MIO    ".
 * - \#define FCU_COMPAT    When defined the module will in start and return to FCU
 *                      compatible mode after reset as the default. When not 
 *                      defined the default is to support VLCB zero item responses
 *                      which improves efficiency and performance.
 * 
 * The following parameter values are required to be defined for use by MNS:
 * - \#define PARAM_MANU              See the manufacturer settings in vlcb.h
 * - \#define PARAM_MAJOR_VERSION     The major version number
 * - \#define PARAM_MINOR_VERSION     The minor version character. E.g. 'a'
 * - \#define PARAM_BUILD_VERSION     The build version number
 * - \#define PARAM_MODULE_ID         The module ID. Normally set to MTYP_VLCB
 * - \#define PARAM_NUM_NV            The number of NVs. Normally set to NV_NUM
 * - \#define PARAM_NUM_EVENTS        The number of events.
 * - \#define PARAM_NUM_EV_EVENT      The number of EVs per event
 * 
 */
#include <xc.h>

#include "vlcb.h"
#include "module.h"
#include "devincs.h"
#include "vlcbdefs_enums.h"
#include "mns.h"
#include "ticktime.h"
#include "nvm.h"
#include "timedResponse.h"
#include "statusDisplay.h"
#include "statusLeds.h"

/** Version of this Service implementation.*/
#define MNS_VERSION 1

// Forward declarations
static void mnsFactoryReset(void);
static void mnsPowerUp(void);
static void mnsPoll(void);
static Processed mnsProcessMessage(Message * m);
static void mnsLowIsr(void);
static uint8_t getParameter(uint8_t);
#ifdef VLCB_DIAG
static DiagnosticVal * mnsGetDiagnostic(uint8_t index);
/**
 * The diagnostic values supported by the MNS service.
 */
DiagnosticVal mnsDiagnostics[NUM_MNS_DIAGNOSTICS+1];
#endif

#ifdef PRODUCED_EVENTS
#include "event_teach.h"
#include "event_producer.h"
Boolean sendProducedEvent(Happening happening, EventState onOff);
#endif
#ifdef EVENT_HASH_TABLE
extern void rebuildHashtable(void);
#endif
void setLEDsByMode(void);

/**
 *  The descriptor for the MNS service.
 */
const Service mnsService = {
    SERVICE_ID_MNS,         // id
    2,                      // version
    mnsFactoryReset,        // factoryReset
    mnsPowerUp,             // powerUp
    mnsProcessMessage,      // processMessage
    mnsPoll,                // poll
#if defined(_18F66K80_FAMILY_)
    NULL,                   // highIsr
    mnsLowIsr,              // lowIsr
#endif
#ifdef VLCB_SERVICE
    NULL,                   // get ESD data
#endif
#ifdef VLCB_DIAG
    mnsGetDiagnostic        // getDiagnostic
#endif
};

// General MNS variables
/**
 * Node number.
 */
Word nn;            // node number
/**
 * Module operating mode.
 */
uint8_t mode_state; // operational mode
static uint8_t last_mode_state;

#define MODE_PRESETUP   0xFD    // hope not to clash with any defined by standard

/**
 * Module operating mode flags.
 */
uint8_t mode_flags; // operational mode flags
static uint8_t last_mode_flags;

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


/**
 * Module's push button handling.
 * Other UI options are not currently supported.
 */
TickValue pbTimer;
/** 
 * Addition check if the pb was pressed and not just whether time since pbTimer 
 * has been exceeded.
 */
static uint8_t pbWasPushed;

/* Heartbeat controls */
static uint8_t heartbeatSequence;
static TickValue heartbeatTimer;

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
 * Forward declaration for the TimedResponse callback function for sending
 * Parameter responses.
 * @param type type of TimedResponse
 * @param serviceIndex the service
 * @param step the TimedResponse step
 * @return indication if all the responses have been sent.
 */
TimedResponseResult  mnsTRrqnpnCallback(uint8_t type, uint8_t serviceIndex, uint8_t step);

/*
 * Defines for the PNN flags byte
 */
/** Parameter Flag bit for a Consumer module. */
#define PNN_FLAGS_CONSUMER  1
/** Parameter Flag bit for a Producer module. */
#define PNN_FLAGS_PRODUCER  2
/** Parameter Flag bit for a module in Normal mode. */
#define PNN_FLAGS_NORMAL    4
/** Parameter Flag bit for a module supporting the PIC Boot process. */
#define PNN_FLAGS_BOOT      8
/** Parameter Flag bit for a module which can consume its own events. */
#define PNN_FLAGS_COE       16
/** Parameter Flag bit for a module in Learn mode. */
#define PNN_FLAGS_LEARN     32
/** Parameter Flag bit for a module that is compliant with VLCB. */
#define PNN_FLAGS_VLCB      64

/*
 * Forward declaration for getParameterFlags.
 */
static uint8_t getParameterFlags(void);

/*
 * The Service functions
 */
/**
 * Perform the MNS factory reset. Just set the node number and mode to default.
 */
static void mnsFactoryReset(void) {
    nn.bytes.hi = NN_HI_DEFAULT;
    nn.bytes.lo = NN_LO_DEFAULT;
    writeNVM(NN_NVM_TYPE, NN_ADDRESS+1, nn.bytes.hi);
    writeNVM(NN_NVM_TYPE, NN_ADDRESS, nn.bytes.lo);
    
    last_mode_state = mode_state = MODE_UNINITIALISED;
    writeNVM(MODE_NVM_TYPE, MODE_ADDRESS, mode_state);

    last_mode_flags = mode_flags = 0;       // heartbeat disabled by default
    writeNVM(MODE_FLAGS_NVM_TYPE, MODE_FLAGS_ADDRESS, mode_flags);
}

/**
 * Perform the MNS power up.
 * Loads the node number and mode from non volatile memory. Initialises the LEDs 
 * clear the Diagnostics values.
 */
static void mnsPowerUp(void) {
    int temp;
    uint8_t i;
    
    temp = readNVM(NN_NVM_TYPE, NN_ADDRESS+1);
    if (temp < 0) {
        nn.bytes.hi = NN_HI_DEFAULT;
        nn.bytes.lo = NN_LO_DEFAULT;
    } else {
        nn.bytes.hi = (uint8_t)temp;
        temp = readNVM(NN_NVM_TYPE, NN_ADDRESS);
        if (temp < 0) {
            nn.bytes.hi = NN_HI_DEFAULT;
            nn.bytes.lo = NN_LO_DEFAULT;
        } else {
            nn.bytes.lo = (uint8_t)temp;
        }
    }
    temp = readNVM(MODE_NVM_TYPE, MODE_ADDRESS);
    if (temp < 0) {
        mode_state = MODE_DEFAULT;
    } else {
        mode_state = (uint8_t)temp;
    }
    setupModePreviousMode = mode_state;
    temp = readNVM(MODE_FLAGS_NVM_TYPE, MODE_FLAGS_ADDRESS);
    if (temp < 0) {
        mode_flags = 0;
    } else {
        mode_flags = (uint8_t)temp;
    }
    mode_flags &= ~FLAG_MODE_FCUCOMPAT; // force FCU compat off
#ifdef FCU_COMPAT
    mode_flags |= FLAG_MODE_FCUCOMPAT;  // force FCU compat on if defined
#endif
    last_mode_flags = mode_flags;
    setLEDsByMode();
    
    pbTimer.val = tickGet();
    pbWasPushed = FALSE;
    
#ifdef VLCB_DIAG
    // Clear the diagnostics
    for (i=1; i<= NUM_MNS_DIAGNOSTICS; i++) {
        mnsDiagnostics[i].asInt = 0;
    }
    mnsDiagnostics[MNS_DIAGNOSTICS_COUNT].asInt = NUM_MNS_DIAGNOSTICS;
#endif
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
    if (mode_state == MODE_SETUP) {
        switch (m->opc) {
            case OPC_SNN:   // Set node number
                if (m->len < 3) {
#ifdef VLCB_GRSP
                    sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_SNN, SERVICE_ID_MNS, CMDERR_INV_CMD);
#endif
                } else {    
                    nn.bytes.hi = m->bytes[0];
                    nn.bytes.lo = m->bytes[1];
                    writeNVM(NN_NVM_TYPE, NN_ADDRESS+1, nn.bytes.hi);
                    writeNVM(NN_NVM_TYPE, NN_ADDRESS, nn.bytes.lo);
                    
                    mode_state = MODE_NORMAL;
//                    writeNVM(MODE_NVM_TYPE, MODE_ADDRESS, mode_state);
#ifdef EVENT_HASH_TABLE
                    rebuildHashtable();
#endif
                    
                    sendMessage2(OPC_NNACK, nn.bytes.hi, nn.bytes.lo);
#ifdef VLCB_DIAG
                    mnsDiagnostics[MNS_DIAGNOSTICS_NNCHANGE].asUint++;
#endif
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
                sendMessage5(OPC_PNN, 0,0, PARAM_MANU, PARAM_MODULE_ID, getParameterFlags());
                return PROCESSED;
            default:
                break;
        }
        return NOT_PROCESSED;
    }
    // No NN but in Normal mode or equivalent message processing
    switch (m->opc) {
        case OPC_QNN:   // Query node
            sendMessage5(OPC_PNN, nn.bytes.hi,nn.bytes.lo, PARAM_MANU, PARAM_MODULE_ID, getParameterFlags());
            return PROCESSED;
#ifdef VLCB_MODE
        case OPC_MODE:  // MODE
            if (m->len < 4) {
                sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_MODE, SERVICE_ID_MNS, CMDERR_INV_CMD);
                return PROCESSED;
            }
            if ((m->bytes[0] == 0) && (m->bytes[1] == 0)) { // Global MODE
                newMode = m->bytes[2];
                // do broadcast MODE flag changes
                switch (newMode) {
                    case MODE_HEARTBEAT_ON:
                        mode_flags |= FLAG_MODE_HEARTBEAT;
                        return PROCESSED;
                    case MODE_HEARTBEAT_OFF:
                        mode_flags &= ~FLAG_MODE_HEARTBEAT;
                        return PROCESSED;
                    case MODE_FCUCOMPAT_ON:
                        mode_flags |= FLAG_MODE_FCUCOMPAT;
                        return PROCESSED;
                    case MODE_FCUCOMPAT_OFF:
                        mode_flags &= ~FLAG_MODE_FCUCOMPAT;
                        return PROCESSED;
                    default:
                        break;
                }
            }
            break;
#endif
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
#ifdef VLCB_GRSP
                sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_RQNPN, SERVICE_ID_MNS, CMDERR_INV_CMD);
#endif
                return PROCESSED;
            }
            if (m->bytes[2] > 20) {
                sendMessage3(OPC_CMDERR, nn.bytes.hi, nn.bytes.lo, CMDERR_INV_PARAM_IDX);
#ifdef VLCB_GRSP
                sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_RQNPN, SERVICE_ID_MNS, CMDERR_INV_PARAM_IDX);
#endif
                return PROCESSED;
            }
            i = getParameter(m->bytes[2]);
            sendMessage4(OPC_PARAN, nn.bytes.hi, nn.bytes.lo, m->bytes[2], i);

            if (((mode_flags & FLAG_MODE_FCUCOMPAT) == 0) && (m->bytes[2] == 0)) {
                startTimedResponse(TIMED_RESPONSE_RQNPN, findServiceIndex(SERVICE_ID_MNS), mnsTRrqnpnCallback);
            }

            return PROCESSED;
        case OPC_NNRSM: // reset to manufacturer defaults
            previousNN.word = nn.word;  // save the old NN
            factoryReset();
            if (previousNN.word != 0) {
                sendMessage2(OPC_NNREL, previousNN.bytes.hi, previousNN.bytes.lo);
            }
            RESET();
#ifdef VLCB_DIAG
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
                    sendMessage5(OPC_DGN, nn.bytes.hi, nn.bytes.lo, OPC_RDGN, m->bytes[2], 0);
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
#endif
#ifdef VLCB_SERVICE
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
#endif
#ifdef VLCB_MODE
        case OPC_MODE:  // set operating mode
            if (m->len < 4) {
                sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_MODE, SERVICE_ID_MNS, CMDERR_INV_CMD);
                return PROCESSED;
            }
            newMode = m->bytes[2];
            previousNN.word = nn.word;  // save the old NN
            
            switch (newMode) {
                case MODE_SETUP:
                case MODE_UNINITIALISED:
                    if (mode_state == MODE_NORMAL) { // check current mode
                        sendMessage2((newMode == MODE_SETUP) ? OPC_RQNN : OPC_NNREL, nn.bytes.hi, nn.bytes.lo);
                        sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_MODE, SERVICE_ID_MNS, GRSP_OK);
                        nn.bytes.lo = nn.bytes.hi = 0;
                        writeNVM(NN_NVM_TYPE, NN_ADDRESS+1, nn.bytes.hi);
                        writeNVM(NN_NVM_TYPE, NN_ADDRESS, nn.bytes.lo);
                        //return to setup
                        mode_state = (newMode == MODE_SETUP) ? MODE_SETUP : MODE_UNINITIALISED;
                        setupModePreviousMode = MODE_NORMAL;
                        // Update the LEDs
                        setLEDsByMode();
                        return PROCESSED;
                    }
                    break;
                case MODE_HEARTBEAT_ON:
                    mode_flags |= FLAG_MODE_HEARTBEAT;
                    sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_MODE, SERVICE_ID_MNS, GRSP_OK);
                    return PROCESSED;
                case MODE_HEARTBEAT_OFF:
                    mode_flags &= ~FLAG_MODE_HEARTBEAT;
                    sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_MODE, SERVICE_ID_MNS, GRSP_OK);
                    return PROCESSED;
                case MODE_FCUCOMPAT_ON:
                    mode_flags |= FLAG_MODE_FCUCOMPAT;
                    sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_MODE, SERVICE_ID_MNS, GRSP_OK);
                    return PROCESSED;
                case MODE_FCUCOMPAT_OFF:
                    mode_flags &= ~FLAG_MODE_FCUCOMPAT;
                    sendMessage5(OPC_GRSP, nn.bytes.hi, nn.bytes.lo, OPC_MODE, SERVICE_ID_MNS, GRSP_OK);
                    return PROCESSED;
                default:
                    break;
            }
            return NOT_PROCESSED;
#endif
        case OPC_NNRST: // reset CPU
            RESET();
            return PROCESSED;   // should never get here
        default:
            break;
    }
    return NOT_PROCESSED;
}

/**
 * Get the current module status flags as reported by Parameter 8 and PNN.
 * @return the current module status flags
 */
static uint8_t getParameterFlags() {
    uint8_t flags;
    flags = 0;
    if (have(SERVICE_ID_CONSUMER)) {
        flags |= PNN_FLAGS_CONSUMER;
    }
    if (have(SERVICE_ID_PRODUCER)) {
        flags |= PNN_FLAGS_PRODUCER;
    }
    if (flags == (PNN_FLAGS_PRODUCER | PNN_FLAGS_CONSUMER)) flags |= PNN_FLAGS_COE;     // CoE
    if (have(SERVICE_ID_BOOT)) {
        flags |= PNN_FLAGS_BOOT;
    }
    if (mode_flags & FLAG_MODE_LEARN) {
        flags |= PNN_FLAGS_LEARN;
    }
    if (mode_state == MODE_NORMAL) {
        flags |= PNN_FLAGS_NORMAL;
    }
#ifdef VLCB
    flags |= PNN_FLAGS_VLCB; // add VLCB compatability
#endif
    return flags;
}

#ifdef VLCB_DIAG
/**
 * Update the module status with an error.
 * This is safe to be called from the CAN send function.
 */
void updateModuleErrorStatus(void) {
    if (mnsDiagnostics[MNS_DIAGNOSTICS_STATUS].asBytes.lo < 0xFF) {
        mnsDiagnostics[MNS_DIAGNOSTICS_STATUS].asBytes.lo++;
    }
}
#endif

/**
 * Called regularly, processing for LED flashing and mode state transition 
 * timeouts.
 */
static void mnsPoll(void) {
#ifdef VLCB_DIAG
    // Heartbeat message
    if (mode_state == MODE_NORMAL) {
        if (tickTimeSince(heartbeatTimer) > 5*ONE_SECOND) {
            if (mode_flags & FLAG_MODE_HEARTBEAT) {
                sendMessage5(OPC_HEARTB, nn.bytes.hi,nn.bytes.lo,heartbeatSequence++,mnsDiagnostics[MNS_DIAGNOSTICS_STATUS].asBytes.lo,0);
            }
            heartbeatTimer.val = tickGet();
            if (mnsDiagnostics[MNS_DIAGNOSTICS_STATUS].asBytes.lo > 0) {
                mnsDiagnostics[MNS_DIAGNOSTICS_STATUS].asBytes.lo--;
            }
        }
    }
#endif
    
    // Update mode if app or any service has changed them
    if (mode_flags != last_mode_flags) {
        writeNVM(MODE_FLAGS_NVM_TYPE, MODE_FLAGS_ADDRESS, mode_flags);
        last_mode_flags = mode_flags;
    }
    if (mode_state != last_mode_state) {
        // don't persist setup mode
        if ((mode_state == MODE_UNINITIALISED) || (mode_state == MODE_NORMAL)) {
            writeNVM(MODE_FLAGS_NVM_TYPE, MODE_ADDRESS, mode_state);
        }
        last_mode_state = mode_state;
    }
#ifdef VLCB_DIAG
    // Module uptime
    if (tickTimeSince(uptimeTimer) > ONE_SECOND) {
        uptimeTimer.val = tickGet();
        mnsDiagnostics[MNS_DIAGNOSTICS_UPTIMEL].asUint++;
        if (mnsDiagnostics[MNS_DIAGNOSTICS_UPTIMEL].asUint == 0) {
            mnsDiagnostics[MNS_DIAGNOSTICS_UPTIMEH].asUint++;
        }
    }
#endif
    
    // Do the mode changes by push button
    switch(mode_state) {
        case MODE_UNINITIALISED:
            // check the PB status
            if (APP_pbPressed() == 0) {
                // pb has been released
                pbTimer.val = tickGet();
            } else {
                // No need to release the PB
                if (tickTimeSince(pbTimer) > 4*ONE_SECOND) {
                    // Do state transition from Uninitialised to Setup
                    mode_state = MODE_PRESETUP;
                    setupModePreviousMode = MODE_UNINITIALISED;
                    setLEDsByMode();
                }
            }
            break;
        case MODE_PRESETUP:
            if (APP_pbPressed() == 0) {
                // pb has been released
                
                // Do state transition from Uninitialised to Setup
                mode_state = MODE_SETUP;
                setupModePreviousMode = MODE_UNINITIALISED;
                pbTimer.val = tickGet();    // reset the timer ready for Setup mode
                //start the request for NN
                sendMessage2(OPC_RQNN, nn.bytes.hi, nn.bytes.lo);
                setLEDsByMode();
            }
            pbTimer.val = tickGet();
            break;
        case MODE_SETUP:
            if (APP_pbPressed() == 0) {
                // PB has been released

                if ((tickTimeSince(pbTimer) > HUNDRED_MILI_SECOND) && (tickTimeSince(pbTimer) < 2*ONE_SECOND)) {
                    // a short press returns to previous mode
                    mode_state = setupModePreviousMode;
                    if (mode_state == MODE_NORMAL) {
                        // restore the NN
                        nn.word = previousNN.word;
                        sendMessage2(OPC_NNACK, nn.bytes.hi, nn.bytes.lo);
#ifdef VLCB_DIAG
                        mnsDiagnostics[MNS_DIAGNOSTICS_NNCHANGE].asUint++;
#endif
                    }
                    setLEDsByMode();
                }
                if (tickTimeSince(pbTimer) > 4*ONE_SECOND) {
                    mode_state = MODE_UNINITIALISED;
                    setLEDsByMode();
                }
                pbTimer.val = tickGet();
                pbWasPushed = FALSE;
            } else {
                pbWasPushed = TRUE;
            }
            break;
        default:    // Normal mode
            // check the PB status
            if (APP_pbPressed() == 0) {
                // PB has been released
                if (pbWasPushed && (tickTimeSince(pbTimer) > HUNDRED_MILI_SECOND) && (tickTimeSince(pbTimer) < 2*ONE_SECOND)) {
                    // Do State transition from Normal to Setup
                    previousNN.word = nn.word;  // save the old NN
                    nn.bytes.lo = nn.bytes.hi = 0;
                    //return to setup
                    mode_state = MODE_SETUP;
                    setupModePreviousMode = MODE_NORMAL;
                    //start the request for NN
                    sendMessage2(OPC_RQNN, previousNN.bytes.hi, previousNN.bytes.lo);
                    setLEDsByMode();
                }
                if (pbWasPushed &&(tickTimeSince(pbTimer) >= 4*ONE_SECOND)) {
                    // was down for more than 4 sec, Move to Uninitialised
                    previousNN.word = nn.word;  // save the old NN
                    nn.bytes.lo = nn.bytes.hi = 0;
                    //go to uninitialised
                    mode_state = MODE_UNINITIALISED;
                    setupModePreviousMode = MODE_NORMAL;
                    //start the request for NN
                    sendMessage2(OPC_NNREL, previousNN.bytes.hi, previousNN.bytes.lo);
                    setLEDsByMode();
                }
                pbTimer.val = tickGet();
                pbWasPushed = FALSE;
            } else {
                pbWasPushed = TRUE;
            }
    }
}

#if defined(_18F66K80_FAMILY_)
/**
 * The MNS interrupt service routine. Handles the tickTime overflow to update
 * the extension bytes.
 */
static void mnsLowIsr(void) {
    // Tick Timer interrupt
    //check to see if the symbol timer overflowed
    if(TMR_IF) {
        /* there was a timer overflow */
        TMR_IF = 0;
        timerExtension1++;
        if(timerExtension1 == 0) {
            timerExtension2++;
        }
    }
    return;
}
#endif

#ifdef VLCB_DIAG
/**
 * Get the MNS diagnostic values.
 * @param index the index indicating which diagnostic is required. 0..NUM_MNS_DIAGNOSTICS
 * @return the Diagnostic value or NULL if the value does not exist.
 */
static DiagnosticVal * mnsGetDiagnostic(uint8_t index) {
    if (index > NUM_MNS_DIAGNOSTICS) {
        return NULL;
    }
    return &(mnsDiagnostics[index]);
}
#endif

/**
 * Set the LEDs according to the current mode.
 */
void setLEDsByMode(void) {
       switch (mode_state) {
        case MODE_UNINITIALISED:
            showStatus(STATUS_UNINITIALISED);
            break;
        case MODE_SETUP:
        case MODE_PRESETUP:
            showStatus(STATUS_SETUP);
            break;
        default:
            showStatus(STATUS_NORMAL);
            break;
    }
}

/**
 * Return the parameter specified by idx. 
 * @param idx the parameter number
 * @return parameter value
 */
static uint8_t getParameter(uint8_t idx) {
    uint8_t i;
    switch(idx) {
    case PAR_NUM:       // 0 Number of parameters
        return 20;
    case PAR_MANU:      // 1 Manufacturer id
        return PARAM_MANU;
    case PAR_MINVER:	// 2 Minor version letter
        return PARAM_MINOR_VERSION;
    case PAR_MTYP:	 	// 3 Module type code
        return PARAM_MODULE_ID;
    case PAR_EVTNUM:	// 4 Number of events supported
        return PARAM_NUM_EVENTS;
    case PAR_EVNUM:		// 5 Event variables per event
        return PARAM_NUM_EV_EVENT;
    case PAR_NVNUM:		// 6 Number of Node variables
        return PARAM_NUM_NV;
    case PAR_MAJVER:	// 7 Major version number
        return PARAM_MAJOR_VERSION;
    case PAR_FLAGS:		// 8 Node flags
        return getParameterFlags();
    case PAR_CPUID:		// 9 Processor type
        return CPU;
    case PAR_BUSTYPE:	// 10 Bus type
        if (have(SERVICE_ID_CAN)) {
            return PB_CAN;
        }
        return 0;
    case PAR_LOAD1:		// 11 load address, 4 bytes
        return 0x00;
    case PAR_LOAD2:		// 12 load address, 4 bytes
        return 0x08;
    case PAR_LOAD3:		// 13 load address, 4 bytes
        return 0x00;
    case PAR_LOAD4:		// 14 load address, 4 bytes
        return 0x00;
#if defined(_18FXXQ83_FAMILY_)
    case PAR_CPUMID:	// 15 CPU manufacturer's id as read from the chip config space, 4 bytes (note - read from cpu at runtime, so not included in checksum)
        return *(const uint8_t*)0x3FFFFC; // Device revision byte 0
    case PAR_CPUMID+1:  // 16
        return *(const uint8_t*)0x3FFFFD; // Device recision byte 1
#endif
    case PAR_CPUMID+2:  // 17
        return *(const uint8_t*)0x3FFFFE;  // Device ID byte 0
    case PAR_CPUMID+3:  // 18
        return *(const uint8_t*)0x3FFFFF;  // Device ID byte 1
    case PAR_CPUMAN:	// 19 CPU manufacturer code
        return CPUM_MICROCHIP;
    case PAR_BETA:		// 20 Beta revision (numeric), or 0 if release
        return PARAM_BUILD_VERSION;
    default:
        return 0;
    }
}

#ifdef VLCB_SERVICE
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
#endif

#ifdef VLCB_DIAG
/**
 * This is the callback used by the diagnostic responses. 
 * @param type always set to TIMED_RESPONSE_RDNG
 * @param serviceIndex indicates the service requesting the responses
 * @param step loops through each of the diagnostics
 * @return whether all of the responses have been sent yet.
 */
TimedResponseResult mnsTRallDiagnosticsCallback(uint8_t type, uint8_t serviceIndex, uint8_t step) {
    if (services[serviceIndex]->getDiagnostic == NULL) {
        sendMessage6(OPC_DGN, nn.bytes.hi, nn.bytes.lo, serviceIndex+1, 0, 0, 0);
        return TIMED_RESPONSE_RESULT_FINISHED;
    }
    DiagnosticVal * d = services[serviceIndex]->getDiagnostic(step);   // get the actual diagnostic value
    if (d == NULL) {
        // the requested diagnostic doesn't exist
        return TIMED_RESPONSE_RESULT_FINISHED;
    }
    // return the diagnostic
    sendMessage6(OPC_DGN, nn.bytes.hi, nn.bytes.lo, serviceIndex+1, step, d->asBytes.hi, d->asBytes.lo);
    return TIMED_RESPONSE_RESULT_NEXT;
}
#endif

/**
 * This is the callback used by the RQNPN Parameter responses. 
 * @param type always set to TIMED_RESPONSE_RQNPN
 * @param serviceIndex indicates the service requesting the responses
 * @param step loops through each of the parameters
 * @return whether all of the responses have been sent yet.
 */
TimedResponseResult  mnsTRrqnpnCallback(uint8_t type, uint8_t serviceIndex, uint8_t step) {
    if (step >= 20) {
        return TIMED_RESPONSE_RESULT_FINISHED;
    }
    sendMessage4(OPC_PARAN, nn.bytes.hi, nn.bytes.lo, step+1, getParameter(step+1));
    return TIMED_RESPONSE_RESULT_NEXT;
}
