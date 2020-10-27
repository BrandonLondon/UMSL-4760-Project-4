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

int ipcid; //inter proccess shared memory
Shared* data; //shared memory data
int toChildQueue; //queue for communicating to child from master
int toMasterQueue; //queue for communicating from child to master
int locpidcnt = 0; //keeps track of current local PID for new proccesses
char* filen; //name of this executable
int childCount = 19; //Max children concurrent

FILE* o;  //output log file pointer

/* Consts for time between proccesses, and how much the scheduler should advance the time on every iteration */
const int MAX_TIME_BETWEEN_NEW_PROCS_NS = 150000;
const int MAX_TIME_BETWEEN_NEW_PROCS_SEC = 1;
const int SCHEDULER_CLOCK_ADD_INC = 10000;

/* Queue and Realtime constants */
const int CHANCE_TO_BE_REALTIME = 80;
const int QUEUE_BASE_TIME = 10; //in ms

/* Create prototypes for used functions*/
void Handler(int signal);
void DoFork(int value);
void ShmAttatch();
void TimerHandler(int sig);
int SetupInterrupt();
int SetupTimer();
void DoSharedWork();
int FindEmptyProcBlock();
void SweepProcBlocks();
void AddTimeSpec(Time* time, int sec, int nano);
void AddTime(Time* time, int amount);
int FindPID(int pid);
int FindLocPID(int pid);
void QueueAttatch();
void AddTimeLong(Time* time, long amount);
void SubTime(Time* time, Time* time2);
void SubTimeOutput(Time* time, Time* time2, Time* out);

/* Message queue standard message buffer */
struct {
	long mtype;
	char mtext[100];
} msgbuf;

/* Find average time and overwrite the passed time structure with the averages */
void AverageTime(Time* time, int count)
{
	long timeEpoch = (((long)(time->seconds) * (long)1000000000) + (long)(time->ns))/count; //calculate epoch of total time

	Time temp = {0, 0};
	AddTimeLong(&temp, timeEpoch); //convert epoch string to time structure

	time->seconds = temp.seconds;
	time->ns = temp.ns;
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
	time->ns = newnano; //since newnano is now < 1 billion, it is less than second. Assign it to ns
}

/* Used to specifically add x seconds and y nanoseconds */
void AddTimeSpec(Time* time, int sec, int nano)
{
	time->seconds += sec;
	AddTime(time, nano);
}

/* Add more than 2.147 billion nanoseconds to the time */
void AddTimeLong(Time* time, long amount)
{
	long newnano = time->ns + amount;
	while (newnano >= 1000000000) //nano = 10^9, so keep dividing until we get to something less and increment seconds
	{
		newnano -= 1000000000;
		(time->seconds)++;
	}
	time->ns = (int)newnano; //since newnano is now < 1 billion, it is less than second. Assign it to ns
}

/* Subtract time by computing the epoch of 2 times, then substracing and converting back to a time structure. Overwrites first param */
void SubTime(Time* time, Time* time2)
{
	long epochTime1 = (time->seconds * 1000000000) + (time->ns); //Since 1 billion ns in a second, multiple seconds by 10000000000
	long epochTime2 = (time2->seconds * 1000000000) + (time2->ns);

	long epochTimeDiff = abs(epochTime1 - epochTime2);

	Time temp;
	temp.seconds = 0;
	temp.ns = 0;

	AddTimeLong(&temp, epochTimeDiff); //convert epoch difference to a time structure.

	time->seconds = temp.seconds;
	time->ns = temp.ns;
}

/* Does the same as SubTime but instead overwrites the third parameter instead of the first */
void SubTimeOutput(Time* time, Time* time2, Time* out)
{
	long epochTime1 = (time->seconds * 1000000000) + (time->ns); //Since 1 billion ns in a second, multiple seconds by 10000000000
	long epochTime2 = (time2->seconds * 1000000000) + (time2->ns);

	long epochTimeDiff = abs(epochTime1 - epochTime2);

	Time temp;
	temp.seconds = 0;
	temp.ns = 0;

	AddTimeLong(&temp, epochTimeDiff); //convert epoch difference to a time structure.

	out->seconds = temp.seconds;
	out->ns = temp.ns;
}

/* handle ctrl-c and timer hit */
void Handler(int signal) 
{
	fflush(stdout); //make sure that messages are output correctly before we start terminating things

	int i;
	for (i = 0; i < childCount; i++) //loop thorough the proccess table and issue a termination signal to all unkilled proccess/children
		if (data->proc[i].pid != -1)
			kill(data->proc[i].pid, SIGTERM);

	fflush(o); //flush out the output file
	fclose(o); //close output file
	shmctl(ipcid, IPC_RMID, NULL); //free shared mem
	msgctl(toChildQueue, IPC_RMID, NULL); //free queues
	msgctl(toMasterQueue, IPC_RMID, NULL);

	printf("%s: Termination signal caught. Killed processes and killing self now...goodbye...\n\n", filen);

	kill(getpid(), SIGTERM); //kill self
}

