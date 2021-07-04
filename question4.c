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
// gcc -pthread question4.c

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

//arrival time bounds (expressed in seconds)
#define UPPER_TIME 172800
#define LOWER_TIME 1

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
	unsigned int arrivalTime;
};

/*
 Array Implementation of MinHeap data Structure
*/

// max heap size
#define HEAP_SIZE 200

struct Heap{
    struct processes arr[NUM_OF_PROCESSES];
    int count;
    int capacity;
};

typedef struct Heap Heap;

// heap methods
Heap *CreateHeap(int capacity, Heap *h);
void insert(Heap *h, struct processes key);
int isEmpty(Heap *h);
void print(Heap *h);
void heapify_bottom_top(Heap *h,int index);
void heapify_top_bottom(Heap *h, int parent_node);
struct processes PopMin(Heap *h); 

//protoypes
double scheduler(sem_t *sem_id, int *numProcesses, Heap *h);
void taskGiver(struct processes prosArr[], Heap *h, sem_t *sem_id);
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
	unsigned int tempArrival;
	struct processes prosArr[NUM_OF_PROCESSES];
	
	//SHARED MEMORY
	// time each processor spent running
	double *totalTime = mmap(NULL, sizeof *totalTime, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	*totalTime = 0;
	int *numProcesses = mmap(NULL, sizeof *numProcesses, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	*numProcesses = NUM_OF_PROCESSES;

	// shared heap
	Heap *h = mmap(NULL, sizeof(Heap), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	CreateHeap(HEAP_SIZE, h);
	
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
		tempArrival = rand() % (UPPER_TIME - LOWER_TIME) + LOWER_TIME;
		fprintf(fp, "p%d\t burst time: %llu\t\t memory requirement: %d\n", i, tempCycle, tempMem);
		
		// sort this later
		prosArr[i].cycleTime = tempCycle;
		prosArr[i].memory = tempMem;
		prosArr[i].arrivalTime = tempArrival;
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
	
	for (int i = 0; i < 5; i++) 
	{
		child_pid = fork();
		// the linux machine will sometimes decide not to give us enough forks, the philosophers are starving, just run again
		if (child_pid == 0) 
		{
		   //call scheduling child function here
		   *totalTime += scheduler(sem_id, numProcesses, h);
		   exit(0);
		}
		else if (child_pid < 0)
		{
			printf("Child  : [fork] Failed\n");
			exit(EXIT_FAILURE);
		}
		//control reaches this point only in the parent
	}
	
	//schedule tasks in heap
	taskGiver(prosArr, h, sem_id);
	
	//the parent waits for all the child processes to finish
	while ((wpid = wait(&status)) > 0); 
	
	//get total time it took to run
	printf("time to run through all processes + time processes spent in a queue: %f\n", *totalTime);
	
	//put in recognizable terms 
	convertSectoDay((unsigned long long int)*totalTime);
	
	//free memory
	shmdt(numProcesses);
	shmdt(totalTime);

    return 0;
}

/*
 * functional loop where the 5 children processes will go into and begin to 
 * read the processes from memory and then schdule which processor
 * will run which process
 */
double scheduler(sem_t *sem_id, int *numProcesses, Heap *h)
{
	double timeToRun = 0;       //keep track of wait time + execution time of everything that ran on this processor
	double execTime = 0;
	double waitList = 0;
	struct processes temp;

	while (*numProcesses > 0)
	{
		
		// if semaphore available, then continue
		if (sem_wait(sem_id) < 0)
			printf("%d  : [sem_wait] Failed\n", getpid());
		// THIS IS CRITICAL SECTION
		
		
		//check if process available
		if (isEmpty(h) == 1)
		{
			
			//release control of semaphore
			if (sem_post(sem_id) < 0)
				printf("%d   : [sem_post] Failed \n", getpid());
			
			waitList = 0;
			continue;
		}
		
		
		if (*numProcesses <= 0)
		{
			if (sem_post(sem_id) < 0)
				printf("%d   : [sem_post] Failed \n", getpid());
			return timeToRun;
		}
		
		// get next item in the heap
		temp = PopMin(h);
		
		printf("name: %s  \ttime: %llu   \tmem: %d   \tarrival Time: %d   \tpid: %d\n", temp.name, temp.cycleTime, temp.memory, temp.arrivalTime, getpid());
		*numProcesses = *numProcesses - 1;
		
		// calulate timeToRun
		execTime = (double)temp.cycleTime / GHZ;
		execTime += waitList;
		timeToRun += execTime;
	
		//release control of semaphore
		if (sem_post(sem_id) < 0)
			printf("%d   : [sem_post] Failed \n", getpid());
		
		//sleep for last timeToRun /1000
		usleep(execTime * 100);
		
	}	
	
	return timeToRun;
}

/*
 * add tasks to the max heap 
 * when they become available
 */
void taskGiver(struct processes prosArr[], Heap *h, sem_t *sem_id)
{
	
	int currentTime = 0;
	int wait = 0;
	
	//release control of semaphore
	if (sem_post(sem_id) < 0)
		printf("%d   : [sem_post] Failed \n", getpid());
	
	for(int i = 0; i < NUM_OF_PROCESSES; i++)
	{
		if (prosArr[i].arrivalTime <= currentTime)
		{
			insert(h, prosArr[i]);
		} 
		else 
		{
			wait = abs(currentTime - prosArr[i].arrivalTime);
			usleep(wait * 100);
			currentTime += wait;
			insert(h, prosArr[i]);
		}
		
	}
	printf("all added\n");
}

/*
 * take in the arr of processes 
 * and sort based on arrival time
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
        while (j >= 0 && prosArr[j].arrivalTime > key.arrivalTime)
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
		printf("%s  time: %llu   mem: %d   arrival time: %d\n", prosArr[i].name, prosArr[i].cycleTime, prosArr[i].memory, prosArr[i].arrivalTime);
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

// HEAP methods--------------------------------------------------------------

/*
 * Heap constructor
 * allocate space for a new heap
 */
Heap *CreateHeap(int capacity, Heap *h)
{

    //check if memory allocation is fails
    if(h == NULL)
	{
        printf("Memory Error!");
        return NULL;
    }
    h->count=0;
    h->capacity = capacity;
    //h->arr = (struct processes *) malloc(capacity*sizeof(struct processes));

    //check if allocation succeed
    if ( h->arr == NULL)
	{
        printf("Memory Error!");
        return NULL;
    }
    return h;
}

/*
 * insert a new process into the heap
 */
void insert(Heap *h, struct processes key)
{
    if( h->count < h->capacity){
        h->arr[h->count] = key;
        heapify_bottom_top(h, h->count);
        h->count++;
    }
}

/*
 * boolean function to check if 
 * the heap is currntly empty
 */
int isEmpty(Heap *h)
{
    if(h->count==0)
	{
        return 1;
    }
	return 0;
}

/*
 * read just heap to meet min heap requirements
 * meant for after you insert a new item
 */
void heapify_bottom_top(Heap *h,int index)
{
    struct processes temp;
    int parent_node = (index-1)/2;

    if(h->arr[parent_node].cycleTime > h->arr[index].cycleTime)
	{
        //swap and recursive call
        temp = h->arr[parent_node];
        h->arr[parent_node] = h->arr[index];
        h->arr[index] = temp;
        heapify_bottom_top(h,parent_node);
    }
}

/*
 * readjust heap to meet min heap requirements
 * meant for after popping the minimum element off
 */
void heapify_top_bottom(Heap *h, int parent_node)
{
    int left = parent_node*2+1;
    int right = parent_node*2+2;
    int min;
    struct processes temp;

    if(left >= h->count || left <0)
        left = -1;
    if(right >= h->count || right <0)
        right = -1;

    if(left != -1 && h->arr[left].cycleTime < h->arr[parent_node].cycleTime)
        min=left;
    else
        min =parent_node;
    if(right != -1 && h->arr[right].cycleTime < h->arr[min].cycleTime)
        min = right;

    if(min != parent_node)
	{
        temp = h->arr[min];
        h->arr[min] = h->arr[parent_node];
        h->arr[parent_node] = temp;

        // recursive  call
        heapify_top_bottom(h, min);
    }
}

/*
 * pop off the smallest item from the 
 * heap and return  the process
 */
struct processes PopMin(Heap *h)
{
    struct processes pop;
    if(h->count==0)
	{
        printf("\n__Heap is Empty__\n");
        return pop;
    }
    // replace first node by last and delete last
    pop = h->arr[0];
    h->arr[0] = h->arr[h->count-1];
    h->count--;
    heapify_top_bottom(h, 0);
    return pop;
}
