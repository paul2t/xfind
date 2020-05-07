
#if APP_INTERNAL

inline void DEBUG_TEST_TIMER(u32 test, volatile u64& start, volatile u64& time)
{
	if (test && start)
	{
		time = GetTickCount64() - start;
		start = 0;
	}
}

#else
#define DEBUG_TEST_TIMER(...)
#endif

volatile u32 indexingInProgress;
volatile u32 searchInProgress;

#define MAX_FILE_PARSED_SIZE (MegaBytes(1)-1)

internal WORK_QUEUE_CALLBACK(workerReloadFileToMemory)
{
	if (queue->should_stop) return;
	// Not thread safe TIMED_FUNCTION();

	FileIndexEntry* fileIndex = (FileIndexEntry*)data;
	FILETIME lastWriteTime = GetLastWriteTime(fileIndex->path.str);
	if (CompareFileTime(&fileIndex->lastWriteTime, &lastWriteTime))
	{
		fileIndex->lastWriteTime = lastWriteTime;

		fileIndex->content.size = 0;
		FILE* file = fopen(fileIndex->path.str, "rb");
		if (file)
		{
			fileIndex->truncated = false;
			_fseeki64(file, 0, SEEK_END);
			size_t newSize = _ftelli64(file);
			if (newSize > MAX_FILE_PARSED_SIZE)
			{
				fileIndex->truncated = true;
				newSize = MAX_FILE_PARSED_SIZE;
			}
			rewind(file);

			if (fileIndex->content.memory_size < newSize + 1)
			{
				fileIndex->content.memory_size = (i32)(newSize * (fileIndex->content.str ? 1.5f : 1.0f)) + 1;
				if (fileIndex->content.memory_size > MAX_FILE_PARSED_SIZE + 1)
					fileIndex->content.memory_size = MAX_FILE_PARSED_SIZE + 1;
				fileIndex->content.str = (char*)realloc(fileIndex->content.str, fileIndex->content.memory_size);
			}

			memid nitemsRead = fread(fileIndex->content.str, 1, newSize, file);
			fileIndex->content.size = (i32)nitemsRead;
			terminate_with_null(&fileIndex->content);
			fclose(file);
		}

		fileIndex->modifiedSinceLastSearch = true;
	}
	_WriteBarrier();
	u32 test = InterlockedDecrement(&indexingInProgress);
	DEBUG_TEST_TIMER(!test, indexTimeStart, indexTime);
}