/* Perform a forking call to launch a user proccess */
void DoFork(int value) //do fun fork stuff here. I know, very useful comment.
{
	char* forkarg[] = { //null terminated args set
			"./user",
			NULL
	}; //null terminated parameter array of chars

	execv(forkarg[0], forkarg); //exec
	Handler(1);
}

/* Attaches to shared memory */
void ShmAttatch() //attach to shared memory
{
	key_t shmkey = ftok("shmshare", 312); //shared mem key

	if (shmkey == -1) //check if the input file exists
	{
		fflush(stdout);
		perror("Error: Ftok failed");
		return;
	}

	ipcid = shmget(shmkey, sizeof(Shared), 0600 | IPC_CREAT); //get shared mem

	if (ipcid == -1) //check if the input file exists
	{
		fflush(stdout);
		perror("Error: failed to get shared memory");
		return;
	}

	data = (Shared*)shmat(ipcid, (void*)0, 0); //attach to shared mem

	if (data == (void*)-1) //check if the input file exists
	{
		fflush(stdout);
		perror("Error: Failed to attach to shared memory");
		return;
	}
}

/* Handle the timer hitting x seconds*/
void TimerHandler(int sig)
{
	Handler(sig);
}

/* Setup interrupt handling */
int SetupInterrupt()
{
	struct sigaction act;
	act.sa_handler = TimerHandler;
	act.sa_flags = 0;
	return (sigemptyset(&act.sa_mask) || sigaction(SIGPROF, &act, NULL));
}

/* setup interrupt handling from the timer */
int SetupTimer()
{
	struct itimerval value;
	value.it_interval.tv_sec = 3;
	value.it_interval.tv_usec = 0;
	value.it_value = value.it_interval;
	return (setitimer(ITIMER_PROF, &value, NULL));
}

/* Find the next empty proccess block. Returns proccess block position if one is available or -1 if one is not */
int FindEmptyProcBlock()
{
	int i;
	for (i = 0; i < childCount; i++)
	{
		if (data->proc[i].pid == -1)
			return i; //return proccess table position of empty
	}

	return -1; //error: no proccess slot available
}

/* Sets all proccess blocks to the initial value of -1 for algorithm reasons */
void SweepProcBlocks()
{
	int i;
	for (i = 0; i < MAX_PROCS; i++)
		data->proc[i].pid = -1;
}

/* Find the proccess block with the given pid and return the position in the array */
int FindPID(int pid)
{
	int i;
	for (i = 0; i < childCount; i++)
		if (data->proc[i].pid == pid)
			return i;
	return -1;
}

/* Find the proccess block with the given local PID */
int FindLocPID(int pid)
{
	int i;
	for (i = 0; i < childCount; i++)
		if (data->proc[i].loc_pid == pid)
			return i;
	return -1;
}

