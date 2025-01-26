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
 * @author Original CANACC8 assembler version (c) Mike Bolton
 * @author Modifications to EEPROM routines and conversion to C18 (c) Andrew Crosland
 * @author FLASH routines by (c) Chuck Hoelzen
 * @author Modifications, refinements & combine EEPROM and FLASH into one module (C) Pete Brownlow 2014-2017   software@upsys.co.u
 * @author Major rewrite  Ian Hogg  
 * @date Dec 2022
 * 
 * EEPROM read and write are straightforward since the PIC offers byte read and 
 * write access to the EEPROM so these routines just provide the necessary functionality
 * and register access and write unlocking.
 * 
 * Flash read is also done using the PIC reading of a single byte of flash.
 * 
 * Flashing writing is complicated due to the PIC only supporting page writes and
 * bits can only be cleared. If any bit needs to be set then the entire page needs 
 * to be erased. To do this a RAM buffer is used to hold the current page's contents.
 * Writing a byte to Flash involves checking if the current page in the buffer 
 * is the correct page for the byte to be written, if it is not then the current page 
 * may need to be flushed out before the correct page is read in and the byte modified.
 * To flush the page then the buffer needs to be checked to see if it is dirty (been modified).
 * If the buffer is dirty and bits have been cleared then the page first needs to 
 * be erased before the whole page is is written.
 * 
 */

/**
 * @file
 * @brief
 * Non volatile memory functions.
 * 
 * @details
 * Functionality for reading and writing to EEPROM and Flash NVM.
 * Read and write to EEPROM is straightforward.
 * Reading from flash is also straightforward but writing to flash is complex. 
 * This involves needing to erase a block if changing any bit from 0 to 1 and
 * if changing a single byte the entire block must be read, the byte changed 
 * and the entire block written back.
 */

#include <xc.h>
#include "vlcb.h"
#include "hardware.h"
#include "nvm.h"
#include "mns.h"

#if defined(_18F66K80_FAMILY_)
#define FLASH_PAGE_SIZE _FLASH_ERASE_SIZE
#endif

#if defined(_18FXXQ83_FAMILY_)

/**
 * Contains the size of a Flash page in bytes.
 */
#define FLASH_PAGE_SIZE          (256U)

/**
 * Contains the total size of Flash in bytes.
 */
#define PROGMEM_SIZE               (0x020000U)

/**
 * @ingroup nvm_driver
 * @def BUFFER_RAM_START_ADDRESS
 * Contains the starting address of the buffer RAM.
 */
#define BUFFER_RAM_START_ADDRESS   (0x3700U)

/**
 * @ingroup nvm_driver
 * @def EEPROM_START_ADDRESS
 * Contains the starting address of EEPROM.
 */
#define EEPROM_START_ADDRESS       (0x380000U)

/**
 * @ingroup nvm_driver
 * @def EEPROM_SIZE
 * Contains the size of EEPROM in bytes.
 */
#define EEPROM_SIZE                (1024U)
#endif

#define NVMCMD_READ             0x00
#define NVMCMD_READ_POSTINC     0x01
#define NVMCMD_READPAGE         0x02
#define NVMCMD_WRITE            0x03
#define NVMCMD_WRITE_POSTINC    0x04
#define NVMCMD_WRITEPAGE        0x05
#define NVMCMD_ERASEPAGE        0x06
#define NVMCMD_NOP              0x07


// Structure for tracking Flash operations
static union
{
    uint8_t asByte;       
    struct  {
        uint8_t writeNeeded:1; //flag if buffer is modified
        uint8_t eraseNeeded:1;  //flag if long write with block erase
    };
} flashFlags;

#if defined(_18F66K80_FAMILY_)
static flash_data_t       flashBuffer[FLASH_PAGE_SIZE];    // Assumes that Erase and Write are the same size
#endif
#if defined(_18FXXQ83_FAMILY_)
// On the Q series the NVM peripheral uses a fixed RAM buffer at 0x3700
 flash_data_t        * flashBuffer = (flash_data_t *)BUFFER_RAM_START_ADDRESS;
#endif
static flash_address_t    flashBlock;     //address of current flash block

/** Provides the Flash Block number given an address.*/
#define BLOCK(A)    (A&(~((flash_address_t)FLASH_PAGE_SIZE-1)))
/** Provides the offset into a Flash Block given an address.*/
#define OFFSET(A)   (A&(FLASH_PAGE_SIZE-1))

/**
 *  Initialise variables for Flash program tracking.
 */
void initRomOps(void) {
    flashFlags.asByte = 0;  // no write and no erase
    flashBlock = 0x0800; // invalid but as long a write isn't needed it will be 
                         // ok. Next write will always be to a different block.
    TBLPTRU = 0;
#if defined(_18FXXQ83_FAMILY_)
    NVMCON1bits.WRERR = 0;
#endif
}

