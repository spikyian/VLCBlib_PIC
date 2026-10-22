# VLCBlib_PIC
C library for VLCB for use on PIC devices.
Uses XC8.
Has CAN transport functionality using the PIC18 ECAN peripheral.

## The module application
An VLCB module using this library needs to consider the following application responsibilities:

### Inputs
   - Monitoring input pins, 
   - Perform behaviour based upon module NV settings,
   - Save input state according to type of input in RAM
   - Send events to the transport layer according to behaviour requirements

### Outputs
   - Receive messages from the transport layer or receive actions from the action queue
   - Use the NV settings to determine the behaviour
   - Save state into non volatile memory e.g. EEPROM
   - Make changes to the output pin state

### On power on
   - Restore output state using the information stored in non volatile memory
   - Initialise input state based upon current input pin state

### Regular poll
   - Update and perform time based behaviour

## A module designer needs to:
 1. Determine which services the module will use.
 2. Write a modules.h which is used by the VLCBlib to control its operation.
 3. If the module will use NVs then:
     - Define the NV usage and allocation,
     - Define the memory allocation (type and address) for the NVs,
     - Define the default value (factory reset settings) of the NVs,
     - Determine whether any validation of NV settings is required.
 4. If the module has a CAN interface:
     - Decide where in NVM the CANID is to be stored,
     - Decide how much memory can be used for transmit and receive buffers.
 5. If the module is to support event teaching:
     - Decide the number of events and number of EVs per event,
     - Define the event EV usage and allocation,
     - Define the memory allocation (type and address) for the EVs,
 6. If the module also supports produced events then:
     - If the concept of Happenings is to be used then defined the size of the Happening identifier,
     - Provide a function to provide the current event state given a Happening
 7. If the module also supports consumed events then:
     - If Actions concept is to be used then the size of the Action queue should be defined.
 8. All modules also need:
     - The address and type of NVM where the module's node number is to be stored,
     - The address and type of NVM where the mode is to be stored,
     - Define the module type name, module ID and version,
     - The number of LEDs used to display module state,
     - A macro to obtain the push button state,
     - A macro to set up the ports for the LEDs and push button.

## Other information
   - The module Uninitialised/Setup/Normal state is available using the uint8_t mode_state global variable.
   - Other stateful mode flags are available in the uint8_t mode_flags variable.
   - The module's node number is available as Word nn global variable.
   - A module may define a function to process VLCB messages before being handled by the library.
   - A module may also define a function to process VLCB messages if not handled by the library. 
  
# Full documentation
The full user documentation (look in the \*.h files) and developer documentation (look in the \*.c files) can be viewed by opening [https://spikyian.github.io/VLCBlib_PIC/](https://spikyian.github.io/VLCBlib_PIC/) in your browser.

## Versions and releases
The library's version is in `vlcb_version.h` as macros, so it costs nothing
unless a module uses it. Releases are numbered vMAJOR.MINOR.PATCH:
- MAJOR: a change that needs modules to be changed,
- MINOR: new features that existing modules do not need to change for,
- PATCH: fixes only.

A pre-release, such as a release candidate for testers, adds -rcN (or -alphaN, -betaN), e.g. v1.0.0-rc1.

A module that needs a particular release says so in module.h:

    #define VLCBLIB_REQUIRED    VLCBLIB_VERSION_NUMBER(1,0,0)

and then will not build against an older library.

To make a release, do not edit `vlcb_version.h` by hand. On GitHub go to
Actions > Release > Run workflow, choose the branch, enter the version and
press Run workflow. It sets the version in `vlcb_version.h` and `Doxyfile`,
moves the Unreleased part of `CHANGELOG.md` under the version, commits and tags
the commit, and creates the GitHub release with those notes. (If the branch is
protected, allow GitHub Actions to push to it.)

