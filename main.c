#include <unistd.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/wait.h>

#include <stdlib.h>
#include <string.h>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <semaphore.h>

#include <time.h>

#define N_BUFFERS 10

unsigned int Global_BufferLength = 0;
unsigned int Global_NumberOfProducers = 0;
unsigned int Global_MessagesPerProducer = 0;

struct Buffer // Buffer
{
	unsigned int * intBuffer;
	unsigned int start, end;
};

void BufferInsertElement(struct Buffer * pointerToBuffer, unsigned int element) // Done
{
	pointerToBuffer->intBuffer[pointerToBuffer->end] = element;
	pointerToBuffer->end++;
	pointerToBuffer->end %= Global_BufferLength;
}

unsigned int BufferGetElement(struct Buffer * pointerToBuffer) // Done
{
	unsigned int element = pointerToBuffer->intBuffer[pointerToBuffer->start];
	pointerToBuffer->start++;
	pointerToBuffer->start %= Global_BufferLength;
	return element;
}

struct Buffer * BindBuffers() // Done
{
	static int shmId = 0;
	if(shmId == 0)
		shmId = shmget(IPC_PRIVATE, N_BUFFERS * sizeof(struct Buffer) + N_BUFFERS * Global_BufferLength * sizeof(unsigned int), SHM_W | SHM_R);

	if(shmId <= 0)
	{
		printf("shmget failed...\n");
		abort();
	}
	void * data = shmat(shmId, NULL, 0);

	struct Buffer * buffers = (struct Buffer *) data;
	for(size_t i = 0; i < N_BUFFERS; i++)
		buffers[i].intBuffer = data + N_BUFFERS * sizeof(struct Buffer) + i * Global_BufferLength * sizeof(unsigned int);

	return buffers;
}

struct Buffer * InitBuffers() // Done
{
	struct Buffer * buffers = BindBuffers();
	memset(buffers, 0, N_BUFFERS * sizeof(struct Buffer));
	return buffers;
}

struct ProjectSemaphores // Done
{
	sem_t BufferLock[N_BUFFERS];
	sem_t BufferFreeSpace[N_BUFFERS];
	sem_t isThereAnyDataInBuffers;
};
struct ProjectSemaphores * BindSemaphores() // Done
{
	static int shmId = 0;
	if(shmId == 0)
		shmId = shmget(IPC_PRIVATE, sizeof(struct ProjectSemaphores), SHM_W | SHM_R);

	if(shmId <= 0)
	{
		printf("shmget failed...\n");
		abort();
	}
	return (struct ProjectSemaphores *) shmat(shmId, NULL, 0);
}

struct ProjectSemaphores * InitSemaphores() // Done
{
	struct ProjectSemaphores * PS = BindSemaphores();
	for(size_t i = 0; i < N_BUFFERS; i++)
	{
		sem_init(&PS->BufferLock[i], 1, 1);
		sem_init(&PS->BufferFreeSpace[i], 1, Global_BufferLength);
	}
	sem_init(&PS->isThereAnyDataInBuffers, 1, 0);
	return PS;
}

