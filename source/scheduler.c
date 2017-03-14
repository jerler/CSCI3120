
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "scheduler.h"




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

extern void removeRCB(int index){
	queue[index].sequenceNumber = -1;
	queue[index].fileDescriptor = -1;
	if (queue[index].fileHandle) {
		fclose( queue[index].fileHandle );
	}
	queue[index].fileHandle = NULL;
	queue[index].lengthRemaining = 0;
	queue[index].quantum = 0;
}


/* A lot of code in here was borrowed from serve_client code and might not be the most efficient*/
extern int processNextRCB(char* type){
	static char* buffer;
	int len;

	if( !buffer ) {                                   /* 1st time, alloc buffer */
    		buffer = malloc( MAX_HTTP_SIZE );
    		if( !buffer ) {                                 /* error check */
      			perror( "Error while allocating memory" );
			return 0;
    		}
	}

  	memset( buffer, 0, MAX_HTTP_SIZE );

	if (strcmp(type, "SJF") == 0){
		/*Get the index of the shortest job*/
		int i, sz;
		int index = -1;
		for (i = 0; i < RCB_QUEUE_SIZE; i++){
			if (queue[i].sequenceNumber != -1){
				if((index == -1) || (queue[i].lengthRemaining < sz)){
					index = i;
					sz = queue[i].lengthRemaining;
				}
			}
		}
		
		/* Process shortest job
		 * Note: we may want to modify this bit to work with all algorithms, based on quantum*/
		do {                                          /* loop, read & send file */
	        	len = fread( buffer, 1, MAX_HTTP_SIZE, queue[index].fileHandle);  /* read file chunk */
			if( len < 0 ) {                             /* check for errors */
				perror( "Error while writing to client" );
			} else if( len > 0 ) {                      /* if none, send chunk */
				len = write( queue[index].fileDescriptor, buffer, len );
				if( len < 1 ) {                           /* check for errors */
		    			perror( "Error while writing to client" );
		  		}
			}
		} while( len == MAX_HTTP_SIZE );              /* the last chunk < 8192 */

		/*Remove shortest job*/
		removeRCB(index);

		return 1;
	}

	return 0;

}



