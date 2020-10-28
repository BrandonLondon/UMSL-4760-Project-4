#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/ipc.h> 
#include <sys/shm.h> 
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/msg.h>
#include "queue.h"
#include "shared.h"
#include "string.h"

/* Constants for termination and using all time--the reason termination is not const is because it changes depending if it is a realtime proccess or not */
int CHANCE_TO_DIE_PERCENT = 10;
const int CHANCE_TO_USE_ALL_TIME_PERCENT = 90;

/* Housekeeping holders for shared memory and file name alias */
Shared* data;
int toChildQueue;
int toMasterQueue;
int ipcid;
char* filen;

/* Function prototypes */
void ShmAttatch();
void QueueAttatch();
void AddTime(Time* time, int amount);
void AddTimeSpec(Time* time, int sec, int nano);
int FindPID(int pid);

/* Message queue standard message buffer */
struct {
	long mtype;
	char mtext[100];
} msgbuf;

/* Find the proccess block with the given pid and return the position in the array */
int FindPID(int pid)
{
	int i;
	for (i = 0; i < MAX_PROCS; i++)
		if (data->proc[i].pid == pid)
			return i;
	return -1;
}

/* Add time to given time structure, max 2.147billion ns */
void AddTime(Time* time, int amount)
{
	int newnano = time->ns + amount;
	while (newnano >= 1000000000) //nano = 10^9, so keep dividing until we get to something less and increment seconds
	{
		newnano -= 1000000000;
		(time->seconds)++;
	}
	time->ns = newnano; //since ns is < 10^9, it is our new nanoseconds
}

/* Used to specifically add x seconds and y nanoseconds */
void AddTimeSpec(Time* time, int sec, int nano)
{
	time->seconds += sec;
	AddTime(time, nano);
}

/* Attach to queues incoming/outgoing */
void QueueAttatch()
{
	key_t shmkey = ftok("shmsharemsg", 766);

	if (shmkey == -1) //check if the input file exists
	{
		printf("\n%s: ", filen);
		fflush(stdout);
		perror("./oss: Error: Ftok failed");
		return;
	}

	toChildQueue = msgget(shmkey, 0600 | IPC_CREAT); //attach to child queue

	if (toChildQueue == -1)
	{
		printf("\n%s: ", filen);
		fflush(stdout);
		perror("./oss: Error: toChildQueue creation failed");
		return;
	}

	shmkey = ftok("shmsharemsg2", 767);

	if (shmkey == -1) //check if the input file exists
	{
		printf("\n%s: ", filen);
		fflush(stdout);
		perror("./oss: Error: Ftok failed");
		return;
	}

	toMasterQueue = msgget(shmkey, 0600 | IPC_CREAT); //attach to master queue

	if (toMasterQueue == -1)
	{
		printf("\n%s: ", filen);
		fflush(stdout);
		perror("./oss: Error: toMasterQueue creation failed");
		return;
	}
}

/* Attaches to shared memory */
void ShmAttatch() //same exact memory attach function from master minus the init for the semaphores
{
	key_t shmkey = ftok("shmshare", 312); //shared mem key

	if (shmkey == -1) //check if the input file exists
	{
		printf("\n%s: ", filen);
		fflush(stdout);
		perror("./oss: Error: Ftok failed");
		return;
	}

	ipcid = shmget(shmkey, sizeof(Shared), 0600 | IPC_CREAT); //get shared mem

	if (ipcid == -1) //check if the input file exists
	{
		printf("\n%s: ", filen);
		fflush(stdout);
		perror("./oss: Error: failed to get shared memory");
		return;
	}

	data = (Shared*)shmat(ipcid, (void*)0, 0); //attach to shared mem

	if (data == (void*)-1) //check if the input file exists
	{
		printf("\n%s: ", filen);
		fflush(stdout);
		perror("./oss: Error: Failed to attach to shared memory");
		return;
	}
}

