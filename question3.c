#include <stdio.h>			 //FILE, NULL, fflush, fopen, fclose, printf, etc
#include <stdlib.h> 		 //rand(), srand()
#include <time.h>    		 //time()
#include <sys/types.h>		 //pid_t, size_t
#include <sys/wait.h>		 //wait
#include <unistd.h>			 //getpid, fork, sleep
#include <semaphore.h>		 //sem_t, SEM_FAILED
#include <fcntl.h>			 //O_CREAT, O_RDONLY, O_WRONLY
#include <sys/mman.h>        //mman
#include<string.h>           //memove
#include <sys/shm.h>         //shmdt

//how to run
//gcc -pthread question3.c

//total number of processes to generate and schedule 
#define NUM_OF_PROCESSES 200

//upper and lower limits for how many cycles a process can take
//add one to upper limit to make the rand inclusive
#define UPPER_CYCLE 50000000000001
#define LOWER_CYCLE 10000000

//upper and lower limits for ammount of memory a process can take
//expressed in MB * 100 to avoid fractional numbers
#define UPPER_MEMORY 800000
#define LOWER_MEMORY 25

//clock speed 
#define GHZ2 2000000000
#define GHZ3 3000000000
#define GHZ4 4000000000

//will be used to lock down file I/O between processes to keep it atomic
const char *semName = "/semLock";
//rogue semaphores are here - /dev/shm
//if weird error with semaphore check if it is still left open here

//structs
struct processes
{
	char name[5];
	unsigned long long int cycleTime;
	int memory;
};

//protoypes
double schedulerSmallFirst(sem_t *sem_id, int *numProcesses, struct processes prosArr[], long long ghz);
double schedulerBigFirst(sem_t *sem_id, int *numProcesses, struct processes prosArr[], long long ghz);
void sortProcesses(struct processes prosArr[]);
void printPros(struct processes prosArr[]);
void convertSectoDay(unsigned long long int totalTime);