/* The biggest and fattest function west of the missisipi */
void DoSharedWork()
{
	/* General sched data */
	int activeProcs = 0;
	int remainingExecs = 100;
	int exitCount = 0;
	int status;

	/* Proc toChildQueue and message toChildQueue data */
	int activeProcIndex = -1;
	int procRunning = 0;
	int msgsize;

	/* Set shared memory clock value */
	data->sysTime.seconds = 0;
	data->sysTime.ns = 0;

	/* Setup time for random child spawning */
	Time nextExec = { 0,0 };

	/* Setup proccess timeslice */
	Time timesliceEnd = { 0,0 };

	/* Time Statistics */
	Time totalCpuTime = { 0,0 };
	Time totalWaitTime = { 0, 0 };
	Time totalBlockedTime = { 0,0 };
	Time totalTime = { 0,0 };
	Time idleTime = {0, 0};

	/* Create queues */
	struct Queue* queue0 = createQueue(childCount); //Queue of local PIDS (fake/emulated pids)
	struct Queue* queue1 = createQueue(childCount); //Queue of local PIDS (fake/emulated pids)
	struct Queue* queue2 = createQueue(childCount); //Queue of local PIDS (fake/emulated pids)
	struct Queue* queue3 = createQueue(childCount); //Queue of local PIDS (fake/emulated pids)

	struct Queue* queueBlock = createQueue(childCount);

	int queueCost0 = QUEUE_BASE_TIME * 1000000;
	int queueCost1 = QUEUE_BASE_TIME * 2 * 1000000;
	int queueCost2 = QUEUE_BASE_TIME * 3 * 1000000;
	int queueCost3 = QUEUE_BASE_TIME * 4 * 1000000;

	/* Message tracking */
	int pauseSent = 0;

	srand(time(0)); //set random seed

	while (1) {
		AddTime(&(data->sysTime), SCHEDULER_CLOCK_ADD_INC); //increment clock between tasks to advance the clock a little
		AddTime(&(idleTime), SCHEDULER_CLOCK_ADD_INC);

		pid_t pid; //pid temp
		int usertracker = -1; //updated by userready to the position of ready struct to be launched

		/* Only executes when there is a proccess ready to be launched, given the time is right for exec, there is room in the proc table, annd there are execs remaining */
		if (remainingExecs > 0 && activeProcs < childCount && (data->sysTime.seconds >= nextExec.seconds) && (data->sysTime.ns >= nextExec.ns)) 
		{
			pid = fork(); //the mircle of proccess creation

			if (pid < 0) //...or maybe not proccess creation if this executes
			{
				perror("Failed to fork, exiting");
				Handler(1);
			}

			remainingExecs--; //we have less execs now since we launched successfully
			if (pid == 0)
			{
				DoFork(pid); //do the fork thing with exec followup
			}

			/* Setup the next exec for proccess*/
			nextExec.seconds = data->sysTime.seconds; //capture current time
			nextExec.ns = data->sysTime.ns;

			int secstoadd = abs(rand() % (MAX_TIME_BETWEEN_NEW_PROCS_SEC + 1)); //add additional time to current time to setup the next attempted exec
			int nstoadd = abs((rand() * rand()) % (MAX_TIME_BETWEEN_NEW_PROCS_NS + 1));
			AddTimeSpec(&nextExec, secstoadd, nstoadd);

			/* Setup the child proccess and its proccess block if there is a available slot in the control block */
			int pos = FindEmptyProcBlock();
			if (pos > -1)
			{
				/* Initialize the proccess table */
				data->proc[pos].pid = pid; //we stored the pid from fork call and now assign it to PID
				data->proc[pos].realtime = ((rand() % 100) < CHANCE_TO_BE_REALTIME) ? 1 : 0; //Random chance calculation for being realtime or user

				data->proc[pos].tCpuTime.seconds = 0; //Null out statitic tracking times
				data->proc[pos].tCpuTime.ns = 0;

				data->proc[pos].tBlockedTime.seconds = 0;
				data->proc[pos].tBlockedTime.ns = 0;

				data->proc[pos].tSysTime.seconds = data->sysTime.seconds; //set time of birth time
				data->proc[pos].tSysTime.ns = data->sysTime.ns;

				data->proc[pos].loc_pid = ++locpidcnt; //increment localpid counter and assign a new lcaolpid to the child

				/* Logic for determining what queue to initially place the new child after it is generated depending on its priority value*/
				if (data->proc[pos].realtime == 1) 
				{
					data->proc[pos].queueID = 0; //if realtime
					enqueue(queue0, data->proc[pos].loc_pid);
					fprintf(o, "%s: [%i:%i] [PROC CREATE] [PID: %i] LOC_PID: %i INTO QUEUE: 0\n", filen, data->sysTime.seconds, data->sysTime.ns, data->proc[pos].pid, data->proc[pos].loc_pid);

				}
				else
				{
					data->proc[pos].queueID = 1; //if user
					enqueue(queue1, data->proc[pos].loc_pid);
					fprintf(o, "%s: [%i:%i] [PROC CREATE] [PID: %i] LOC_PID: %i INTO QUEUE: 1\n", filen, data->sysTime.seconds, data->sysTime.ns, data->proc[pos].pid, data->proc[pos].loc_pid);
				}

				activeProcs++; //increment active execs
			}
			else
			{
				kill(pid, SIGTERM); //if child failed to find a proccess block, just kill it off
			}
		}

		/*If there is a proccess currently running, we should check if it sends a message in the queue */
		if (procRunning == 1)
		{
			if ((msgsize = msgrcv(toMasterQueue, &msgbuf, sizeof(msgbuf), data->proc[activeProcIndex].pid, 0)) > -1) //blocking wait while waiting for child to respond
			{
				if (strcmp(msgbuf.mtext, "USED_TERM") == 0) //child decides to die
				{
					msgrcv(toMasterQueue, &msgbuf, sizeof(msgbuf), data->proc[activeProcIndex].pid, 0); //After we recieve signal that child used term, wait for the child to send its percentage

					int i;
					sscanf(msgbuf.mtext, "%i", &i); //convert from string to int
					int cost;

					printf("[LOC_PID: %i] Time Finished: %i:%i, Time Started: %i:%i\n", 
						data->proc[activeProcIndex].loc_pid, data->sysTime.seconds, data->sysTime.ns, data->proc[activeProcIndex].tSysTime.seconds, data->proc[activeProcIndex].tSysTime.ns);

					SubTimeOutput(&(data->sysTime), &(data->proc[activeProcIndex].tSysTime), &(data->proc[activeProcIndex].tSysTime));	//calculate total time in system

					switch (data->proc[activeProcIndex].queueID) //depending on what queue it was in, we must increment a different amount of time, this is the logic for that.
					{
					case 0:
						cost = (int)((double)queueCost0 * ((double)i / (double)100)); //cost is equal to the cost of the queue * percentage of total time used (0 - 99%) floored.
						fprintf(o, "\t%s: [%i:%i] [TERMINATE] [PID: %i] LOC_PID: %i AFTER %iNS\n\n", filen, 
							data->sysTime.seconds, data->sysTime.ns, data->proc[activeProcIndex].pid, data->proc[activeProcIndex].loc_pid, cost);

						AddTime(&(data->proc[activeProcIndex].tCpuTime), cost); //statistics tracking and then increment system clock by the cost
						AddTime(&(data->sysTime), cost);
						break;
					case 1:
						cost = (int)((double)queueCost1 * ((double)i / (double)100)); //cost is equal to the cost of the queue * percentage of total time used (0 - 99%) floored.
						fprintf(o, "\t%s: [%i:%i] [TERMINATE] [PID: %i] LOC_PID: %i AFTER %iNS\n\n", filen, 
							data->sysTime.seconds, data->sysTime.ns, data->proc[activeProcIndex].pid, data->proc[activeProcIndex].loc_pid, cost);

						AddTime(&(data->proc[activeProcIndex].tCpuTime), cost);  //statistics tracking and then increment system clock by the cost
						AddTime(&(data->sysTime), cost);
						break;
					case 2:
						cost = (int)((double)queueCost2 * ((double)i / (double)100)); //cost is equal to the cost of the queue * percentage of total time used (0 - 99%) floored.
						fprintf(o, "\t%s: [%i:%i] [TERMINATE] [PID: %i] LOC_PID: %i AFTER %iNS\n\n", filen, 
							data->sysTime.seconds, data->sysTime.ns, data->proc[activeProcIndex].pid, data->proc[activeProcIndex].loc_pid, cost);

						AddTime(&(data->proc[activeProcIndex].tCpuTime), cost);  //statistics tracking and then increment system clock by the cost
						AddTime(&(data->sysTime), cost);
						break;
					case 3:
						cost = (int)((double)queueCost3 * ((double)i / (double)100)); //cost is equal to the cost of the queue * percentage of total time used (0 - 99%) floored.
						fprintf(o, "\t%s: [%i:%i] [TERMINATE] [PID: %i] LOC_PID: %i AFTER %iNS\n\n", filen, 
							data->sysTime.seconds, data->sysTime.ns, data->proc[activeProcIndex].pid, data->proc[activeProcIndex].loc_pid, cost);

						AddTime(&(data->proc[activeProcIndex].tCpuTime), cost);  //statistics tracking and then increment system clock by the cost
						AddTime(&(data->sysTime), cost);
						break;
					}

					procRunning = 0; //proccess is no longer running
				}
				else if (strcmp(msgbuf.mtext, "USED_ALL") == 0) //child decides to use all time
				{
					switch (data->proc[activeProcIndex].queueID) //depending on what queue it was in, we must increment a different amount of time, this is the logic for that.
					{
					case 0:
						enqueue(queue0, data->proc[FindPID(msgbuf.mtype)].loc_pid); //requeue the proccess into the same queue since it is realtime
						data->proc[FindPID(msgbuf.mtype)].queueID = 0;
						AddTime(&(data->sysTime), queueCost0);  //statistics tracking and then increment system clock by the cost
						fprintf(o, "\t%s: [%i:%i] [QUEUE SHIFT 0 -> 0] [PID: %i] LOC_PID: %i\ AFTER FULL %iNS\n\n", filen, 
							data->sysTime.seconds, data->sysTime.ns, data->proc[activeProcIndex].pid, data->proc[activeProcIndex].loc_pid, queueCost0);

						AddTime(&(data->proc[activeProcIndex].tCpuTime), queueCost0);
						break;
					case 1:
						enqueue(queue2, data->proc[FindPID(msgbuf.mtype)].loc_pid); //requeue the proccess into the next level of queue since it used all of its time
						data->proc[FindPID(msgbuf.mtype)].queueID = 2;
						fprintf(o, "\t%s: [%i:%i] [QUEUE SHIFT 1 -> 2] [PID: %i] LOC_PID: %i AFTER FULL %iNS\n\n", filen, 
							data->sysTime.seconds, data->sysTime.ns, data->proc[activeProcIndex].pid, data->proc[activeProcIndex].loc_pid, queueCost1);

						AddTime(&(data->sysTime), queueCost1);  //statistics tracking and then increment system clock by the cost
						AddTime(&(data->proc[activeProcIndex].tCpuTime), queueCost1);
						break;
					case 2:
						enqueue(queue3, data->proc[FindPID(msgbuf.mtype)].loc_pid); //requeue the proccess into the next level of queue since it used all of its time
						data->proc[FindPID(msgbuf.mtype)].queueID = 3;
						fprintf(o, "\t%s: [%i:%i] [QUEUE SHIFT 2 -> 3] [PID: %i] LOC_PID: %i AFTER FULL %iNS\n\n", filen, 
							data->sysTime.seconds, data->sysTime.ns, data->proc[activeProcIndex].pid, data->proc[activeProcIndex].loc_pid, queueCost2);

						AddTime(&(data->sysTime), queueCost2);  //statistics tracking and then increment system clock by the cost
						AddTime(&(data->proc[activeProcIndex].tCpuTime), queueCost2);
						break;
					case 3:
						enqueue(queue3, data->proc[FindPID(msgbuf.mtype)].loc_pid); //requeue the proccess into the same queue because there is nowhere else to go
						data->proc[FindPID(msgbuf.mtype)].queueID = 3;
						fprintf(o, "\t%s: [%i:%i] [QUEUE SHIFT 3 -> 3] [PID: %i] LOC_PID: %i AFTER FULL %iNS\n\n", filen, 
							data->sysTime.seconds, data->sysTime.ns, data->proc[activeProcIndex].pid, data->proc[activeProcIndex].loc_pid);

						AddTime(&(data->sysTime), queueCost3);  //statistics tracking and then increment system clock by the cost
						AddTime(&(data->proc[activeProcIndex].tCpuTime), queueCost3);
						break;
					}
					procRunning = 0; //proccess is no longer running
				}
				else if (strcmp(msgbuf.mtext, "USED_PART") == 0)
				{
					msgrcv(toMasterQueue, &msgbuf, sizeof(msgbuf), data->proc[activeProcIndex].pid, 0); //After we recieve signal that we used part, wait for the child to send its percentage

					int i;
					sscanf(msgbuf.mtext, "%i", &i); //convert from string to int
					int cost;

					switch (data->proc[activeProcIndex].queueID) //depending on what queue it was in, we must increment a different amount of time, this is the logic for that.
					{
					case 0:
						cost = (int)((double)queueCost0 * ((double)i / (double)100)); //cost is equal to the cost of the queue * percentage of total time used (0 - 99%) floored.
						fprintf(o, "\t%s: [%i:%i] [BLOCKED] [PID: %i] LOC_PID: %i AFTER %iNS\n\n", filen, 
							data->sysTime.seconds, data->sysTime.ns, data->proc[activeProcIndex].pid, data->proc[activeProcIndex].loc_pid, cost);

						AddTime(&(data->sysTime), cost);  //statistics tracking and then increment system clock by the cost
						AddTime(&(data->proc[activeProcIndex].tCpuTime), cost);
						break;
					case 1:
						cost = (int)((double)queueCost1 * ((double)i / (double)100)); //cost is equal to the cost of the queue * percentage of total time used (0 - 99%) floored.
						fprintf(o, "\t%s: [%i:%i] [BLOCKED] [PID: %i] LOC_PID: %i AFTER %iNS\n\n", filen, 
							data->sysTime.seconds, data->sysTime.ns, data->proc[activeProcIndex].pid, data->proc[activeProcIndex].loc_pid, cost);

						AddTime(&(data->sysTime), cost);  //statistics tracking and then increment system clock by the cost
						AddTime(&(data->proc[activeProcIndex].tCpuTime), cost);
						break;
					case 2:
						cost = (int)((double)queueCost2 * ((double)i / (double)100)); //cost is equal to the cost of the queue * percentage of total time used (0 - 99%) floored.
						fprintf(o, "\t%s: [%i:%i] [BLOCKED] [PID: %i] LOC_PID: %i AFTER %iNS\n\n", filen, 
							data->sysTime.seconds, data->sysTime.ns, data->proc[activeProcIndex].pid, data->proc[activeProcIndex].loc_pid, cost);

						AddTime(&(data->sysTime), cost);  //statistics tracking and then increment system clock by the cost
						AddTime(&(data->proc[activeProcIndex].tCpuTime), cost);
						break;
					case 3:
						cost = (int)((double)queueCost3 * ((double)i / (double)100)); //cost is equal to the cost of the queue * percentage of total time used (0 - 99%) floored.
						fprintf(o, "\t%s: [%i:%i] [BLOCKED] [PID: %i] LOC_PID: %i AFTER %iNS\n\n", filen, 
							data->sysTime.seconds, data->sysTime.ns, data->proc[activeProcIndex].pid, data->proc[activeProcIndex].loc_pid, cost);

						AddTime(&(data->sysTime), cost);  //statistics tracking and then increment system clock by the cost
						AddTime(&(data->proc[activeProcIndex].tCpuTime), cost);
						break;
					}

					enqueue(queueBlock, data->proc[FindPID(msgbuf.mtype)].loc_pid); //our rule is to load the items into the blocked queue if they have used only part of time
					procRunning = 0; //proccess is no longer running
				}
			}
		}

		if (isEmpty(queueBlock) == 0)
		{
			if (procRunning == 0)
			{
				AddTime(&(idleTime), 5000000);
				AddTime(&(data->sysTime), 5000000);	//No process is running. Hyperspeed until the next process is ready!	
			}		

			int t;
			/* Loop through all blocked entries, return them to mainstream quques if unblocked, else keep them blocked */
			for (t = 0; t < getSize(queueBlock); t++) //I realize this is slightly inefficient, but the alternatives are worse. This is simpler.
			{
				int blockedProcID = FindLocPID(dequeue(queueBlock)); //get next element from blocked queue
				if ((msgsize = msgrcv(toMasterQueue, &msgbuf, sizeof(msgbuf), data->proc[blockedProcID].pid, IPC_NOWAIT)) > -1 && strcmp(msgbuf.mtext, "USED_IO_DONE") == 0) //async check if there was a message from pid
				{
					if (data->proc[blockedProcID].realtime == 1) // if the proccess was realtime, return it back to the realtime queue (queue 0)
					{
						enqueue(queue0, data->proc[blockedProcID].loc_pid);
						data->proc[blockedProcID].queueID = 0;
						fprintf(o, "%s: [%i:%i] [UNBLOCKED] [PID: %i] LOC_PID: %i TO QUEUE: 0\n", filen, 
							data->sysTime.seconds, data->sysTime.ns, data->proc[blockedProcID].pid, data->proc[blockedProcID].loc_pid);
					}
					else //otherwise, it is a user proccess, throw it in with the rest of the users in queue 1
					{
						enqueue(queue1, data->proc[blockedProcID].loc_pid);
						data->proc[blockedProcID].queueID = 1;
						fprintf(o, "%s: [%i:%i] [UNBLOCKED] [PID: %i] LOC_PID: %i TO QUEUE: 0\n", filen, 
							data->sysTime.seconds, data->sysTime.ns, data->proc[blockedProcID].pid, data->proc[blockedProcID].loc_pid);
					}

					int schedCost = ((rand() % 9900) + 100);
					AddTime(&(data->sysTime), schedCost); //simulate scheduling cost to the system

					fprintf(o, "\t%s: [%i:%i] [SCHEDULER] [PID: %i] LOC_PID: %i COST TO MOVE: %iNS\n\n", filen, 
						data->sysTime.seconds, data->sysTime.ns, data->proc[blockedProcID].pid, data->proc[blockedProcID].loc_pid, schedCost);
				}
				else
				{
					enqueue(queueBlock, data->proc[blockedProcID].loc_pid); //proc not ready to be unblocked
				}
			}
		}

		/* if there is something in one of the queues and no proccess running, let's get a proccess running */
		if ((isEmpty(queue0) == 0 || isEmpty(queue1) == 0 || isEmpty(queue2) == 0 || isEmpty(queue3) == 0) && procRunning == 0)
		{
			if (isEmpty(queue0) == 0) //there is something in queue 0, we are traversing these queues in the order of importance
			{
				activeProcIndex = FindLocPID(dequeue(queue0)); //dequeue the proccess
				msgbuf.mtype = data->proc[activeProcIndex].pid;
				strcpy(msgbuf.mtext, "");
				fprintf(o, "%s: [%i:%i] [DISPATCH] [PID: %i] LOC_PID: %i QUEUE: 0\n", filen, 
					data->sysTime.seconds, data->sysTime.ns, data->proc[activeProcIndex].pid, data->proc[activeProcIndex].loc_pid);

				msgsnd(toChildQueue, &msgbuf, sizeof(msgbuf), IPC_NOWAIT); //wake the child
			}
			else if (isEmpty(queue1) == 0) //there is something in queue 1 but not queue 0
			{
				activeProcIndex = FindLocPID(dequeue(queue1)); //dequeue the proccess
				msgbuf.mtype = data->proc[activeProcIndex].pid;
				strcpy(msgbuf.mtext, "");
				fprintf(o, "%s: [%i:%i] [DISPATCH] [PID: %i] LOC_PID: %i QUEUE: 1\n", filen, 
					data->sysTime.seconds, data->sysTime.ns, data->proc[activeProcIndex].pid, data->proc[activeProcIndex].loc_pid);

				msgsnd(toChildQueue, &msgbuf, sizeof(msgbuf), IPC_NOWAIT); //wake the child
			}
			else if (isEmpty(queue2) == 0) //there is something in queue 2 but not in 1 or 0
			{
				activeProcIndex = FindLocPID(dequeue(queue2)); //dequeue the proccess
				msgbuf.mtype = data->proc[activeProcIndex].pid;
				strcpy(msgbuf.mtext, "");
				fprintf(o, "%s: [%i:%i] [DISPATCH] [PID: %i] LOC_PID: %i QUEUE: 2\n", filen, 
					data->sysTime.seconds, data->sysTime.ns, data->proc[activeProcIndex].pid, data->proc[activeProcIndex].loc_pid);

				msgsnd(toChildQueue, &msgbuf, sizeof(msgbuf), IPC_NOWAIT); //wake the child
			}
			else if (isEmpty(queue3) == 0) //there is something in queue 3 but not in queue 1 or 0 or 2
			{
				activeProcIndex = FindLocPID(dequeue(queue3)); //dequeue the proccess
				msgbuf.mtype = data->proc[activeProcIndex].pid;
				strcpy(msgbuf.mtext, "");
				fprintf(o, "%s: [%i:%i] [DISPATCH] [PID: %i] LOC_PID: %i QUEUE: 3\n", filen, 
					data->sysTime.seconds, data->sysTime.ns, data->proc[activeProcIndex].pid, data->proc[activeProcIndex].loc_pid);

				msgsnd(toChildQueue, &msgbuf, sizeof(msgbuf), IPC_NOWAIT); //wake the child
			}

			int schedCost = ((rand() % 9900) + 100);
			AddTime(&(data->sysTime), schedCost); //simulate scheduling time
			fprintf(o, "\t%s: [%i:%i] [SCHEDULER] [PID: %i] LOC_PID: %i COST TO SCHEDULE: %iNS\n", filen, 
				data->sysTime.seconds, data->sysTime.ns, data->proc[activeProcIndex].pid, data->proc[activeProcIndex].loc_pid, schedCost);

			procRunning = 1; //signal that there is a proccess running
		}

		if ((pid = waitpid((pid_t)-1, &status, WNOHANG)) > 0) //if a PID is returned meaning the child died
		{
			if (WIFEXITED(status))
			{
				if (WEXITSTATUS(status) == 21) //21 is my custom return val
				{
					exitCount++;
					activeProcs--;

					int position = FindPID(pid);

					/* Perform post-mortem statistical calculations */
					data->proc[position].tWaitTime.seconds = data->proc[position].tSysTime.seconds; //capture current time as wait time
					data->proc[position].tWaitTime.ns = data->proc[position].tSysTime.ns;

					SubTime(&(data->proc[position].tWaitTime), &(data->proc[position].tCpuTime)); //obtain wait time by subtrcting blocked and cpuTime from time in the system
					SubTime(&(data->proc[position].tWaitTime), &(data->proc[position].tBlockedTime));

					printf("/**TIME STATS FOR LOC_PID: %i**/\n\tCPU Time: %i:%i\n\tWait Time: %i:%i\n\tBlocked Time: %i:%i\n\t--------------------------\n\n", 
						data->proc[position].loc_pid, data->proc[position].tCpuTime.seconds, data->proc[position].tCpuTime.ns, data->proc[position].tWaitTime.seconds, 
						data->proc[position].tWaitTime.ns, data->proc[position].tBlockedTime.seconds, data->proc[position].tBlockedTime.ns);

					AddTimeLong(&(totalCpuTime), (((long)data->proc[position].tCpuTime.seconds * (long)1000000000)) + (long)(data->proc[position].tCpuTime.ns)); 
					AddTimeLong(&(totalWaitTime), (((long)data->proc[position].tWaitTime.seconds) * (long)1000000000) + (long)(data->proc[position].tWaitTime.ns));
					AddTimeLong(&(totalBlockedTime), ((long)(data->proc[position].tBlockedTime.seconds) * (long)1000000000) + (long)(data->proc[position].tBlockedTime.ns));

					if (position > -1)
						data->proc[position].pid = -1;
				}
			}
		}

		if (remainingExecs <= 0 && exitCount >= 100) //only get out of loop if we run out of execs or we have maxed out child count
		{
			totalTime.seconds = data->sysTime.seconds; //after the simulation has finished, copy over the final clock values over to a local structure
			totalTime.ns = data->sysTime.ns;
			break;
		}
		fflush(stdout);
	}

	/* Print total times */
	printf("/** TOTAL TIMES **/\n\tTotal Time: %i:%i\n\tCPU Time: %i:%i\n\tWait Time: %i:%i\n\tBlocked Time: %i:%i\n\tSystem Idle Time %i:%i\n\t--------------------\n\n",
		 totalTime.seconds, totalTime.ns, totalCpuTime.seconds, totalCpuTime.ns, totalWaitTime.seconds, 
		 totalWaitTime.ns, totalBlockedTime.seconds, totalBlockedTime.ns, idleTime.seconds, idleTime.ns);

	/* Replace total times with average times instead */
	AverageTime(&(totalTime), exitCount);
	AverageTime(&(totalCpuTime), exitCount);
	AverageTime(&(totalWaitTime), exitCount);
	AverageTime(&(totalBlockedTime), exitCount);
	AverageTime(&(idleTime), exitCount);

	/* Print average times */
	printf("/** AVERAGE **/\n\tTotal Time: %i:%i\n\tCPU Time: %i:%i\n\tWait Time: %i:%i\n\tBlocked Time: %i:%i\n\tSystem Idle Time %i:%i\n\t--------------------\n\n", 
		totalTime.seconds, totalTime.ns, totalCpuTime.seconds, totalCpuTime.ns, totalWaitTime.seconds, 
		totalWaitTime.ns, totalBlockedTime.seconds, totalBlockedTime.ns, idleTime.seconds, idleTime.ns);

	/* Wrap up the output file and detatch from shared memory items */
	shmctl(ipcid, IPC_RMID, NULL);
	msgctl(toChildQueue, IPC_RMID, NULL);
	msgctl(toMasterQueue, IPC_RMID, NULL);
	fflush(o);
	fclose(o);
}

