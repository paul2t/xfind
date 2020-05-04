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

	u32 volatile should_stop;

	HANDLE semaphore;

	WorkQueueEntry* entries;
	i32 entries_count;
};

internal b32 executeNextWorkQueueEntry(WorkQueue* queue)
{
	if (queue->should_stop) return true;
	b32 shouldSleep = false;

	u32 originalNextEntryToRead = queue->nextEntryToRead;
	u32 newNextEntryToRead = (originalNextEntryToRead + 1) % queue->entries_count;
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
		if (queue->should_stop) break;
		executeNextWorkQueueEntry(queue);
	}

	queue->completionGoal = 0;
	queue->completionCount = 0;
}

internal void addEntryToWorkQueue(WorkQueue* queue, WorkQueueCallback* callback, void* data)
{
	if (queue->should_stop) return;
	// TODO: Switch to InterlockedCompareExchange eventually so that any thread can add ?
	u32 newNextEntryToWrite = (queue->nextEntryToWrite + 1) % queue->entries_count;
	while (newNextEntryToWrite == queue->nextEntryToRead) { executeNextWorkQueueEntry(queue); }
	WorkQueueEntry *entry = queue->entries + queue->nextEntryToWrite;
	entry->callback = callback;
	entry->data = data;
	InterlockedIncrement(&queue->completionGoal);
	_WriteBarrier();
	queue->nextEntryToWrite = newNextEntryToWrite;
	ReleaseSemaphore(queue->semaphore, 1, 0);
}

internal void cleanWorkQueue(WorkQueue* queue)
{
	TIMED_FUNCTION();
	queue->should_stop = true;
	_WriteBarrier();
	while (queue->completionGoal != queue->completionCount)
	{
		int r = queue->nextEntryToRead;
		int w = queue->nextEntryToWrite;
		if (r == w) continue;
		int value = InterlockedCompareExchange(&queue->nextEntryToRead, w, r);
		if (r == value)
		{
			if (w < r) w += queue->entries_count;
			InterlockedAdd((volatile LONG*)&queue->completionCount, w - r);
		}
	}
	queue->completionGoal = 0;
	queue->completionCount = 0;
	_WriteBarrier();
	queue->should_stop = false;
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

// @param nbThreads If 0, then creates as many threads as the number of logical threads.
internal void initThreadPool(MemoryArena& arena, ThreadPool& pool, i32 nbThreads = 0)
{
	pool = {};
	pool.queue.entries_count = 16 * 4096;
	pool.queue.entries = pushArray(arena, WorkQueueEntry, pool.queue.entries_count);
	pool.nbThreads = nbThreads > 0 ? nbThreads : getNumberOfLogicalThreads();
	pool.data = pushArray(arena, ThreadData, pool.nbThreads);
	pool.queue.semaphore = CreateSemaphoreEx(0, 0, pool.nbThreads, 0, 0, SEMAPHORE_ALL_ACCESS);
	for (i32 i = 0; i < pool.nbThreads; ++i)
	{
		createWorkerThread(pool.data + i, &pool.queue);
	}
}


#endif
