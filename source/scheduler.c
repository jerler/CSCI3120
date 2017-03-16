
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "scheduler.h"





/*Sequence numbers: 
 * empty spots have a sequenceNumber of -1 */
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
		rcb.lock = 0;
		queue[index] = rcb; 			//add rcb to queue
		return 1;
	}
	return 0;					//queue was full
}

void removeRCB(int sequenceNumber){
	int index = -1;
	int i;
	/*Find rcb*/
	for (i = 0; i < RCB_QUEUE_SIZE; i++){
		if(queue[i].sequenceNumber == sequenceNumber){
			index = i;
			break;
		}
	}
	if (index == -1) {
		perror("Tried to remove rcb but it doesn't exist.");
		return;
	}
	queue[index].sequenceNumber = -1;
	queue[index].fileDescriptor = -1;
	queue[index].fileHandle = NULL;
	queue[index].lengthRemaining = 0;
	queue[index].quantum = 0;
	queue[index].lock = 0;
}

extern struct RequestControlBlock* getNextJob(char* type){
	struct RequestControlBlock* rcb;
	if(strcmp(type, "SJF") == 0){
		/*Get the shortest job*/
		int i, sz;
		int index = -1;

		for (i = 0; i < RCB_QUEUE_SIZE; i++){
			if ((queue[i].sequenceNumber > -1) && (queue[i].lock == 0)){
				if((index == -1) || (queue[i].lengthRemaining < sz)){
					index = i;
					sz = queue[i].lengthRemaining;
				}
			}
		}
		if (index == -1) {	//no more jobs available!
			struct RequestControlBlock empty;
			empty.sequenceNumber = -1;
			rcb = &empty;
		}
		else {
			queue[index].lock = 1; 	/*lock rcb so it cannot be grabbed by another thread*/
			rcb = &queue[index];
		}	
	

	}
	return rcb;
}

extern void updateRCB(char* type, int len, struct RequestControlBlock* rcb){
	rcb->lengthRemaining -= len;
	if (strcmp(type,"SJF") == 0){
		if (rcb->lengthRemaining <= 0){
			printf("Request %d completed", rcb->sequenceNumber);
			fclose(rcb->fileHandle);
			close(rcb->fileDescriptor);			
			removeRCB(rcb->sequenceNumber);
		}
		else {
			rcb->lock = 0; 	/*Unlock file*/
			printf("Something went wrong processing %d with SJF.", rcb->sequenceNumber);
		}

	}

}




