
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "scheduler.h"

#define RCB_QUEUE_SIZE 64		   /* max number of request control blocks in queue */

struct RequestControlBlock {
	int sequenceNumber;
	int fileDescriptor;
	FILE* fileHandle;
	int lengthRemaining;
	int quantum;
}; 



int globalSequence = 0;			  		/* sequence number of next RCB */
struct RequestControlBlock queue[RCB_QUEUE_SIZE];	/* holds all RCBs for the scheduler */

/*for testing only*/
extern void displayQueue(int n){
	int i;
	for(i = 0; i < n; i++){
		printf("%d: %d\n", i, queue[i].sequenceNumber);
	}
}

extern void initializeQueue(){
	int i;
	for(i = 0; i < RCB_QUEUE_SIZE; i++){
		queue[i].sequenceNumber = -1;
	}
}

extern int createRCB(int fd, FILE* fh, int sz, char* type){
	struct RequestControlBlock rcb;
	int index;
	for (index = 0; index < RCB_QUEUE_SIZE; index++){
		if(queue[index].sequenceNumber == -1){
			break;				//we have found the first free spot
		}
	}
	if (index < RCB_QUEUE_SIZE) {
		rcb.sequenceNumber = globalSequence++;
		rcb.fileDescriptor = fd;
		rcb.fileHandle = fh;
		rcb.lengthRemaining = sz;
		if (strcmp(type, "SJF") == 0){
			rcb.quantum = sz; 
		}
		queue[index] = rcb; 			//add rcb to queue
		return 1;
	}
	return 0;					//queue was full

}