int main(int argc, int argv)
{
	ShmAttatch(); //attach to shared mem
	QueueAttatch(); //attach to queues

	int pid = getpid(); //shorthand for getpid every time from now

	CHANCE_TO_DIE_PERCENT = (data->proc[FindPID(pid)].realtime == 1) ? CHANCE_TO_DIE_PERCENT * 2 : CHANCE_TO_DIE_PERCENT; //if realtime proccess, set chance to die higher

	/* Variables to keep tabs on time to be added instead of creating new ints every time */
	int secstoadd = 0;
	int mstoadd = 0;
	int runningIO = 0;
	Time unblockTime;

	srand(time(NULL) ^ (pid << 16)); //ensure randomness by bitshifting and ORing the time based on the pid

	while (1)
	{
		msgrcv(toChildQueue, &msgbuf, sizeof(msgbuf), pid, 0); //get blocked here every time the master asks us to make a decision until unblocked

		if ((rand() % 100) <= CHANCE_TO_DIE_PERCENT && runningIO == 0) //roll for termination
		{
			msgbuf.mtype = pid;
			strcpy(msgbuf.mtext, "USED_TERM");
			msgsnd(toMasterQueue, &msgbuf, sizeof(msgbuf), 0); //send parent termination signal

			int rngTimeUsed = (rand() % 99) + 1;
			char* convert[15];
			sprintf(convert, "%i", rngTimeUsed);

			msgbuf.mtype = pid;
			strcpy(msgbuf.mtext, convert);
			msgsnd(toMasterQueue, &msgbuf, sizeof(msgbuf), 0); //after calculating how much time we used, send the percent to parent and die

			exit(21);
		}


		if ((rand() % 100) <= CHANCE_TO_USE_ALL_TIME_PERCENT) //roll to use all time and send result to parent if using all
		{
			msgbuf.mtype = pid;
			strcpy(msgbuf.mtext, "USED_ALL");
			msgsnd(toMasterQueue, &msgbuf, sizeof(msgbuf), 0); //send used all signal to parent
		}
		else
		{
			if (runningIO == 0) //this was here before I changed back from async. Think of this as a dinosaur now. 
			{
				/* determine unblock time for the proccess and add it to current time to determine when to stop spinlocking */
				unblockTime.seconds = data->sysTime.seconds;
				unblockTime.ns = data->sysTime.ns;
				secstoadd = rand() % 6;
				mstoadd = (rand() % 1001) * 1000000;
				runningIO = 1;

				AddTimeSpec(&unblockTime, secstoadd, mstoadd); //set unblock time to some value seconds value 0-5 and 0-1000ms but converted to ns to make my life easier
				AddTimeSpec(&(data->proc[FindPID(pid)].tBlockedTime), secstoadd, mstoadd); //add blocked time to the proccess statistics in proc block 

				int rngTimeUsed = (rand() % 99) + 1;
				char* convert[15];
				sprintf(convert, "%i", rngTimeUsed);

				msgbuf.mtype = pid;
				strcpy(msgbuf.mtext, "USED_PART");
				msgsnd(toMasterQueue, &msgbuf, sizeof(msgbuf), IPC_NOWAIT); //send that we are going to block ourselves

				msgbuf.mtype = pid;
				strcpy(msgbuf.mtext, convert);
				fflush(stdout);
				msgsnd(toMasterQueue, &msgbuf, sizeof(msgbuf), 0); //send time used before block to parent


				while (1) //spinlock until our unblock time is here
				{
					if (data->sysTime.seconds >= unblockTime.seconds && data->sysTime.ns >= unblockTime.ns) //check current time against the unblock time
						break;
				}

				msgbuf.mtype = pid;
				strcpy(msgbuf.mtext, "USED_IO_DONE");
				msgsnd(toMasterQueue, &msgbuf, sizeof(msgbuf), IPC_NOWAIT); //send that our IO operation has finished

				runningIO = 0;
			}
		}
	}
}
