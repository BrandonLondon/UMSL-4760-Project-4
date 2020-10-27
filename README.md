# PURPOSE
The goal of this homework is to learn about process scheduling inside an operating system. You will work on the specified scheduling
algorithm and simulate its performance.
# Task
In this project, you will simulate the process scheduling part of an operating system. You will implement time-based scheduling,
ignoring almost every other aspect of the OS. Please use message queues for synchronization.
# Invoking the solution
Execute oss with no parameters. You may add some parameters to modify the number of processes or base quantum but if you do so,
document this in your README.
# How to Run
```
$ make
$ ./oss [options]
```
# Options
```
-h -> show help menu
-n -> how many children should exist at any given time. Max 19
```
# Output
output.log

# Termination Criteria
oss should stop generating processes if it has already generated 100 processes or if more than 3 real-life seconds have passed. If you
stop adding new processes, the system should eventually empty of processes and then it should terminate. What is important is that
you tune your parameters so that the system has processes in all the queues at some point and that I can see that in the log file. As
discussed previously, ensure that appropriate statistics are displayed.
# Issues
There are some issues in the output file. some entries may not be in the proper order, or may seem to "overlap" in time even with a queue size of 1, but this is false. Each proccess follows the termination of the last in this case.
# VERSION CONTROL:

==============================================================================Commits on Oct,27 2020=============================================================================
-Added expired queues and finshing touches

@BrandonLondon
BrandonLondon committed 13 minutes ago
 
====================================================================================Commits on Oct,26 2020======================================================================
Finished Rough outline of program

@BrandonLondon
BrandonLondon committed 17 hours ago
 
Adding Termination

@BrandonLondon
BrandonLondon committed 18 hours ago
 
Fixed Bug, program would stop randomly

@BrandonLondon
BrandonLondon committed 22 hours ago
 
Bug***** Program wont stop

@BrandonLondon
BrandonLondon committed yesterday
 
=====================================================================================Commits on Oct,25 2020=====================================================================
Implemented s and n

@BrandonLondon
BrandonLondon committed 2 days ago
 
Created variables to hold paramter values

@BrandonLondon
BrandonLondon committed 2 days ago
 
Kill the Child

@BrandonLondon
BrandonLondon committed 2 days ago
 
Corrupt nonsense

@BrandonLondon
BrandonLondon committed 2 days ago
 
Im a mess

@BrandonLondon
BrandonLondon committed 2 days ago
 
Finshing Queue.h

@BrandonLondon
BrandonLondon committed 2 days ago
 
Added Queue.h

@BrandonLondon
BrandonLondon committed 2 days ago
 
===========================================================Commits on Oct,24 2020=======================================================================
type def structs

@BrandonLondon
BrandonLondon committed 3 days ago
 

======================================Commits on Oct,21 2020============================================================================================
attached shared memory

@BrandonLondon
BrandonLondon committed 6 days ago
 
Added a release memory function 

@BrandonLondon
BrandonLondon committed 6 days ago
 
added a bunch of things

@BrandonLondon
BrandonLondon committed 6 days ago
 
help

@BrandonLondon
BrandonLondon committed 6 days ago
 
help

@BrandonLondon
BrandonLondon committed 6 days ago
 
help

@BrandonLondon
BrandonLondon committed 6 days ago
 
Trying to add memory

@BrandonLondon
BrandonLondon committed 6 days ago
 
Depression Begins

BrandonLondon
BrandonLondon committed 6 days ago
 
added getopt

Brandon London
Brandon London committed 6 days ago
 
==========================================================================Commits on Oct,20 2020======================================================================
Some changes to file extensions

@BrandonLondon
BrandonLondon committed 7 days ago
 
Some changes to file extensions

@BrandonLondon
BrandonLondon committed 7 days ago
 
===============================================================================Commits on Oct,19 2020=====================================================================
Inital commit, created files

Brandon London
Brandon London committed 8 days ago
