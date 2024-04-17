#ifndef _HARDWARE_H_
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
#define _HARDWARE_H_


/**
 *  How to determine whether interrupts are enabled
 */
#if defined(_18F66K80_FAMILY_)
#define geti()  (INTCONbits.GIEH)
#define bothEi()    {INTCONbits.GIEH = 1; INTCONbits.GIEL = 1;}
#define bothDi()    {INTCONbits.GIEH = 0; INTCONbits.GIEL = 0;}
#endif
#if defined(_18FXXQ83_FAMILY_)
#define geti()  (INTCON0bits.GIE)
#define bothEi()    {INTCON0bits.GIE = 1;}
#define bothDi()    {INTCON0bits.GIE = 0;}
#endif

#endif