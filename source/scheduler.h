#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdio.h>
#include "rcb.h"


#define RCB_QUEUE_SIZE 	64
#define MAX_HTTP_SIZE 	8192            /* size of buffer to allocate */
#define EIGHT_KB	8192		/* size of RR and high priority MLFB quantums */  
#define SIXTY_FOUR_KB	65536		/* size of medium and low priority MLFB quantums */


extern int globalSequence;		/* The sequence number given to the next RCB */

/* This function is for testing only.
 * It currently prints out the sequence numbers of the first n RCBs,
 * but feel free to change this to suit your needs 
 */
extern void displayQueue(int n);

/* This function initializes the sequence numbers of the RCBs in the 
 * queues to -1. This allows for easily finding available spots. 
 */
extern void initializeQueue();

/* This function finds the first empty slot in the queue, creates
 * an RCB and adds it to the queue.
 * If no spots are available, the function returns 0. Otherwise it returns 1. 
 */
extern int createRCB(int fd, FILE* fh, int sz, char* type);

/* This function resets an RCB to default values to make it available
 */ 
void removeRCB(struct RequestControlBlock *rcb);

/* This function will grab the next RCB (based on the scheduling type
 * input parameter).
 * It will return a pointer to the rcb and set the lock value to 1 so
 * that it will not be grabbed again
 */
extern struct RequestControlBlock* getNextJob(char* type);

/* This function will update or remove the RCB after processing, based on scheduling type.
 * It will subtract len from the lengthRemaining. 
 * If there are still bytes to send the rcb will be unlocked (added back to the queue).
 * Otherwise the rbc will be removed and the connection and file will be closed. 
 */
extern void updateRCB(char* type, int len, struct RequestControlBlock* rcb);



#endif
