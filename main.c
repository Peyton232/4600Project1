#include <stdio.h>			 // FILE, NULL, fflush, fopen, fclose, printf, etc
#include <stdlib.h> 		 // rand(), srand()
#include <time.h>    		 // time()
#include <sys/types.h>		 // pid_t, size_t
#include <sys/wait.h>		 // wait
#include <unistd.h>			 // getpid, fork, sleep
#include <semaphore.h>		 // sem_t, SEM_FAILED
#include <fcntl.h>			 // O_CREAT, O_RDONLY, O_WRONLY

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
#define UPPER_MEMORY 25
#define LOWER_MEMORY 800000

// will be used to lock down file I/O between processes to keep it atomic
const char *semName = "/semLock";
// rogue semaphores are here - /dev/shm
// if weird error with semaphore check if it is still left open here


//protoypes
void scheduler(sem_t *sem_id, int n);

int main()
{	
	char buffer[1024];
	FILE *fp; 
	int parentPid = getpid();
	unsigned long long int tempCycle; 
	int tempMem;
	
	/* Intializes random number generator */
	time_t t;
	srand((unsigned) time(&t));
	
	//open file to store generated processes
	fp = fopen("randomProcesses.txt", "w+");
	
	//generate random processes to read from file
	for (int i = 0; i < NUM_OF_PROCESSES; i++)
	{
		// get random time and memory for this process
		tempCycle = rand() % (UPPER_CYCLE - LOWER_CYCLE) + LOWER_CYCLE;
		tempMem = rand() % (UPPER_MEMORY - LOWER_MEMORY) + LOWER_MEMORY;
		fprintf(fp, "p%d %llu %d\n", i, tempCycle, tempMem);
	}
	
	//close file
	fclose(fp);
	
	//create semaphore
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
		   scheduler(sem_id, NUM_OF_PROCESSES);
		   exit(0);
		}
		// control reaches this point only in the parent
	}
	
	// the parent waits for all the child processes to finish
	while ((wpid = wait(&status)) > 0); 

    return 0;
}


/*
 * functional loop where the 5 children processes will go into and begin to 
 * read the processes in randomProcesses.txt and then schdule which processor
 * will run which process
 *
 *
 */
void scheduler(sem_t *sem_id, int n){
	
	int numProcesses = n;
	
	while (numProcesses > 0){
		numProcesses = 0;
	}
	
}


// how to open and close file 
	/*
	//open file
	int fd;
	fd = open ("file.txt", O_WRONLY);
	if (fd == -1)
	{
		perror("file access");
		exit(EXIT_FAILURE);
	}
	//close file
	close(fd);
	*/

