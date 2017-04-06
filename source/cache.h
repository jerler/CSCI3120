/*
 * cache.h
 *
 *  Created on: Mar 21, 2017
 *      Author: julie
 */

/*
 * Initializes; returns nothing
 */
void cache_init(int size);

/*
 * Returns -1 if error, else returns the ID number of the CFD
 */
int cache_open(char *file);

/*
 * Returns the number of bytes sent or -1 if fail
 */
int cache_send(int cfd, int client, int n);

/*
 * Returns the size of the file or -1 if fail
 */
int cache_filesize(int cfd);

/*
 * Return 0 if success, -1 if fail
 */
int cache_close(int cfd);

/*
 * For test purposes only
 */
void cache_destroy();
