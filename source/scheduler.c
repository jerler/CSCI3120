
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "scheduler.h"


int globalSequence = 0;			  		/* sequence number of next RCB */
//struct RequestControlBlock queue[RCB_QUEUE_SIZE];	/* holds all RCBs for the scheduler */
struct RequestControlBlock *firstRCB = NULL;		/* pointer to the first RCB in the queue */
int queueSize = 0;					/* number of RCBs in queue */

/*for testing only*/
/*extern void displayQueue(int n){
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
} */

void addRcbSjf(struct RequestControlBlock *rcb){
	if (firstRCB == NULL) {
		rcb->next = NULL;
		firstRCB = rcb;
		return;
	}	
	struct RequestControlBlock *prev;			
	prev = NULL;
	rcb->next = firstRCB;
	while((rcb->next != NULL)){
		if (rcb->next->lengthRemaining > rcb->lengthRemaining){
			if (prev == NULL){	/* add rcb to the front of the list */
				firstRCB = rcb;						
			}
			else {
				prev->next = rcb;
			}
			break;
		}
		else {			/* move to the next item in the list */
			prev = rcb->next;
			rcb->next = rcb->next->next;
		}				
	}
	if (rcb->next == NULL){		/* add rcb to the end of the list */
		prev->next = rcb;
	}
}

void addRcbRr(struct RequestControlBlock *rcb){
	if (firstRCB == NULL) {
		rcb->next = NULL;
		firstRCB = rcb;
		return;
	}

	struct RequestControlBlock *temp = firstRCB;
	/* Go through the queue to find the end 
	 * NOTE: We assume that the queue is non-empty. */
	while(temp->next != NULL){
		temp = temp->next;
	}
	temp->next = rcb;
	rcb->next = NULL;
}

extern int createRCB(int fd, FILE* fh, int sz, char* type){

	if (queueSize <= RCB_QUEUE_SIZE) {
		struct RequestControlBlock rcb;
		rcb.sequenceNumber = globalSequence++;
		rcb.fileDescriptor = fd;
		rcb.fileHandle = fh;
		rcb.lengthRemaining = sz;
		if (strcmp(type, "SJF") == 0){
			rcb.quantum = sz; 
		}
		else if (strcmp(type, "RR") == 0) {
			rcb.quantum = MAX_HTTP_SIZE;
		}

		/* Add RCB to queue */		
		if(strcmp(type, "SJF") == 0){	/*slot rcb into queue in SJF order */
			addRcbSjf(&rcb);
		}
		else if (strcmp(type, "RR") == 0){
			addRcbRr(&rcb);
		}		
		
		queueSize++;
		return 1;
	}
	return 0;					//queue was full
}

void removeRCB(struct RequestControlBlock *rcb){
	if (rcb == NULL) {
		perror("Tried to remove rcb but it doesn't exist.");
		return;
	}
	else {
		queueSize--;
		free(rcb);
	}
}

/* Note that we do not want to update queue size here. We need
 * to ensure that there is space for the job to rejoin the queue
 * if it does not complete. 
 */ 
extern struct RequestControlBlock* getNextJob(char* type){
	struct RequestControlBlock* rcb;
	/* SJF and RR only have one queue and the next job is at the front */
	if ((strcmp(type, "SJF") == 0) || (strcmp(type, "RR") == 0)){
		rcb = firstRCB;	/* Get the first job in the queue */
		if (rcb != NULL) {		
			firstRCB = rcb->next; 			/*Remove job from queue */
		}	
	}
	
	/* MLFB has to consider the possibility that the next job is in a different queue */	
	else if (strcmp(type, "MLFB") == 0){
	
	}

	return rcb; 
}

extern void updateRCB(char* type, int len, struct RequestControlBlock* rcb){
	rcb->lengthRemaining -= len;
	/* Regardless of scheduler type, and finished job is handled the same way */	
	if (rcb->lengthRemaining <= 0){
		printf("Request %d completed", rcb->sequenceNumber);
		fclose(rcb->fileHandle);
		close(rcb->fileDescriptor);			
		removeRCB(rcb);
	}
	else if (strcmp(type,"SJF") == 0){
		/* All jobs should complete in one pass for SJF */
		printf("Something went wrong processing %d with SJF.", rcb->sequenceNumber);
	}
	else if (strcmp(type, "RR") == 0){
		/* Update length and return to the end of the queue */
		rcb->lengthRemaining -= len;
		addRcbRr(rcb);
	}
}







