#include <stdio.h>			 // FILE, NULL, fflush, fopen, fclose, printf, etc
#include <stdlib.h> 		 // rand(), srand()
#include <time.h>    		 // time()
#include <sys/types.h>		 // pid_t, size_t
#include <sys/wait.h>		 // wait
#include <unistd.h>			 // getpid, fork, sleep
#include <semaphore.h>		 // sem_t, SEM_FAILED
#include <fcntl.h>			 // O_CREAT, O_RDONLY, O_WRONLY
#include <sys/mman.h>        // mman
#include<string.h>           // memove
#include <sys/shm.h>         // shmdt

//how to run
// gcc -pthread main.c

// total number of processes to generate and schedule 
#define NUM_OF_PROCESSES 200

// upper and lower limits for how many cycles a process can take
// add one to upper limit to make the rand inclusive
#define UPPER_CYCLE 50000000000001
#define LOWER_CYCLE 10000000

// upper and lower limits for ammount of memory a process can take
// expressed in MB * 100 to avoid fractional numbers
#define UPPER_MEMORY 800000
#define LOWER_MEMORY 25

//clock speed 
#define GHZ 8000000000

// will be used to lock down file I/O between processes to keep it atomic
const char *semName = "/semLock";
// rogue semaphores are here - /dev/shm
// if weird error with semaphore check if it is still left open here

//structs
struct processes{
	char name[5];
	unsigned long long int cycleTime;
	int memory;
};

