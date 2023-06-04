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
 * @file
 * Implementation of message queues used for receive and transmit buffers.
 */
#include <xc.h>
#include "vlcb.h"
#include "queue.h"

/**
 * Push a message onto the message queue.
 * @param q the queue
 * @param m the message
 * @return QUEUE_SUCCESS for success QUEUE_FAIL for buffer full
 */
Qresult push(Queue * q, Message * m) {
    if (((q->writeIndex+1)&((q->size)-1)) == q->readIndex) return QUEUE_FAIL;	// buffer full
    (q->messages[q->writeIndex]).opc = m->opc;
    (q->messages[q->writeIndex]).bytes[0] = m->bytes[0];
    (q->messages[q->writeIndex]).bytes[1] = m->bytes[1];
    (q->messages[q->writeIndex]).bytes[2] = m->bytes[2];
    (q->messages[q->writeIndex]).bytes[3] = m->bytes[3];
    (q->messages[q->writeIndex]).bytes[4] = m->bytes[4];
    (q->messages[q->writeIndex]).bytes[5] = m->bytes[5];
    (q->messages[q->writeIndex]).bytes[6] = m->bytes[6];
    (q->messages[q->writeIndex]).len = m->len;
    q->writeIndex++;
    
    if (q->writeIndex >= q->size) q->writeIndex = 0;
    return QUEUE_SUCCESS;
}
/**
 * A bit like a pop but doesn't copy the message and instead returns a pointer to
 * the buffer to which the message can be copied by the caller.
 * @param q the queue
 * @return a message pointer
 */
Message * getNextWriteMessage(Queue * q) {
    uint8_t wr;
    if (((q->writeIndex+1)&((q->size)-1)) == q->readIndex) return NULL;	// buffer full
    wr = q->writeIndex;
    q->writeIndex++;
    if (q->writeIndex >= q->size) q->writeIndex = 0;
    return &(q->messages[wr]);
}


/**
 * Pull the next message from the queue.
 *
 * @param q the queue
 * @return the next message
 */
Message * pop(Queue * q) {
    Message * ret;
	if (q->writeIndex == q->readIndex) {
        return NULL;	// buffer empty
    }
	ret = &(q->messages[q->readIndex++]);
	if (q->readIndex >= q->size) q->readIndex = 0;
	return ret;
}

/**
 * Peek into the buffer.
 *
 * @param q the queue
 * @param index the position into the queue
 * @return the message
 */
Message * peek(Queue * q, unsigned char index) {
    if (q->readIndex == q->writeIndex) return NULL;    // empty
    index += q->readIndex;
//    index -= 1;
    if (index >= q->size) {
        index -= q->size;
    }
    if (index == q->writeIndex) return NULL; // at end
    return &(q->messages[index]);
}


/**
 * Return number of items in the queue.
 * @param q the queue
 * @return the number of items
 */
unsigned char quantity(Queue * q) {
    return (q->writeIndex - q->readIndex) & (q->size -1);
}

