

#if APP_INTERNAL && 0
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


volatile b32 watchPathsChanged = false;

//internal DWORD directory_watcher(void* _data)
WORK_QUEUE_CALLBACK(directory_watcher)
{
	State* state = (State*)data;
	String* paths = state->watchPaths;
	i32 pathsSize = state->watchPathsSize;

	if (paths && pathsSize > 0)
	{
		HANDLE* changeHandles = new HANDLE[pathsSize];
		i32 changeHandlesSize = 0;
		for (i32 i = 0; i < pathsSize; ++i)
		{
			terminate_with_null(paths);
			changeHandles[i] = FindFirstChangeNotification(paths[i].str, TRUE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
			if (changeHandles[i] == INVALID_HANDLE_VALUE)
			{
				break;
			}
			++changeHandlesSize;
		}

		if (changeHandlesSize == pathsSize)
		{
			for (;;)
			{
				DWORD waitStatus = WaitForMultipleObjects(changeHandlesSize, changeHandles, FALSE, 5);

				if (watchPathsChanged)
					break;

				if (waitStatus == WAIT_TIMEOUT)
				{
					continue;
				}

				if (!(WAIT_OBJECT_0 <= waitStatus && waitStatus < WAIT_OBJECT_0 + changeHandlesSize))
				{
#if APP_INTERNAL
					assert(0);
#endif
					break;
				}

				i32 index = waitStatus - WAIT_OBJECT_0;
				printf("Directory %d changed : %.*s\n", index, strexp(paths[index]));
				state->needToGenerateIndex = true;

				if (FindNextChangeNotification(changeHandles[index]) == FALSE)
				{
#if APP_INTERNAL
					assert(0);
#endif
					break;
				}
			}
		}

		for (i32 i = 0; i < changeHandlesSize; ++i)
			FindCloseChangeNotification(changeHandles[i]);
		delete[] changeHandles;
	}
}


void updateWatchedDirectories(State& state)
{
	if (state.searchPathExists && state.searchPaths && state.searchPathsSize > 0)
	{
		if (state.dirWatchThread.nbThreads <= 0)
			initThreadPool(state.arena, state.dirWatchThread, 1);

		cleanWorkQueue(&state.dirWatchThread.queue, &watchPathsChanged);

		state.watchArena.Release();
		state.watchPaths = pushArray(state.watchArena, String, state.searchPathsSize);
		for (i32 i = 0; i < state.searchPathsSize; ++i)
		{
			state.watchPaths[i] = pushStringZeroTerminated(state.watchArena, state.searchPaths[i]);
		}
		state.watchPathsSize = state.searchPathsSize;

		addEntryToWorkQueue(&state.dirWatchThread.queue, directory_watcher, &state);
	}
}
