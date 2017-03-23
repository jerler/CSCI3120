#ifndef RCB_H
#define RCB_H


struct RequestControlBlock {
	struct RequestControlBlock *next;	/*The next rcb in the queue*/
	int sequenceNumber;
	int fileDescriptor;
	FILE* fileHandle;
	int lengthRemaining;
	int quantum;
}; 

#endif