//protoypes
double scheduler2mem(sem_t *sem_id, int *numProcesses, struct processes prosArr2mem[]);
double scheduler4mem(sem_t *sem_id, int *numProcesses, struct processes prosArr4mem[]);
double scheduler8mem(sem_t *sem_id, int *numProcesses, struct processes prosArr8mem[]);
void sortProcesses(struct processes prosArr[], struct processes prosArr2mem[], struct processes prosArr4mem[], struct processes prosArr8mem[]);
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
	
	// array of processes, we will remove elements from the array as each processor takes on a process
	struct processes* prosArr = mmap(NULL, NUM_OF_PROCESSES * sizeof(prosArr), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	struct processes* prosArr2mem = mmap(NULL, NUM_OF_PROCESSES * sizeof(prosArr2mem), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	struct processes* prosArr4mem = mmap(NULL, NUM_OF_PROCESSES * sizeof(prosArr4mem), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	struct processes* prosArr8mem = mmap(NULL, NUM_OF_PROCESSES * sizeof(prosArr8mem), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

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
	sortProcesses(prosArr, prosArr2mem, prosArr4mem, prosArr8mem);
	//test print
	printPros(prosArr);
	
	//create semaphore to be used for critical sections
	sem_t *sem_id = sem_open(semName, O_CREAT, 0644, 1);
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
	fflush(0); // not directly relevant but always a good idea before forking
	for (int i = 0; i < 5; i++) {
		if (fork() == 0) {
		   	//call scheduling child function here
			if(i < 2){
		   	*totalTime += scheduler2mem(sem_id, numProcesses, prosArr2mem);
			}
			if(i >= 2 && i < 4){
		   	*totalTime += scheduler4mem(sem_id, numProcesses, prosArr4mem);
			*totalTime += scheduler2mem(sem_id, numProcesses, prosArr2mem);
			}
			if(i == 4){
		   	*totalTime += scheduler8mem(sem_id, numProcesses, prosArr8mem);
		   	*totalTime += scheduler4mem(sem_id, numProcesses, prosArr4mem);
			*totalTime += scheduler2mem(sem_id, numProcesses, prosArr2mem);
			}
		   	exit(0);
		}
		// control reaches this point only in the parent
	}
	
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
 * functional loop where the 5 children processes will go into and begin to 
 * read the processes in randomProcesses.txt and then schdule which processor
 * will run which process
 */
//scheduler for 2GB processes
double scheduler2mem(sem_t *sem_id, int *numProcesses, struct processes prosArr2mem[]){
	double timeToRun = 0;       //keep track of wait time + execution time of everything that ran on this processor
	double execTime = 0, wait = 0;
	
	while(prosArr2mem[0].name != 0){
			// if semaphore available, then continue
			if (sem_wait(sem_id) < 0)
				printf("%d  : [sem_wait] Failed\n", getpid());
			// THIS IS CRITICAL SECTION
		
			if (*numProcesses <= 0){
				if (sem_post(sem_id) < 0)
					printf("%d   : [sem_post] Failed \n", getpid());
				return timeToRun + wait;
			}
		
			// get next item in prosArr
			printf("%s  time: %llu   mem: %d   pid: %d\n", prosArr2mem[0].name, prosArr2mem[0].cycleTime, prosArr2mem[0].memory, getpid());
			memmove(&prosArr2mem[0], &prosArr2mem[1], (NUM_OF_PROCESSES - 1) * sizeof(struct processes));
			*numProcesses = *numProcesses - 1;
		
			// calulate timeToRun
			execTime = (double)prosArr2mem[0].cycleTime / GHZ;
			wait += timeToRun;
			timeToRun += execTime;
	
			//release control of semaphore
			if (sem_post(sem_id) < 0)
				printf("%d   : [sem_post] Failed \n", getpid());
		
			//sleep for last timeToRun /1000
			usleep(execTime * 1000);

	}	
	
	return timeToRun + wait;
}

//scheduler for 4GB processes
double scheduler4mem(sem_t *sem_id, int *numProcesses, struct processes prosArr4mem[]){
	double timeToRun = 0;       //keep track of wait time + execution time of everything that ran on this processor
	double execTime = 0, wait = 0;
	
	while(prosArr4mem[0].name != 0){
			// if semaphore available, then continue
			if (sem_wait(sem_id) < 0)
				printf("%d  : [sem_wait] Failed\n", getpid());
			// THIS IS CRITICAL SECTION
		
			if (*numProcesses <= 0){
				if (sem_post(sem_id) < 0)
					printf("%d   : [sem_post] Failed \n", getpid());
				return timeToRun + wait;
			}
		
			// get next item in prosArr
			printf("%s  time: %llu   mem: %d   pid: %d\n", prosArr4mem[0].name, prosArr4mem[0].cycleTime, prosArr4mem[0].memory, getpid());
			memmove(&prosArr4mem[0], &prosArr4mem[1], (NUM_OF_PROCESSES - 1) * sizeof(struct processes));
			*numProcesses = *numProcesses - 1;
		
			// calulate timeToRun
			execTime = (double)prosArr4mem[0].cycleTime / GHZ;
			wait += timeToRun;
			timeToRun += execTime;
	
			//release control of semaphore
			if (sem_post(sem_id) < 0)
				printf("%d   : [sem_post] Failed \n", getpid());
		
			//sleep for last timeToRun /1000
			usleep(execTime * 1000);

	}	
	
	return timeToRun + wait;
}

//scheduler for 8GB processes
double scheduler8mem(sem_t *sem_id, int *numProcesses, struct processes prosArr8mem[]){
	double timeToRun = 0;       //keep track of wait time + execution time of everything that ran on this processor
	double execTime = 0, wait = 0;
	
	while(prosArr8mem[0].name != 0){
			// if semaphore available, then continue
			if (sem_wait(sem_id) < 0)
				printf("%d  : [sem_wait] Failed\n", getpid());
			// THIS IS CRITICAL SECTION
		
			if (*numProcesses <= 0){
				if (sem_post(sem_id) < 0)
					printf("%d   : [sem_post] Failed \n", getpid());
				return timeToRun + wait;
			}
		
			// get next item in prosArr
			printf("%s  time: %llu   mem: %d   pid: %d\n", prosArr8mem[0].name, prosArr8mem[0].cycleTime, prosArr8mem[0].memory, getpid());
			memmove(&prosArr8mem[0], &prosArr8mem[1], (NUM_OF_PROCESSES - 1) * sizeof(struct processes));
			*numProcesses = *numProcesses - 1;
		
			// calulate timeToRun
			execTime = (double)prosArr8mem[0].cycleTime / GHZ;
			wait += timeToRun;
			timeToRun += execTime;
	
			//release control of semaphore
			if (sem_post(sem_id) < 0)
				printf("%d   : [sem_post] Failed \n", getpid());
		
			//sleep for last timeToRun /1000
			usleep(execTime * 1000);
	}	
	
	return timeToRun + wait;
}

/*
 * take in the arr of processes 
 * and sort based on time needed to run
 */
void sortProcesses(struct processes prosArr[], struct processes prosArr2mem[], struct processes prosArr4mem[], struct processes prosArr8mem[])
{
	int i, j;
	int n = NUM_OF_PROCESSES;
	struct processes key;
	const char* nullPtr = '\0';
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

	//distribute processes to memory limited arrays based on memory size of process
	j = 0;
	for (i = 0; i < n; i++)
    { 
        if(prosArr[i].memory < 200000)
		{
			prosArr2mem[j] = prosArr[i];
			j++;
		}  
	}
	j = 0;
	for (i = 0; i < n; i++)
    { 
        if(prosArr[i].memory >= 200000 && prosArr[i].memory < 400000)
		{
			prosArr4mem[j] = prosArr[i];
			j++;
		}  
	}
	j = 0;
	for (i = 0; i < n; i++)
    { 
        if(prosArr[i].memory >= 400000)
		{
			prosArr8mem[j] = prosArr[i];
			j++;
		}  
	}
printf("debug 2\n");	

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
	
    unsigned int day = n / (24 * 3600);
 
    n = n % (24 * 3600);
    unsigned int hour = n / 3600;
 
    n %= 3600;
    unsigned int minutes = n / 60 ;
 
    n %= 60;
    unsigned int seconds = n;
    
	printf("days: %d\n", day);
	printf("hours: %d\n", hour);
	printf("minutes: %d\n", minutes);
	printf("seconds: %d\n", seconds);
}