/* Attach to queues incoming/outgoing */
void QueueAttatch()
{
	key_t shmkey = ftok("shmsharemsg", 766);

	if (shmkey == -1) //check if the input file exists
	{
		fflush(stdout);
		perror("./oss: Error: Ftok failed");
		return;
	}

	toChildQueue = msgget(shmkey, 0600 | IPC_CREAT); //attach to child queue

	if (toChildQueue == -1)
	{
		fflush(stdout);
		perror("./oss: Error: toChildQueue creation failed");
		return;
	}

	shmkey = ftok("shmsharemsg2", 767);

	if (shmkey == -1) //check if the input file exists
	{
		fflush(stdout);
		perror("./oss: Error: Ftok failed");
		return;
	}

	toMasterQueue = msgget(shmkey, 0600 | IPC_CREAT); //attach to master queue

	if (toMasterQueue == -1)
	{
		fflush(stdout);
		perror("./oss: Error: toMasterQueue creation failed");
		return;
	}
}

/* Program entry point */
int main(int argc, int** argv)
{
	//alias for file name
	filen = argv[0]; //shorthand for filename

	if (SetupInterrupt() == -1) //Handler for SIGPROF failed
	{
		perror("./oss: Failed to setup Handler for SIGPROF");
		return 1;
	}
	if (SetupTimer() == -1) //timer failed
	{
		perror("./oss: Failed to setup ITIMER_PROF interval timer");
		return 1;
	}

	int optionItem;
	while ((optionItem = getopt(argc, argv, "hn:")) != -1) //read option list
	{
		switch (optionItem)
		{
		case 'h': //show help menu
			printf("\t%s Help Menu\n\
		\t-h : show help dialog \n\
		\t-n [count] : max proccesses at the same time. Default: 19\n\n", filen);
			return;
		case 'n': //max # of children
			childCount = atoi(optarg);
			if(childCount > 19 || childCount < 0) //if 0  > n > 20 
			{
				printf("%s: Max -n is 19. Must be > 0 Aborting.\n", argv[0]);
				return -1;					
			}

			printf("\n%s: Info: set max concurrent children to: %s", argv[0], optarg);
			break;
		case '?': //an error has occoured reading arguments
			printf("\n%s: Error: Invalid Argument or Arguments missing. Use -h to see usage.", argv[0]);
			return;
		}
	}

	o = fopen("./oss: output.log", "w"); //open output file

	if(o == NULL) //check if file was opened
	{
		perror("oss: Failed to open output file: ");
		return 1;
	}

	ShmAttatch(); //attach to shared mem
	QueueAttatch(); //attach to queues
	SweepProcBlocks(); //reset all proc blocks
	signal(SIGINT, Handler); //setup handler for CTRL-C
	DoSharedWork(); //fattest function west of the mississippi

	return 0;
}