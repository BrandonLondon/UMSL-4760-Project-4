#ifndef SHARED_H

#include <semaphore.h>

/* Pre-processor for how many max procs we can have in the system */
#define MAX_PROCS 19

/* Time structure */
typedef struct {
	unsigned int seconds;
	unsigned int ns;
} Time;

/* Proccess block structure */
typedef struct {
	int realtime;       //flag if process is realtime or not
	int queueID;        //flag for what queue the process is in
	int pid;            //real pid
	int loc_pid;        //fake local pid
	Time tCpuTime;      //cpu time used statistic
	Time tSysTime;      //time in system statistic 
	Time tBlockedTime;  //time blocked statistic 
	Time tWaitTime; 	//time waiting statistic
} ProcBlock;

/* Proccess table and sys time shared block */
typedef struct {
	ProcBlock proc[MAX_PROCS]; //process table
	Time sysTime; //system clock time
	int childcount;
} Shared;

#define SHARED_H
#endif