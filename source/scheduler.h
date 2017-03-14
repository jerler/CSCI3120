#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdio.h>



extern int globalSequence;

extern void displayQueue(int n);

extern void initializeQueue();

extern int createRCB(int fd, FILE* fh, int sz, char* type);

extern void addRCB();



#endif