/**
 * Read EEPROM.  
 * @param index the address
 * @return the value
 */
eeprom_data_t EEPROM_Read(eeprom_address_t index) {
#if defined (_18F66K80_FAMILY_)
    // do read of EEPROM
    while (EECON1bits.WR)       // Errata says this is required
        ;
    // EEADRH = index >> 8;        //  High byte of address to read
    SET_EADDRH((index >> 8)&0xFF);
    EEADR = index & 0xFF;       	/* Low byte of Data Memory Address to read */
    EECON1bits.EEPGD = 0;    	/* Point to DATA memory */
    EECON1bits.CFGS = 0;    	/* Access program FLASH/Data EEPROM memory */
    EECON1bits.RD = 1;			/* EEPROM Read */
    while (EECON1bits.RD)
        ;

    asm("NOP");                 /* data available after a NOP */

    return EEDATA;
#endif
#if defined(_18FXXQ83_FAMILY_)
    // ready?
    while (NVMCON0bits.GO)
        ;
    //Load NVMADR with the desired byte address
    NVMADRU = 0x38;
    NVMADRH = (uint8_t) (index >> 8);
    NVMADRL = (uint8_t) index;

    //Set the byte read command
    NVMCON1bits.NVMCMD = NVMCMD_READ;
    
    //Start byte read
    NVMCON0bits.GO = 1;
    while (NVMCON0bits.GO)
        ;

    return NVMDATL;
#endif
}

/**
 * Write a byte to EEPROM
 * @param index the address
 * @param value the value to be written
 * @return 0 for success or error otherwise
 */
uint8_t EEPROM_Write(eeprom_address_t index, eeprom_data_t value) {
    uint8_t interruptEnabled;
    interruptEnabled = geti(); // store current global interrupt state
    do {
#if defined (_18F66K80_FAMILY_)
        SET_EADDRH((index >> 8)&0xFF);      // High byte of address to write
        EEADR = index & 0xFF;       	/* Low byte of Data Memory Address to write */
        EEDATA = value;
        EECON1bits.EEPGD = 0;       /* Point to DATA memory */
        EECON1bits.CFGS = 0;        /* Access program FLASH/Data EEPROM memory */
        EECON1bits.WREN = 1;        /* Enable writes */
        /* Disable Interrupts */
        bothDi();                       
        EECON2 = 0x55;
        EECON2 = 0xAA;
        EECON1bits.WR = 1;
        while (EECON1bits.WR)       // should wait until WR clears
            ;
        while (!EEIF)
            ;
        EEIF = 0;
        if (interruptEnabled) {     // Only enable interrupts if they were enabled at function entry
            /* Re-enable Interrupts */
            bothEi();                  
        }
        EECON1bits.WREN = 0;		/* Disable writes */
#endif
#if defined(_18FXXQ83_FAMILY_)
        // ready?
        while (NVMCON0bits.GO)
            ;
        //Load NVMADR with the target address of the byte
        NVMADRU = 0x38;
        NVMADRH = (uint8_t) (index >> 8);
        NVMADRL = (uint8_t) index;

        //Load NVMDAT with the desired value
        NVMDATL = value;

        //Set the byte write command
        NVMCON1bits.NVMCMD = NVMCMD_WRITE;

        //Disable global interrupt
        bothDi();

        //Perform the unlock sequence 
        NVMLOCK = 0x55;
        NVMLOCK = 0xAA;
        
        //Start byte write
        NVMCON0bits.GO = 1;

        if (interruptEnabled) {     // Only enable interrupts if they were enabled at function entry
            /* Re-enable Interrupts */
            bothEi();                  
        }

        //Clear the NVM Command
        NVMCON1bits.NVMCMD = NVMCMD_NOP;
#endif
        // check that it worked
        if (EEPROM_Read(index) == value) {
            break;
        }
#ifdef VLCB_DIAG
        mnsDiagnostics[MNS_DIAGNOSTICS_MEMERRS].asUint++;
        updateModuleErrorStatus();
#endif
    } while (1);
    return GRSP_OK;
}



/**
 * Read Flash. 
 * @param address the address
 * @return the value
 */
static flash_data_t FLASH_Read(flash_address_t address) {
    // do read of Flash
    if (BLOCK(address) == flashBlock) {
        // if the block is the current one then get it directly
        return flashBuffer[OFFSET(address)];
    } else {
        // we'll read single byte from flash
#if defined (_18F66K80_FAMILY_)
        TBLPTR = address;
        TBLPTRU = 0;
        asm("TBLRD");
#endif
#if defined (_18FXXQ83_FAMILY_)
        //Load table pointer with the target address of the byte  
        TBLPTRU = (uint8_t) (address >> 16);
        TBLPTRH = (uint8_t) (address >> 8);
        TBLPTRL = (uint8_t) address;
        //Execute table read 
        asm("TBLRD*");
#endif
        return TABLAT;
    }
}

