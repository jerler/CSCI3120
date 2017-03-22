/* 
 * File: sws.c
 * Author: Alex Brodsky
 * Purpose: This file contains the implementation of a simple web server.
 *          It consists of two functions: main() which contains the main 
 *          loop accept client connections, and serve_client(), which
 *          processes each client request.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>			   /* using fstat to get file size */

#include "network.h"
#include "scheduler.h"
#include "rcb.h"



char* schedType;			   /* the type of scheduler to use */

/* This function takes a file handle to a client, reads in the request, 
 *    parses the request, and sends back the requested file.  If the
 *    request is improper or the file is not available, the appropriate
 *    error is sent back.
 * Changes for project: Instead of sending back the file, it creates an
 * 	RCB block and adds it to the scheduler queue.
 * Parameters: 
 *             fd : the file descriptor to the client connection
 * Returns: None
 */
static void serve_client( int fd ) {
  static char *buffer;                              /* request buffer */
  char *req = NULL;                                 /* ptr to req file */
  char *brk;                                        /* state used by strtok */
  char *tmp;                                        /* error checking ptr */
  FILE *fin;                                        /* input file handle */
  int len;                                          /* length of data read */
  int sz;					    /* size of file */

  if( !buffer ) {                                   /* 1st time, alloc buffer */
    buffer = malloc( MAX_HTTP_SIZE );
    if( !buffer ) {                                 /* error check */
      perror( "Error while allocating memory" );
      abort();
    }
  }

  memset( buffer, 0, MAX_HTTP_SIZE );
  if( read( fd, buffer, MAX_HTTP_SIZE ) <= 0 ) {    /* read req from client */
    perror( "Error while reading request" );
    abort();
  } 

  /* standard requests are of the form
   *   GET /foo/bar/qux.html HTTP/1.1
   * We want the second token (the file path).
   */
  tmp = strtok_r( buffer, " ", &brk );              /* parse request */
  if( tmp && !strcmp( "GET", tmp ) ) {
    req = strtok_r( NULL, " ", &brk );
  }
 
  if( !req ) {                                      /* is req valid? */
    len = sprintf( buffer, "HTTP/1.1 400 Bad request\n\n" );
    write( fd, buffer, len );                       /* if not, send err */
  } else {                                          /* if so, open file */
    req++;                                          /* skip leading / */
    fin = fopen( req, "r" );                        /* open file */
    if( !fin ) {                                    /* check if successful */
      len = sprintf( buffer, "HTTP/1.1 404 File not found\n\n" );  
      write( fd, buffer, len );                     /* if not, send err */
      fclose( fin);
    }
    else {                                        /* if so, add file to queue */
    /* Determine size of file
     * Allocate and initialize a request control block
     * Add RCB to queue
     * Send back response status */
      struct stat st;
      fstat(fd, &st);				     /* get stats of file from file descriptor */
      sz = st.st_size;				     /* extract file size in bytes from stats */
      
      createRCB(fd, fin, sz, schedType);	     /* create RCB and add it to queue */

      len = sprintf( buffer, "HTTP/1.1 200 OK\n\n" );/* send success code */
      write( fd, buffer, len );

    }
  }

}

static int processNextJob(){
	static char* buffer;
	int len, maxRead;
	int totalLen = 0;
	struct RequestControlBlock* rcb = getNextJob(schedType);
	if(rcb->sequenceNumber == -1){	/*No more jobs to process*/
		free(rcb);
		return 0;
	}

	if( !buffer ) {                                   /* 1st time, alloc buffer */
    		buffer = malloc( MAX_HTTP_SIZE );
    		if( !buffer ) {                                 /* error check */
      			perror( "Error while allocating memory" );
			return 1;
    		}
	}

  	memset( buffer, 0, MAX_HTTP_SIZE );

	do {                                          /* loop, read & send file */
		/*set maximum number of bytes to read for this pass*/
		if ((rcb->quantum - totalLen) < MAX_HTTP_SIZE){
			maxRead = rcb->quantum - totalLen;
		}
		else {
			maxRead = MAX_HTTP_SIZE;
		}
		len = fread( buffer, 1, maxRead, rcb->fileHandle );  /* read file chunk */
		if( len < 0 ) {                             /* check for errors */
			perror( "Error while writing to client" );
		} else if( len > 0 ) {                      /* if none, send chunk */
			len = write( rcb->fileDescriptor, buffer, len );
			if( len < 1 ) {                           /* check for errors */
				perror( "Error while writing to client" );
			}
		}
		totalLen += len;
      	} while( (len == MAX_HTTP_SIZE) && (totalLen < rcb->quantum) );         /* the last chunk < 8192 */
      	updateRCB(schedType, totalLen, rcb);	/*scheduler handles rcb from here*/
      	return 1;
}




/* This function is where the program starts running.
 *    The function first parses its command line parameters to determine port #
 *    Then, it initializes, the network and enters the main loop.
 *    The main loop waits for a client (1 or more to connect, and then processes
 *    all clients by calling the seve_client() function for each one.
 * Parameters: 
 *             argc : number of command line parameters (including program name
 *             argv : array of pointers to command line parameters
 * Returns: an integer status code, 0 for success, something else for error.
 */
int main( int argc, char **argv ) {
  int port = -1;                                    /* server port # */
  int fd;                                           /* client file descriptor */

  /* check for and process parameters 
   * port number and scheduler
   */
  if( ( argc < 3 ) || ( sscanf( argv[1], "%d", &port ) < 1 )) {
    printf( "usage: sms <port> <scheduler>\n" );
    return 0;
  }
  schedType = argv[2];
 
  /*for testing*/
  if(strcmp(schedType, "test") == 0){
	
	return 0;
  }
  if(strcmp(schedType, "SJF") != 0){
    printf( "usage: schedule type must be SJF, or else 'test' for testing\n" );
    return 0;
  }   

  network_init( port );                             /* init network module */

  for( ;; ) {                                       /* main loop */
    network_wait();                                 /* wait for clients */

    for( fd = network_open(); fd >= 0; fd = network_open() ) { /* get clients */
      serve_client( fd );                           /* process each client */
    }
    while(processNextJob());			    /* process the rcbs in the queue */
    
  }
}