unsigned int IndepRand() // Done
{
	FILE * F = fopen("/dev/urandom", "r");
	if(!F)
	{
		printf("Cannot open urandom...\n");
		abort();
	}
	unsigned int randomValue;
	fread((char *) &randomValue, 1, sizeof(unsigned int), F);
	fclose(F);

	return randomValue;
}
// Funkcja której zadaniem jest uruchomić nowy proces, wykonać zadaną funkcję i zakończyć żywot
void CreateSubProc(void (*JumpFunction)()) // Done
{
	int ForkResult = fork();
	if(ForkResult == 0) // execute only if we are child
	{
		JumpFunction();
		exit(0);
	}
}
void Consumer() // Done
{
	printf("Consumer has started\n");

	struct Buffer * buffers = BindBuffers();
	struct ProjectSemaphores * semaphores = BindSemaphores();

	size_t messagesAlreadyProceed = 0;
	while(messagesAlreadyProceed < Global_MessagesPerProducer * Global_NumberOfProducers)
	{
		usleep((IndepRand() % 500000));
		sem_wait(&semaphores->isThereAnyDataInBuffers);

		unsigned int randomBuffer = 0;
		unsigned int freeSpaceInBuffer = Global_BufferLength;
		while(1==1)
		{
			randomBuffer = IndepRand() % N_BUFFERS;
			sem_wait(&semaphores->BufferLock[randomBuffer]);
			sem_getvalue(&semaphores->BufferFreeSpace[randomBuffer], &freeSpaceInBuffer);
			if(freeSpaceInBuffer < Global_BufferLength)
				break;
		}
		/*
		int QueueId = -1;
		for(unsigned int I = 0; I < N_PRIORITIES; I++)
		{
			//Lock mutex
			sem_wait(&Semaphores->QueueLock[I]);

			int QueueFreeSpace;
			sem_getvalue(&Semaphores->QueueFreeSpace[I], &QueueFreeSpace);

			if(QueueFreeSpace < Global_QueueLength)
				QueueId = I;

			//Unlock mutex
			sem_post(&Semaphores->QueueLock[I]);

			if(QueueId >= 0)
				break;
		}*/

		sem_wait(&semaphores->BufferLock[randomBuffer]);
		unsigned int element = BufferGetElement(&buffers[randomBuffer]);
		sem_post(&semaphores->BufferLock[randomBuffer]);
		sem_post(&semaphores->BufferFreeSpace[randomBuffer]);

		printf("Consumer has eaten element from buffer nr: %d\n", randomBuffer);
		messagesAlreadyProceed++;
	}

	printf("Consumer processed all messages\n");
}

unsigned int SearchEmptiestBuffer()
{
	struct ProjectSemaphores * semaphores = BindSemaphores();
	unsigned int freeSpaceInBuffer = 0;
	unsigned int emptiestBuffer = 0;
	unsigned int numberOfEmptiestBuffer = 0;
	for(size_t i =0; i < N_BUFFERS; i++)
	{
		sem_wait(&semaphores->BufferLock[i]);
		sem_getvalue(&semaphores->BufferFreeSpace[i], &freeSpaceInBuffer);
		if(freeSpaceInBuffer > emptiestBuffer)
		{
			emptiestBuffer = freeSpaceInBuffer;
			numberOfEmptiestBuffer = (unsigned int) i;
		}
	}

	for(size_t i = 0; i < N_BUFFERS; i++)
		sem_post(&semaphores->BufferLock[i]);

	return numberOfEmptiestBuffer;
}

void Producer(unsigned short QueueId) // TODO
{
	unsigned short myId = getpid();
	printf("Producer nr: %d has started\n", myId);
	struct Buffer * buffers = BindBuffers();
	struct ProjectSemaphores * semaphores = BindSemaphores();

	unsigned int emptiestBuffer = 0;
	unsigned int messagesAlreadySent = 0;
	unsigned int message = 0;

	while(messagesAlreadySent < Global_MessagesPerProducer)
	{
		usleep((IndepRand() % 500000));
		emptiestBuffer = SearchEmptiestBuffer();

		sem_wait(&semaphores->BufferFreeSpace[emptiestBuffer]);
		sem_wait(&semaphores->BufferLock[emptiestBuffer]);
		message = myId * 100 + IndepRand() % 1000;
		BufferInsertElement(&buffers[emptiestBuffer], message);
		printf("Producer nr: %d commit message: %d to buffer nr: %d\n", myId, message, emptiestBuffer);
		sem_post(&semaphores->BufferLock[emptiestBuffer]);

		sem_post(&semaphores->isThereAnyDataInBuffers);

		messagesAlreadySent++;
	}

	printf("Producer nr: %d has finished his job\n", myId);
}
int main(unsigned int ArgC, char ** ArgV) // Done
{
	if(ArgC != 5)
	{
		printf("%s Dlugosc buforow/ Ilosc buforow/ Ilosc producentow/ Ilosc produktow na 1 producenta", ArgV[0]);
		return 1;
	}
	Global_BufferLength = atoi(ArgV[1]);
	//N_BUFFERS = atoi(ArgV[2]);
	Global_NumberOfProducers = atoi(ArgV[3]);
	Global_MessagesPerProducer = atoi(ArgV[4]);

	//Create shared memory
	InitBuffers();
	InitSemaphores();

	//Launch consumer
	CreateSubProc(&Consumer);
	//Launch producers
	for(size_t i = 0; i < Global_NumberOfProducers; i++)
		CreateSubProc(&Producer);

	//Work until there are children
	while(wait(NULL) > 0) {}
	return 0;
}
