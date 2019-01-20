#ifndef _threads_h_
#define _threads_h_



struct WorkQueue;
#define WORK_QUEUE_CALLBACK(name) void name(WorkQueue *queue, void *data)
typedef WORK_QUEUE_CALLBACK(WorkQueueCallback);

struct WorkQueueEntry
{
	WorkQueueCallback* callback;
	void* data;
};

struct WorkQueue
{
	u32 volatile completionGoal;
	u32 volatile completionCount;

	u32 volatile nextEntryToWrite;
	u32 volatile nextEntryToRead;

	HANDLE semaphore;

	WorkQueueEntry entries[4096];
};

internal b32 executeNextWorkQueueEntry(WorkQueue* queue)
{
	b32 shouldSleep = false;

	u32 originalNextEntryToRead = queue->nextEntryToRead;
	u32 newNextEntryToRead = (originalNextEntryToRead + 1) % ArrayCount(queue->entries);
	if (originalNextEntryToRead != queue->nextEntryToWrite)
	{
		u32 index = InterlockedCompareExchange((LONG volatile *)&queue->nextEntryToRead, newNextEntryToRead, originalNextEntryToRead);
		if (index == originalNextEntryToRead)
		{
			WorkQueueEntry entry = queue->entries[index];
			entry.callback(queue, entry.data);
			InterlockedIncrement((LONG volatile *)&queue->completionCount);
		}
	}
	else
	{
		shouldSleep = true;
	}

	return shouldSleep;
}

internal void finishWorkQueue(WorkQueue* queue)
{
	while (queue->completionGoal != queue->completionCount)
	{
		executeNextWorkQueueEntry(queue);
	}

	queue->completionGoal = 0;
	queue->completionCount = 0;
}

internal void addEntryToWorkQueue(WorkQueue* queue, WorkQueueCallback* callback, void* data)
{
	// TODO: Switch to InterlockedCompareExchange eventually so that any thread can add ?
	u32 newNextEntryToWrite = (queue->nextEntryToWrite + 1) % ArrayCount(queue->entries);
	while (newNextEntryToWrite == queue->nextEntryToRead) { executeNextWorkQueueEntry(queue); }
	WorkQueueEntry *entry = queue->entries + queue->nextEntryToWrite;
	entry->callback = callback;
	entry->data = data;
	++queue->completionGoal;
	_WriteBarrier();
	queue->nextEntryToWrite = newNextEntryToWrite;
	ReleaseSemaphore(queue->semaphore, 1, 0);
}

internal void cleanWorkQueue(WorkQueue* queue, volatile u32* stopper)
{
	*stopper = true;
	finishWorkQueue(queue); // Ensure that the index has been loaded.
	_WriteBarrier();
	*stopper = false;
}



struct ThreadData
{
	WorkQueue* queue;
};

internal DWORD worker_thread(void* _data)
{
	ThreadData* data = (ThreadData*)_data;
	WorkQueue* queue = data->queue;

	u32 threadid = GetCurrentThreadId();
	for (;;)
	{
		while (executeNextWorkQueueEntry(queue))
		{
			WaitForSingleObjectEx(queue->semaphore, INFINITE, FALSE);
		}
	}

	return 0;
}

inline void createWorkerThread(ThreadData* threadData, WorkQueue* queue)
{
	threadData->queue = queue;
	HANDLE threadHandle = CreateThread(0, 0, worker_thread, threadData, 0, 0);
	CloseHandle(threadHandle);
}

internal i32 getNumberOfLogicalThreads(SYSTEM_INFO* infos = 0)
{
	SYSTEM_INFO _sysinfos = {};
	if (!infos)
	{
		GetSystemInfo(&_sysinfos);
		infos = &_sysinfos;
	}
	i32 nbLogicalCores = infos->dwNumberOfProcessors;
	return nbLogicalCores;
}


struct ThreadPool
{
	WorkQueue queue = {};
	i32 nbThreads = 0;
	ThreadData* data = 0;
};

internal void initThreadPool(MemoryArena& arena, ThreadPool& pool)
{
	pool = {};
	pool.nbThreads = getNumberOfLogicalThreads();
	pool.data = pushArray(arena, ThreadData, pool.nbThreads);
	pool.queue.semaphore = CreateSemaphoreEx(0, 0, pool.nbThreads, 0, 0, SEMAPHORE_ALL_ACCESS);
	for (i32 i = 0; i < pool.nbThreads; ++i)
	{
		createWorkerThread(pool.data + i, &pool.queue);
	}
}


#endif
