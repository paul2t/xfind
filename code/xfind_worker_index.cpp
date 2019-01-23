
volatile b32 workerSearchPatternShouldStop;
volatile b32 workerLoadIndexShouldStop;
volatile b32 workerIndexerShouldStop;

volatile u32 indexingInProgress;
volatile u32 searchInProgress;


internal WORK_QUEUE_CALLBACK(workerLoadFileToMemory)
{
	if (workerLoadIndexShouldStop) return;
	FileIndexEntry* fileIndex = (FileIndexEntry*)data;
	fileIndex->lastWriteTime = GetLastWriteTime(fileIndex->path.str);
	FILE* file = fopen(fileIndex->path.str, "rb");
	if (file)
	{
		memid nitemsRead = fread(fileIndex->content.str, 1, fileIndex->content.memory_size - 1, file);
		fileIndex->content.size = (i32)nitemsRead;
		terminate_with_null(&fileIndex->content);
		fclose(file);
	}
	InterlockedDecrement(&indexingInProgress);
}

internal WORK_QUEUE_CALLBACK(workerComputeIndex)
{
	if (workerIndexerShouldStop) return;
	//u64 ticksStart = getTickCount();

	umm maxFileLength = MegaBytes(1) - 1;
	umm minFileLength = KiloBytes(4);

	State* state = (State*)data;
	String* searchPaths = state->searchPaths;
	i32 searchPathsSize = state->searchPathsSize;
	FileIndex* fileIndex = &state->index;
	String* searchExtensions = state->extensions;
	i32 searchExtensionsSize = state->extensionsSize;
	MemoryArena& indexArena = state->index.arena;
	indexArena.Release();
	FileIndexEntry** onePastLastFile = fileIndex->onePastLastFile;

	i32 filesSize = 0;

	for (i32 pi = 0; pi < searchPathsSize; ++pi)
	{
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
			if (workerIndexerShouldStop) return;
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
							FileIndexEntry* fileEntry = pushStruct(indexArena, FileIndexEntry);
							*onePastLastFile = fileEntry;
							onePastLastFile = &fileEntry->next;

							String file = pushNewString(indexArena, searchPath.size + filename.size + 1);
							append(&file, searchPath);
							append(&file, filename);
							terminate_with_null(&file);

							fileEntry->path = file;
							fileEntry->relpath = substr(file, searchPaths[pi].size + 1);
							umm fileLength = getFileSize(current);
							fileEntry->content = {};

							{
								umm allocSize = (umm)(1.5 * fileLength) + 1;
								if (allocSize > maxFileLength + 1)
									allocSize = maxFileLength + 1;
								if (allocSize < minFileLength)
									allocSize = minFileLength;
								fileEntry->content.memory_size = (i32)allocSize;
								fileEntry->content.size = 0;
								fileEntry->content.str = pushArray(indexArena, char, allocSize, pushpNoClear());
								InterlockedIncrement(&indexingInProgress);
								addEntryToWorkQueue(queue, workerLoadFileToMemory, fileEntry);
							}
							filesSize++;
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

	fileIndex->filesSize = filesSize;

	InterlockedDecrement(&indexingInProgress);

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
	state->index.filesSize = 0;
	state->index.firstFile = 0;
	state->index.arena.Release();
	state->index.onePastLastFile = &state->index.firstFile;
}

internal void computeFileIndex(State* state)
{
	stopFileIndex(state);

	indexingInProgress = 1;
	addEntryToWorkQueue(&state->pool.queue, workerComputeIndex, state);
}
