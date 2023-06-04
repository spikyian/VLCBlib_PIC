#ifndef _QUEUE_H_
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
#define _QUEUE_H_

/**
 * @file
 * Implementation of message queues used for receive and transmit buffers.
 */

typedef struct Queue {
    Message * messages;
    uint8_t readIndex;
    uint8_t writeIndex;
    uint8_t size;
} Queue;

typedef enum Qresult {
    QUEUE_FAIL=0,
    QUEUE_SUCCESS=1
} Qresult;


uint8_t quantity(Queue * q);
Qresult push(Queue * q, Message * m);
Message * pop(Queue * q);
extern Message * getNextWriteMessage(Queue * q);

#endif
