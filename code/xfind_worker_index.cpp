
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

internal u32 hash(String s)
{
	char* at = s.str;
	u32 result = 0;
	for (i32 i = 0; i < s.size; ++i)
		result = (result << 8 | result >> 24) + *at;
	return result;
}

internal FileIndexEntry* getFileFromPath(FileIndex* fileIndex, String path)
{
	i32 pathHash = hash(path) % fileIndex->filePathHashSize;
	FileIndexEntry* entry = fileIndex->filePathHash[pathHash];
	while (entry && !match(entry->path, path)) entry = entry->nextInPathHash;
	return entry;
}

internal FileIndexEntry* getOrCreateFileFromPath(FileIndex* fileIndex, String path, i32 rootSize = 0)
{
	i32 pathHash = hash(path) % fileIndex->filePathHashSize;
	FileIndexEntry* entry = fileIndex->filePathHash[pathHash];
	while (entry && !match(entry->path, path)) entry = entry->nextInPathHash;
	if (!entry)
	{
		FileIndexEntry* firstEntry = fileIndex->filePathHash[pathHash];
		entry = fileIndex->firstRemovedFile;
		if (entry)
			fileIndex->firstRemovedFile = entry->next;
		else
			entry = pushStruct(fileIndex->arena, FileIndexEntry);

		entry->modifiedSinceLastSearch = true;
		entry->path = path;
		entry->path.str = strdup(path.str);

		entry->nextInPathHash = firstEntry;
		fileIndex->filePathHash[pathHash] = entry;

		entry->next = 0;
		*fileIndex->onePastLastFile = entry;
		fileIndex->onePastLastFile = &entry->next;
	}

	entry->relpath = substr(entry->path, rootSize);

	return entry;
}

volatile b32 workerSearchPatternShouldStop;
volatile b32 workerLoadIndexShouldStop;
volatile b32 workerIndexerShouldStop;

volatile u32 indexingInProgress;
volatile u32 searchInProgress;

#define MAX_FILE_PARSED_SIZE (MegaBytes(1)-1)

internal WORK_QUEUE_CALLBACK(workerReloadFileToMemory)
{
	if (workerLoadIndexShouldStop) return;
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
	if (workerIndexerShouldStop) return;
	//u64 ticksStart = getTickCount();

#if APP_INTERNAL
	treeTraversalTimeStart = GetTickCount64();
#endif

	State* state = (State*)data;
	String* searchPaths = state->searchPaths;
	i32 searchPathsSize = state->searchPathsSize;
	FileIndex* fileIndex = &state->index;
	String* searchExtensions = state->extensions;
	i32 searchExtensionsSize = state->extensionsSize;
	MemoryArena& indexArena = state->index.arena;
	//indexArena.Release();
	FileIndexEntry** onePastLastFile = fileIndex->onePastLastFile;

	// TODO(xf4): make a separate array. Might be faster ?!?!
	for (FileIndexEntry* ei = fileIndex->firstFile; ei; ei = ei->next)
		ei->seenInIndex = false;

	for (i32 pi = 0; pi < searchPathsSize; ++pi)
	{
		if (workerIndexerShouldStop) break;
		char _pathbuffer[4096];
		String searchPath = make_fixed_width_string(_pathbuffer);
		copy(&searchPath, searchPaths[pi]);
		for (int ci = 0; ci < searchPath.size; ++ci) if (searchPath.str[ci] == '/') searchPath.str[ci] = '\\';
		if (char_is_slash(searchPath.str[searchPath.size - 1]))
			searchPath.size--;

		// Already indexed
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
			if (workerIndexerShouldStop) break;
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
						bool matchext = false;
						for (i32 ei = 0; ei < searchExtensionsSize; ++ei)
						{
							if (match(fileext, searchExtensions[ei]))
							{
								matchext = true;
								break;
							}
						}
						if (matchext)
						{
							String file = searchPath;
							append(&file, filename);
							terminate_with_null(&file);

							FileIndexEntry* fileEntry = getOrCreateFileFromPath(fileIndex, file, searchPaths[pi].size + 1);
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
	}

	// NOTE(xf4): Remove files that haven't been seen while searching.
	i32 filesSize = 0;
	FileIndexEntry _tmpe = {};
	for (FileIndexEntry* ei = fileIndex->firstFile, **prevnext = &fileIndex->firstFile; ei; ei = ei->next)
	{
		if (workerIndexerShouldStop) break;
		++filesSize;
		if (!ei->seenInIndex)
		{
			ScopeMutex(&ei->mutex);
			--filesSize;

			FileIndexEntry* nexte = ei->next;
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


			free(ei->content.str);
			free(ei->path.str);
			*ei = {};
			ei->next = fileIndex->firstRemovedFile;
			fileIndex->firstRemovedFile = ei;
			if (fileIndex->onePastLastFile == &ei->next)
				fileIndex->onePastLastFile = prevnext;
			*prevnext = nexte;

			ei = &_tmpe;
			ei->next = nexte;
		}
		else
		{
			prevnext = &ei->next;
		}
	}

	fileIndex->filesSize = filesSize;

	u32 test = InterlockedDecrement(&indexingInProgress);
	DEBUG_TEST_TIMER(!test, indexTimeStart, indexTime);

#if APP_INTERNAL
	treeTraversalTime = GetTickCount64() - treeTraversalTimeStart;
#endif
	//u64 ticksEnd = getTickCount();
	//printf("Found %d files in %llums\n", filesSize, (ticksEnd - ticksStart));
	//printMemUsage(indexArena);
}

internal void stopFileIndex(State* state)
{
	//u64 ticksStart = getTickCount();
	workerIndexerShouldStop = true;
	workerLoadIndexShouldStop = true;
	workerSearchPatternShouldStop = true;
	finishWorkQueue(&state->pool.queue);
	workerSearchPatternShouldStop = false;
	workerLoadIndexShouldStop = false;
	workerIndexerShouldStop = false;
	//u64 ticksEnd = getTickCount();
	//printf("%llums to finish the index queue\n", ticksEnd - ticksStart);

	indexingInProgress = 0;
	//state->index.filesSize = 0;
	//state->index.firstFile = 0;
	//state->index.arena.Release();
	//state->index.onePastLastFile = &state->index.firstFile;
}

internal void computeFileIndex(State* state)
{
#if APP_INTERNAL
	indexTimeStart = GetTickCount64();
#endif

	stopFileIndex(state);

	indexingInProgress = 1;
	addEntryToWorkQueue(&state->pool.queue, workerComputeIndex, state);
}