/**
 * Erase a block of flash.
 * May block awaiting for the application to indicate that it is a suitable time
 * to allow the CPU to be halted.
 */
void eraseFlashBlock(void) {
    uint8_t interruptEnabled;
    // Call back into the application to check if now is a good time to write the flash
    // as the processor will be suspended for up to 2ms.
    while (! APP_isSuitableTimeToWriteFlash())
        ;
    
    interruptEnabled = geti(); // store current global interrupt state
#if defined (_18F66K80_FAMILY_)
    TBLPTR = flashBlock;
    TBLPTRU = 0;
    EECON1bits.EEPGD = 1;   // 1=Program memory, 0=EEPROM
    EECON1bits.CFGS = 0;    // 0=Program memory/EEPROM, 1=ConfigBits
    EECON1bits.WREN = 1;    // enable write to memory
    EECON1bits.FREE = 1;    // enable row erase operation
    bothDi();     // disable all interrupts
    EECON2 = 0x55;          // write 0x55
    EECON2 = 0xaa;          // write 0xaa
    EECON1bits.WR = 1;      // start erasing
    while(EECON1bits.WR)    // wait to finish
        ;
    EECON1bits.WREN = 0;    // disable write to memory
#endif
#if defined (_18FXXQ83_FAMILY_)
    // ready?
    while (NVMCON0bits.GO)
        ;
    //Load NVMADR with the any address in the memory page. NVMADRL is ignored
    NVMADRU = (uint8_t) (flashBlock >> 16);
    NVMADRH = (uint8_t) (flashBlock >> 8);

    NVMCON1bits.NVMCMD = NVMCMD_ERASEPAGE;      //Set the page erase command
    bothDi();                       // disable all interrupts
    //Perform the unlock sequence 
    NVMLOCK = 0x55;
    NVMLOCK = 0xAA;
    NVMCON0bits.GO = 1;             //Start byte write
    while (NVMCON0bits.GO)          // Wait to complete
        ;
    NVMCON1bits.NVMCMD = NVMCMD_NOP;      //Clear the NVM Command
#endif
    if (interruptEnabled) {     // Only enable interrupts if they were enabled at function entry
        bothEi();                   /* Enable Interrupts */
    }
}


/**
 * Flush the current flash buffer out to flash.
 * Will suspend the CPU.
 */
void flushFlashBlock(void) {
    uint8_t interruptEnabled;
#if defined (_18F66K80_FAMILY_)
    TBLPTR = flashBlock; //force row boundary
    TBLPTRU = 0;
#endif
#if defined (_18FXXQ83_FAMILY_)
    
#endif
    if (! flashFlags.writeNeeded) return;
    
    // Wait until App tells us it is a good time
    while (APP_isSuitableTimeToWriteFlash() == BAD_TIME)  // block awaiting a good time
        ;
        
    if (flashFlags.eraseNeeded) {
        eraseFlashBlock();
    }
    
    interruptEnabled = geti(); // store current global interrupt state
    bothDi();     // disable all interrupts ERRATA says this is needed before TBLWT
#if defined (_18F66K80_FAMILY_)
    for (uint8_t i=0; i<FLASH_PAGE_SIZE; i++) {
        TABLAT = flashBuffer[i];
        asm("TBLWT*+");
    }
    // Note from data sheet: 
    //   Before setting the WR bit, the Table
    //   Pointer address needs to be within the
    //   intended address range of the 64 bytes in
    //   the holding register.
    // So we put it back into the block here
    TBLPTR = flashBlock;
    TBLPTRU = 0;
    EECON1bits.EEPGD = 1;   // 1=Program memory, 0=EEPROM
    EECON1bits.CFGS = 0;    // 0=ProgramMemory/EEPROM, 1=ConfigBits
    EECON1bits.FREE = 0;    // No erase
    EECON1bits.WREN = 1;    // enable write to memory
    
    EECON2 = 0x55;
    EECON2 = 0xAA;
    EECON1bits.WR = 1;
    EECON1bits.WREN = 0;
#endif
#if defined(_18FXXQ83_FAMILY_)
    // ready?
    while (NVMCON0bits.GO)
        ;
    //Load NVMADR with the start address of the memory page
    /*NVMADRU = (uint8_t) (flashBlock >> 16);
    NVMADRH = (uint8_t) (flashBlock >> 8);
    NVMADRL = (uint8_t) flashBlock;*/
    NVMADR = flashBlock;

    NVMCON1bits.NVMCMD = NVMCMD_WRITEPAGE;      //Set the page write command
    //Perform the unlock sequence 
    NVMLOCK = 0x55;
    NVMLOCK = 0xAA;
    NVMCON0bits.GO = 1;             //Start byte write
    while (NVMCON0bits.GO)          // Wait to complete

        ;
    NVMCON1bits.NVMCMD = NVMCMD_NOP;      //Clear the NVM Command

#endif
    if (interruptEnabled) {     // Only enable interrupts if they were enabled at function entry
        bothEi();                   /* Enable Interrupts */
    }
    flashFlags.asByte = 0;  // no erase, no write
}

