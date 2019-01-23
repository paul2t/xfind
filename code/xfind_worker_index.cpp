
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
			_fseeki64(file, 0, SEEK_END);
			size_t newSize = _ftelli64(file);
			if (newSize > MAX_FILE_PARSED_SIZE)
				newSize = MAX_FILE_PARSED_SIZE;
			rewind(file);

			if (fileIndex->content.memory_size < newSize)
			{
				fileIndex->content.memory_size = (i32)(newSize * 1.5f);
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




#if APP_INTERNAL || 1
#define MAX_DIRS 25
#define MAX_FILES 255
#define MAX_BUFFER 4096
typedef struct _DIRECTORY_INFO {
	HANDLE hDir;
	TCHAR lpszDirName[MAX_PATH];
	CHAR lpBuffer[MAX_BUFFER];
	DWORD dwBufLength;
	OVERLAPPED Overlapped;
}DIRECTORY_INFO, *PDIRECTORY_INFO, *LPDIRECTORY_INFO;

DIRECTORY_INFO DirInfo[MAX_DIRS];   // Buffer for all of the directories
TCHAR FileList[MAX_FILES*MAX_PATH]; // Buffer for all of the files
DWORD numDirs;

//Method to start watching a directory. Call it on a separate thread so it wont block the main thread.  
// From : https://developersarea.wordpress.com/2014/09/26/win32-file-watcher-api-to-monitor-directory-changes/
void WatchDirectory(char* path)
{
	char buf[2048];
	DWORD nRet;
	BOOL result = TRUE;
	char filename[MAX_PATH];
	DirInfo[0].hDir = CreateFileA(path, GENERIC_READ | FILE_LIST_DIRECTORY,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
		NULL);

	if (DirInfo[0].hDir == INVALID_HANDLE_VALUE)
	{
		return; //cannot open folder
	}

	lstrcpy(DirInfo[0].lpszDirName, path);
	OVERLAPPED PollingOverlap;

	FILE_NOTIFY_INFORMATION* pNotify;
	int offset;
	PollingOverlap.OffsetHigh = 0;
	PollingOverlap.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	while (result)
	{
		result = ReadDirectoryChangesW(
			DirInfo[0].hDir,// handle to the directory to be watched
			&buf,// pointer to the buffer to receive the read results
			sizeof(buf),// length of lpBuffer
			TRUE,// flag for monitoring directory or directory tree
			0
			| FILE_NOTIFY_CHANGE_FILE_NAME
			| FILE_NOTIFY_CHANGE_DIR_NAME
			//| FILE_NOTIFY_CHANGE_SIZE
			| FILE_NOTIFY_CHANGE_LAST_WRITE
			//| FILE_NOTIFY_CHANGE_LAST_ACCESS
			//| FILE_NOTIFY_CHANGE_CREATION
			,
			&nRet,// number of bytes returned
			&PollingOverlap,// pointer to structure needed for overlapped I/O
			NULL);

		WaitForSingleObject(PollingOverlap.hEvent, INFINITE);
		offset = 0;
		//int rename = 0;
		//char oldName[260];
		//char newName[260];
		do
		{
			pNotify = (FILE_NOTIFY_INFORMATION*)((char*)buf + offset);
			strcpy(filename, "");
			int filenamelen = WideCharToMultiByte(CP_ACP, 0, pNotify->FileName, pNotify->FileNameLength / 2, filename, sizeof(filename), NULL, NULL);
			filename[pNotify->FileNameLength / 2] = '\0';
			switch (pNotify->Action)
			{
			case FILE_ACTION_ADDED:
				printf("\nThe file is added to the directory: [%s] \n", filename);
				break;
			case FILE_ACTION_REMOVED:
				printf("\nThe file is removed from the directory: [%s] \n", filename);
				break;
			case FILE_ACTION_MODIFIED:
				printf("\nThe file is modified. This can be a change in the time stamp or attributes: [%s]\n", filename);
				break;
			case FILE_ACTION_RENAMED_OLD_NAME:
				printf("\nThe file was renamed and this is the old name: [%s]\n", filename);
				break;
			case FILE_ACTION_RENAMED_NEW_NAME:
				printf("\nThe file was renamed and this is the new name: [%s]\n", filename);
				break;
			default:
				printf("\nDefault error.\n");
				break;
			}

			offset += pNotify->NextEntryOffset;

		} while (pNotify->NextEntryOffset); //(offset != 0);
	}

	CloseHandle(DirInfo[0].hDir);

}

void watchDirectory(char** paths, i32 pathsSize)
{
	HANDLE* changeHandles = new HANDLE[pathsSize];
	for (i32 i = 0; i < pathsSize; ++i)
	{
		changeHandles[i] = FindFirstChangeNotification(paths[i], TRUE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
		if (changeHandles[i] == INVALID_HANDLE_VALUE) return;
	}

	for (;;)
	{
		DWORD waitStatus = WaitForMultipleObjects(pathsSize, changeHandles, FALSE, INFINITE);

		if (!(WAIT_OBJECT_0 <= waitStatus && waitStatus < WAIT_OBJECT_0 + pathsSize))
		{
#if APP_INTERNAL
			assert(0);
#endif
			break;
		}

		i32 index = waitStatus - WAIT_OBJECT_0;
		printf("Directory %d changed !\n", index);

		if (FindNextChangeNotification(changeHandles[index]) == FALSE)
		{
#if APP_INTERNAL
			assert(0);
#endif
			break;
		}
	}
	for (i32 i = 0; i < pathsSize; ++i)
	{
		FindCloseChangeNotification(changeHandles[i]);
	}
	delete[] changeHandles;
}
#endif