int main()
{	
	//stack variables
	char buffer[1024];
	FILE *fp;
	int parentPid = getpid();
	unsigned long long int tempCycle; 
	int tempMem;
	
	//SHARED MEMORY
	// time each processor spent running
	double *totalTime = mmap(NULL, sizeof *totalTime, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	*totalTime = 0;
	int *numProcesses = mmap(NULL, sizeof *numProcesses, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	*numProcesses = NUM_OF_PROCESSES;
	
	// array of processes, we will remove elements from the array as each process takes on a process
	struct processes* prosArr = mmap(NULL, NUM_OF_PROCESSES * sizeof(prosArr), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	
	// Intializes random number generator
	time_t t;
	srand((unsigned) time(&t));
	
	//open file to store generated processes
	fp = fopen("randomProcesses.txt", "w+");
	
	//generate random processes to write to file and store in array
	for (int i = 0; i < NUM_OF_PROCESSES; i++)
	{
		// get random time and memory for this process
		tempCycle = rand() % (UPPER_CYCLE - LOWER_CYCLE) + LOWER_CYCLE;
		tempMem = rand() % (UPPER_MEMORY - LOWER_MEMORY) + LOWER_MEMORY;
		fprintf(fp, "p%d\t burst time: %llu\t\t memory requirement: %d\n", i, tempCycle, tempMem);
		
		// sort this later
		prosArr[i].cycleTime = tempCycle;
		prosArr[i].memory = tempMem;
		snprintf(prosArr[i].name, 12, "p%d", i);
	}
	
	//close file
	fclose(fp);
	
	// sort arr of structs
	sortProcesses(prosArr);
	
	//test print
	//printPros(prosArr);
	
	//create semaphore to be used for critical sections
	sem_t *sem_id = sem_open(semName, O_CREAT, 0644, 0);
	if (sem_id == SEM_FAILED)
	{
        perror("Parent  : [sem_open] Failed\n");
		exit(EXIT_FAILURE);
	}
	if (sem_unlink(semName) < 0)
	{
		printf("Parent  : [sem_unlink] Failed\n");
		exit(EXIT_FAILURE);
	}
	
	//create 5 children to go and begin execution
	pid_t child_pid, wpid;
	int status = 0;

	child_pid = fork();
	if (child_pid == 0){
		*totalTime += schedulerBigFirst(sem_id, numProcesses, prosArr, GHZ2);
		exit(0);
	}
	else if (child_pid < 0){
		printf("Child  : [fork] Failed\n");
		exit(EXIT_FAILURE);
	}
	child_pid = fork();
	if (child_pid == 0){
		*totalTime += schedulerBigFirst(sem_id, numProcesses, prosArr, GHZ2);
		exit(0);
	}
	else if (child_pid < 0){
		printf("Child  : [fork] Failed\n");
		exit(EXIT_FAILURE);
	}
	child_pid = fork();
	if (child_pid == 0){
		*totalTime += schedulerBigFirst(sem_id, numProcesses, prosArr, GHZ3);
		exit(0);
	}
	else if (child_pid < 0){
		printf("Child  : [fork] Failed\n");
		exit(EXIT_FAILURE);
	}
	child_pid = fork();
	if (child_pid == 0){
		*totalTime += schedulerSmallFirst(sem_id, numProcesses, prosArr, GHZ3);
		exit(0);
	}
	else if (child_pid < 0){
		printf("Child  : [fork] Failed\n");
		exit(EXIT_FAILURE);
	}
	child_pid = fork();
	if (child_pid == 0){
		*totalTime += schedulerSmallFirst(sem_id, numProcesses, prosArr, GHZ4);
		exit(0);
	}
	else if (child_pid < 0){
		printf("Child  : [fork] Failed\n");
		exit(EXIT_FAILURE);
	}
	
	// give children time to all reach semWait
	sleep(1);
		
	//release control of semaphore
	if (sem_post(sem_id) < 0)
		printf("%d   : [sem_post] Failed \n", getpid());
	
	// the parent waits for all the child processes to finish
	while ((wpid = wait(&status)) > 0); 
	
	//get total time it took to run
	printf("time to run through all processes: %f\n", *totalTime);
	
	//put in recognizable terms 
	convertSectoDay((unsigned long long int)*totalTime);
	
	//free memory
	shmdt(numProcesses);
	shmdt(totalTime);

    return 0;
}

 /*
 * functional loop where the 2 faster children processes will go into and begin to 
 * read the processes from memory and then schdule which processor
 * will run which process
 */
double schedulerSmallFirst(sem_t *sem_id, int *numProcesses, struct processes prosArr[], long long ghz)
{
	double timeToRun = 0;       //keep track of wait time + execution time of everything that ran on this processor
	double execTime = 0, wait = 0;
	
	while (*numProcesses > 0)
	{
		// if semaphore available, then continue
		if (sem_wait(sem_id) < 0)
			printf("%d  : [sem_wait] Failed\n", getpid());
		// THIS IS CRITICAL SECTION
		
		if (*numProcesses <= 0)
		{
			if (sem_post(sem_id) < 0)
				printf("%d   : [sem_post] Failed \n", getpid());
			return timeToRun + wait;
		}
		
		// get next item in posArr
		printf("name: %s  \ttime: %llu   \tmem: %d   \tpid: %d\n", prosArr[0].name, prosArr[0].cycleTime, prosArr[0].memory, getpid());
		memmove(&prosArr[0], &prosArr[1], (NUM_OF_PROCESSES - 1) * sizeof(struct processes));
		*numProcesses = *numProcesses - 1;
		
		// calulate timeToRun
		execTime = (double)prosArr[0].cycleTime / ghz;
		wait += timeToRun;
		timeToRun += execTime;
	
		//release control of semaphore
		if (sem_post(sem_id) < 0)
			printf("%d   : [sem_post] Failed \n", getpid());
		
		//sleep for last timeToRun /1000
		execTime *= 1000;
		usleep(execTime);
		
	}	
	return timeToRun + wait;
}

/*
 * functional loop where the 3 slower children processes will go into and begin to 
 * read the processes from memory and then schdule which processor
 * will run which process
 */
double schedulerBigFirst(sem_t *sem_id, int *numProcesses, struct processes prosArr[], long long ghz)
{
	double timeToRun = 0;       //keep track of wait time + execution time of everything that ran on this processor
	double execTime = 0, wait = 0;
	
	while (*numProcesses > 0)
	{
		// if semaphore available, then continue
		if (sem_wait(sem_id) < 0)
			printf("%d  : [sem_wait] Failed\n", getpid());
		// THIS IS CRITICAL SECTION
		
		if (*numProcesses <= 0)
		{
			if (sem_post(sem_id) < 0)
				printf("%d   : [sem_post] Failed \n", getpid());
			return timeToRun + wait;
		}
		// get next item in posArrs
		printf("name: %s  \ttime: %llu   \tmem: %d   \tpid: %d\n", prosArr[*numProcesses - 1].name, prosArr[*numProcesses - 1].cycleTime, prosArr[*numProcesses - 1].memory, getpid());
		*numProcesses = *numProcesses - 1;
		
		// calulate timeToRun
		execTime = (double)prosArr[*numProcesses - 1].cycleTime / ghz;
		wait += timeToRun;
		timeToRun += execTime;
	
		//release control of semaphore
		if (sem_post(sem_id) < 0)
			printf("%d   : [sem_post] Failed \n", getpid());
		
		//sleep for last timeToRun /1000
		execTime *= 1000;
		usleep(execTime);
	}	
	return timeToRun + wait;
}

/*
 * take in the arr of processes 
 * and sort based on time needed to run
 */
void sortProcesses(struct processes prosArr[])
{
	int i, j;
	int n = NUM_OF_PROCESSES;
	struct processes key;
    for (i = 1; i < n; i++)
    { 
        key = prosArr[i]; 
        j = i - 1; 

        /* Move elements of arr[0..i-1], that are 
        greater than key, to one position ahead 
        of their current position */
        while (j >= 0 && prosArr[j].cycleTime > key.cycleTime)
        { 
            prosArr[j + 1] = prosArr[j];
            j = j - 1; 
        } 
        prosArr[j + 1] = key; 
    } 
}

/*
 * test print for the array of processes
 */
void printPros(struct processes prosArr[])
{
	int n = NUM_OF_PROCESSES;
	for (int i = 1; i < n; i++)
    { 
		printf("%s  time: %llu   mem: %d\n", prosArr[i].name, prosArr[i].cycleTime, prosArr[i].memory);
	}
}

/*
 * converts seconds to
 * day, hour, minute, second
 */
void convertSectoDay(unsigned long long int n)
{
	unsigned int month = n / (24 * 3600 * 31);
	
	n = n % (24 * 3600 * 31);
    unsigned int day = n / (24 * 3600);
 
    n = n % (24 * 3600);
    unsigned int hour = n / 3600;
 
    n %= 3600;
    unsigned int minutes = n / 60 ;
 
    n %= 60;
    unsigned int seconds = n;
    
	printf("months %d\n", month);
	printf("days: %d\n", day);
	printf("hour: %d\n", hour);
	printf("minutes: %d\n", minutes);
	printf("seconds: %d\n", seconds);
}