internal WORK_QUEUE_CALLBACK(workerComputeIndex)
{
	if (queue->should_stop) return;
	// Not thread safe TIMED_FUNCTION();

#if APP_INTERNAL
	treeTraversalTimeStart = GetTickCount64();
#endif

	State* state = (State*)data;
	String* searchPaths = state->searchPaths;
	i32 searchPathsSize = state->searchPathsSize;
	FileIndex* fileIndex = &state->index;

	bool locked = true;
	while (!TryLockMutex(&fileIndex->mutex.write))
	{
		if (queue->should_stop)
		{
			locked = false;
			break;
		}
	}
	
	if (locked)
	{
		while (fileIndex->mutex.read)
		{
			if (queue->should_stop)
			{
				UnlockMutex(&fileIndex->mutex.write);
				locked = false;
				break;
			}
		}
	}

	if (locked)
	{
		String* searchExtensions = state->extensions;
		i32 searchExtensionsSize = state->extensionsSize;

		// TODO(xf4): make a separate array. Might be faster ?!?!
		for (FileIndexEntry* ei = fileIndex->firstFile; ei; ei = ei->next)
			ei->seenInIndex = false;

		for (i32 pi = 0; pi < searchPathsSize; ++pi)
		{
			if (queue->should_stop) break;
			char _pathbuffer[4096];
			String searchPath = make_fixed_width_string(_pathbuffer);
			copy(&searchPath, searchPaths[pi]);
			for (int ci = 0; ci < searchPath.size; ++ci) if (searchPath.str[ci] == '/') searchPath.str[ci] = '\\';
			if (char_is_slash(searchPath.str[searchPath.size - 1]))
				searchPath.size--;

			// Path already indexed
			if (findStringInArrayInsensitive(searchPaths, pi, searchPath))
				continue;

			append(&searchPath, "\\*");
			terminate_with_null(&searchPath);

			Directory stack[1024];
			int searchPathSizeStack[1024];
			searchPathSizeStack[0] = 0;
			Directory* current = stack;
			dfind(current, searchPath.str);
			searchPath.size--; // remove the '*'
			i32 stackSize = 1;
			while (stackSize > 0)
			{
				if (queue->should_stop) break;
				if (current->found)
				{
					if (!isHidden(current) || state->config.showHiddenFiles)
					{
						if (isDir(current))
						{
							if (stackSize < ArrayCount(stack))
							{
								searchPathSizeStack[stackSize - 1] = searchPath.size;
								append(&searchPath, current->name);

								// No already indexed
								if (!findStringInArrayInsensitive(searchPaths, searchPathsSize, searchPath))
								{
									append(&searchPath, "\\*");
									terminate_with_null(&searchPath);
									current = stack + stackSize;
									stackSize++;
									dfind(current, searchPath.str);
									searchPath.size--; // remove the '*'
									continue;
								}
								searchPath.size = searchPathSizeStack[stackSize - 1];
							}
						}
						else
						{
							String filename = make_string_slowly(current->name);
							String fileext = file_extension(filename);
							// Is valid extension.
							if (findStringInArrayInsensitive(searchExtensions, searchExtensionsSize, fileext))
							{
								String file = searchPath;
								append(&file, filename);
								terminate_with_null(&file);

								FileIndexEntry* fileEntry = getOrCreateFileFromPath(fileIndex, file, searchPaths[pi].size + 1, 1);
								fileEntry->seenInIndex = true;

								InterlockedIncrement(&indexingInProgress);
								addEntryToWorkQueue(queue, workerReloadFileToMemory, fileEntry);
							}
						}
					}
				}
				else
				{
					dclose(current);
					stackSize--;
					if (stackSize <= 0)
						break;
					current = stack + stackSize - 1;
					searchPath.size = searchPathSizeStack[stackSize - 1];
				}
				dnext(current);
			}

			while (stackSize > 0)
			{
				dclose(stack + stackSize - 1);
				--stackSize;
			}
		}

		// NOTE(xf4): Remove files that haven't been seen while searching.
		i32 filesSize = 0;
		FileIndexEntry _tmpe = {};
		for (FileIndexEntry* ei = fileIndex->firstFile, **prevnext = &fileIndex->firstFile; ei; ei = ei->next)
		{
			if (queue->should_stop) break;
			++filesSize;
			if (!ei->seenInIndex)
			{
				ScopeMutexWrite(&ei->mutex);
				--filesSize;

				if (ei == fileIndex->firstFile)
					AtomicListPopFirst(fileIndex->firstFile);

				u32 hid = hash(ei->path) % fileIndex->filePathHashSize;
				FileIndexEntry** firstH = &fileIndex->filePathHash[hid];
				for (; *firstH; firstH = &((*firstH)->nextInPathHash))
				{
					if (match((*firstH)->path, ei->path))
					{
						auto** nextH = &(*firstH)->nextInPathHash;
						*firstH = *nextH;
						firstH = nextH;
						break;
					}
				}
				ei->nextInPathHash = 0;
				FileIndexEntry* nexte = ei->next;

				freeFileIndexEntry(ei);

				_tmpe.next = nexte;
				*prevnext = nexte;
				ei = &_tmpe;
			}
			else
			{
				prevnext = &ei->next;
			}
		}

		fileIndex->filesSize = filesSize;

		UnlockMutexWrite(&fileIndex->mutex);
	}

	u32 test = InterlockedDecrement(&indexingInProgress);
	DEBUG_TEST_TIMER(!test, indexTimeStart, indexTime);

#if APP_INTERNAL
	treeTraversalTime = GetTickCount64() - treeTraversalTimeStart;
#endif
	post_empty_event(&state->waiting_for_event);
}

internal void stopFileIndex(State* state)
{
	TIMED_FUNCTION();
	cleanWorkQueue(&state->pool.queue);

	indexingInProgress = 0;
	//state->index.filesSize = 0;
	//state->index.firstFile = 0;
	//state->index.arena.Release();
	//state->index.onePastLastFile = &state->index.firstFile;
}

internal void computeFileIndex(State* state)
{
	TIMED_FUNCTION();

	stopFileIndex(state);

	indexingInProgress = 1;
	addEntryToWorkQueue(&state->pool.queue, workerComputeIndex, state);
}



void clearFileIndex(FileIndex* fileIndex)
{
	FileIndexEntry* next = 0;
	for (FileIndexEntry* ei = fileIndex->firstFile; ei; ei = next)
	{
		next = ei->next;
		freeFileIndexEntry(ei);
	}
	free(fileIndex->filePathHash);
	*fileIndex = {};
}


