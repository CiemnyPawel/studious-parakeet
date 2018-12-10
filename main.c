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

unsigned int Global_BufferLength = 0;
unsigned int Global_NumberOfProducers = 0;
unsigned int Global_NumberOfBuffers=0;
unsigned int Global_MessagePerProbe = 0;

struct Buffer // Buffer
{
	unsigned int * intBuffer;
	unsigned int start, end;
};

void BufferInsertElement(struct Buffer * pointerToBuffer, unsigned int Element) // Done
{
	pointerToBuffer->intBuffer[pointerToBuffer->end] = Element;
	pointerToBuffer->end++;
	pointerToBuffer->end %= Global_BufferLength;
}

unsigned int BufferGetElement(struct Buffer * pointerToBuffer) // Done
{
	unsigned int Element = pointerToBuffer->intBuffer[pointerToBuffer->start];
	pointerToBuffer->start++;
	pointerToBuffer->start %= Global_BufferLength;
	return Element;
}

struct Buffer * BindBuffers() // Done
{
	static int ShmId = 0;
	if(ShmId == 0)
		ShmId = shmget(IPC_PRIVATE, Global_NumberOfBuffers * sizeof(struct Buffer) + Global_NumberOfBuffers * Global_QueueLength * sizeof(unsigned int), SHM_W | SHM_R);

	if(ShmId <= 0)
	{
		printf("shmget failed...\n");
		abort();
	}
	void * Data = shmat(ShmId, NULL, 0);

	struct Buffer * Buffers = (struct Buffer *) Data;
	for(size_t i = 0; i < Global_NumberOfBuffers)
		Buffers[i].intBuffer = Data + Global_NumberOfBuffers * sizeof(struct Buffer) + I * Global_QueueLength * sizeof(unsigned int);

	return Buffers;
}
struct PriorityQueue * InitQueues() // TODO
{
	struct PriorityQueue * Queues = BindQueues();
	memset(Queues, 0, N_PRIORITIES * sizeof(struct PriorityQueue));
	return Queues;
}

struct ProjectSemaphores // Done
{
	sem_t BufferLock[Global_NumberOfBuffers];
	sem_t BufferFreeSpace[Global_NumberOfBuffers];
	sem_t DataInQueues;
};
struct ProjectSemaphores * BindSemaphores() // TODO
{
	static int ShmId = 0;
	if(ShmId == 0)
		ShmId = shmget(IPC_PRIVATE, sizeof(struct ProjectSemaphores), SHM_W | SHM_R);

	if(ShmId <= 0)
	{
		printf("shmget failed...\n");
		abort();
	}
	return (struct ProjectSemaphores *) shmat(ShmId, NULL, 0);
}

struct ProjectSemaphores * InitSemaphores() // TODO
{
	struct ProjectSemaphores * PS = BindSemaphores();
	for(unsigned int I = 0; I < N_PRIORITIES; I++)
	{
		sem_init(&PS->QueueLock[I], 1, 1);
		sem_init(&PS->QueueFreeSpace[I], 1, Global_QueueLength);
	}
	sem_init(&PS->DataInQueues, 1, 0);
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
	unsigned int Ret;
	unsigned int X = fread((char *) &Ret, 1, sizeof(unsigned int), F);
	fclose(F);

	return Ret;
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
void ServiceCenter() // TODO
{
	printf("[ServiceCenter] Poczatek zycia\n");

	struct PriorityQueue * Queues = BindQueues();
	struct ProjectSemaphores * Semaphores = BindSemaphores();

	unsigned int AlarmsProcessed = 0;
	while(AlarmsProcessed < (Global_LowPriorityProbes + Global_HighPriorityProbes) * Global_AlarmsPerProbe)
	{
		usleep((IndepRand() % 500000));

		sem_wait(&Semaphores->DataInQueues);
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
		}

		sem_wait(&Semaphores->QueueLock[QueueId]);
		unsigned int R = QueueGetElement(&Queues[QueueId]);
		sem_post(&Semaphores->QueueLock[QueueId]);
		sem_post(&Semaphores->QueueFreeSpace[QueueId]);

		printf("[ServiceCenter] Sonda z kolejki %d zglasza sygnal: |%d|\n", QueueId, R);

		AlarmsProcessed++;
	}

	printf("[ServiceCenter] Koniec zycia\n");
}
void Producer(unsigned short QueueId) // TODO
{
	unsigned short MyId = getpid();
	printf("Start zycia producenta: %d\n", MyId);
	struct PriorityQueue * Queues = BindQueues();
	struct ProjectSemaphores * Semaphores = BindSemaphores();

	unsigned int SentAlarms = 0;
	while(SentAlarms < Global_AlarmsPerProbe)
	{
		usleep((IndepRand() % 500000));
		sem_wait(&Semaphores->QueueFreeSpace[QueueId]);

		sem_wait(&Semaphores->QueueLock[QueueId]);
		unsigned int SignalId = IndepRand() % 10;
		unsigned int AlarmId = MyId * 1000 + SignalId;
		QueueInsertElement(&Queues[QueueId], AlarmId);
		printf("[Kolejka: %d] Zglaszam sygnal %d, moje id to: %d = numer alarmu: |%d|\n", QueueId, SignalId, MyId, AlarmId);
		sem_post(&Semaphores->QueueLock[QueueId]);

		sem_post(&Semaphores->DataInQueues);

		SentAlarms++;
	}

	printf("[Kolejka: %d] Koniec zycia sondy: %d\n", QueueId, MyId);
}
int main(unsigned int ArgC, char ** ArgV) // Done
{
	if(ArgC != 5)
	{
		printf("%s Dlugosc buforow/ Ilosc buforow/ Ilosc producentow/ Ilosc produktow na 1 producenta", ArgV[0]);
		return 1;
	}
	Global_BufferLength = atoi(ArgV[1]);
	Global_NumberOfBuffers = atoi(ArgV[2]);
	Global_NumberOfProducers = atoi(ArgV[3]);
	Global_MessagePerProbe = atoi(ArgV[4]);

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
