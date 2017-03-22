
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "scheduler.h"


int globalSequence = 0;			  		/* sequence number of next RCB */
//struct RequestControlBlock queue[RCB_QUEUE_SIZE];	/* holds all RCBs for the scheduler */
int queueSize = 0;					/* number of RCBs in queue */
struct RequestControlBlock *firstRcb = NULL;		/* pointer to the first RCB in the queue */

/*The following two pointers are only used with MLFB scheduler */
struct RequestControlBlock *firstRcb64 = NULL;	/* pointer to the first RCB in the medium priority queue */
struct RequestControlBlock *firstRcbRr = NULL; 	/* pointer to the first RCB in the low priority queue */

/*for testing only*/
extern void displayQueue(int n){
	int i = 0;
	struct RequestControlBlock *rcb  = firstRcb;
	while(rcb != NULL){
		printf("%d: %d\n", i, rcb->sequenceNumber);
		rcb = rcb->next;
		i++;		
	} 
}

/* This function adds an RCB into the queue in order by length remaining, 
 * where the job with the shortest length remaining is at the front of the queue 
 */
void addRcbSjf(struct RequestControlBlock *rcb){
	if (firstRcb == NULL) {
		rcb->next = NULL;
		firstRcb = rcb;
		return;
	}	
	struct RequestControlBlock *prev;			
	prev = NULL;
	rcb->next = firstRcb;
	while((rcb->next != NULL)){
		if (rcb->next->lengthRemaining > rcb->lengthRemaining){
			if (prev == NULL){	/* add rcb to the front of the list */
				firstRcb = rcb;						
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

/* This funciton adds an RCB to the end of a queue. This function
 * takes in a pointer to the first element of the queue in order to support MLFB 
 */
void addRcbToEnd(struct RequestControlBlock *rcb, struct RequestControlBlock *first){
	if (firstRcb == NULL) {
		rcb->next = NULL;
		firstRcb = rcb;
		return;
	}

	struct RequestControlBlock *temp = firstRcb;
	/* Go through the queue to find the end  */
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

		/* Add RCB to queue */		
		if(strcmp(type, "SJF") == 0){	/*slot rcb into queue in SJF order */
			rcb.quantum = sz;
			addRcbSjf(&rcb);
		}
		/* RR and MLFB handle new RCBs the same way */
		else if ((strcmp(type, "RR") == 0) || (strcmp(type, "MLFB") == 0)){
			rcb.quantum = EIGHT_KB;
			addRcbToEnd(&rcb, firstRcb);
		}
		else {
			perror("Invalid scheduler type");
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
		rcb = firstRcb;	/* Get the first job in the queue */
		if (rcb != NULL) {		
			firstRcb = rcb->next; 			/*Remove job from queue */
		}	
	}
	
	/* MLFB has to consider the possibility that the next job is in a different queue */	
	else if (strcmp(type, "MLFB") == 0){
		rcb = NULL;
		if (firstRcb != NULL) {			/* Try high priority queue first */
			rcb = firstRcb;			
			firstRcb = rcb->next;
		}
		else if (firstRcb64 != NULL) {		/* Move to medium priority queue */
			rcb = firstRcb64;
			firstRcb64 = rcb->next;
		}
		else if (firstRcbRr != NULL) {		/* Move to low priority queue */
			rcb = firstRcbRr;
			firstRcbRr = rcb->next;
		}
	}

	else {
		perror("Invalid scheduler type");
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
		/* Rturn to the end of the queue */
		addRcbToEnd(rcb, firstRcb);
	}
	else if (strcmp(type, "MLFB") == 0){
		if (rcb->quantum == EIGHT_KB) { 	/* Demote to medium priority queue */
			rcb->quantum = SIXTY_FOUR_KB;
			addRcbToEnd(rcb, firstRcb64);			
		}
		else {				/* Put in low priority queue */
			addRcbToEnd(rcb, firstRcbRr);
		}
	}
	else {
		perror("Invalid scheduler type");
	}
}







