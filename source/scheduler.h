#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdio.h>

#define MAX_HTTP_SIZE 8192              /* size of buffer to allocate */
#define RCB_QUEUE_SIZE 64		/* max number of request control blocks in queue */

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

/* This function resets an RCB to default values
 * It also handles closing the file stream if it is still open.
 */ 
extern void removeRCB(int index);


/* This function will grab the next RCB (based on the scheduling type
 * input parameter), then read the file until the quantum is reached.
 * For SJF: 
 * 	- The job with the shortest length remaining is accessed
 * 	- The file is read and passed to the client
 * 	- The RCB is removed from the queue. 
 * The function returns 1 if successful, or 0 if there is an error.
 */
extern int processRCB(char* type);



#endif