/**
 * Load an entire block of flash into the flash buffer.
 */
void loadFlashBlock(void) {
#if defined (_18F66K80_FAMILY_)
    EECON1=0X80;    // access to flash
    TBLPTR = flashBlock;
    TBLPTRU = 0;
    for (uint8_t i=0; i<64; i++) {
        asm("TBLRD*+");
        NOP();
        flashBuffer[i] = TABLAT;
    }
    TBLPTR = flashBlock;
    TBLPTRU = 0;
#endif
#if defined(_18FXXQ83_FAMILY_)
    // ready?
    while (NVMCON0bits.GO)
        ;
    //Load NVMADR with the starting address of the memory page
    NVMADRU = (uint8_t) (flashBlock >> 16);
    NVMADRH = (uint8_t) (flashBlock >> 8);
    NVMADRL = (uint8_t) flashBlock;
    NVMCON1bits.NVMCMD = NVMCMD_READPAGE;      //Set the page read command
    NVMCON0bits.GO = 1;             //Start page read
    while (NVMCON0bits.GO)          // Wait to complete
        ;
    NVMCON1bits.NVMCMD = NVMCMD_NOP;      //Clear the NVM Command
#endif
    flashFlags.asByte = 0; // no erase, no write needed
}
   
/**
 * Write a byte to Flash.
 * May block awaiting for the application to indicate that it is a suitable time
 * to allow the CPU to be halted.
 * @param index the address
 * @param value the value to be written
 * @return 0 for success or error otherwise
 */
uint8_t FLASH_Write(flash_address_t index, flash_data_t value) {
    uint8_t oldValue;
    
    /*
     * Writing flash is a bit of a pain as you must write in blocks. If you want to
     * write just 1 byte then you need ensure you don't change any other byte in the
     * block. To do this the block must be read into a buffer the byte changed 
     * and then the whole buffer written. Unfortunately this algorithm would cause
     * too many writes to happen and the flash would wear out.
     * Instead after reading the block and updating the byte we don't write the
     * buffer back in case there is another update within the same block. The 
     * block is only written back if another block needs to be updated.
     * Whilst writing back if any bit changes from 0 to 1 then the block needs
     * to be erased before writing.
     *
     */
    if (BLOCK(index) != flashBlock) {
        if (flashBlock != 0) {
            // ok we want to write a different block so flush the current block 
            if (flashFlags.eraseNeeded) {
                eraseFlashBlock();
                flashFlags.eraseNeeded = 0;
            }

            flushFlashBlock();
        }
        
        // and load the new one
        flashBlock = BLOCK(index);
        loadFlashBlock();
    }
    flashFlags.eraseNeeded |= (value & ~flashBuffer[OFFSET(index)])?1:0;
    if (flashBuffer[OFFSET(index)] != value) {
        flashFlags.writeNeeded = 1;
        flashBuffer[OFFSET(index)] = value;
    }
    return GRSP_OK;
}

/**
 * Write a single byte to NVM.
 * @param type the type of memory to access
 * @param index the memory address
 * @param value the value to be written
 * @return 0 for success, error otherwise
 */
uint8_t writeNVM(NVMtype type, uint24_t index, uint8_t value) {
    switch(type) {
        case EEPROM_NVM_TYPE:
            return EEPROM_Write((eeprom_address_t)index, value);
        case FLASH_NVM_TYPE:
            return FLASH_Write((flash_address_t)index, value);
        default:
            return GRSP_UNKNOWN_NVM_TYPE;
    }
}

/**
 * Read a single byte of NVM.
 * @param type the type of memory to be accessed
 * @param index the address
 * @return the value if >=0, -error otherwise
 */
int16_t readNVM(NVMtype type, uint24_t index) {
    switch(type) {
        case EEPROM_NVM_TYPE:
            return EEPROM_Read((uint16_t)index);
        case FLASH_NVM_TYPE:
#if defined(_18F66K80_FAMILY_)
            return FLASH_Read((uint16_t)index);
#endif
#if defined(_18FXXQ83_FAMILY_)
            return FLASH_Read(index);
#endif
        default:
            return -GRSP_UNKNOWN_NVM_TYPE;
    }
}


    
