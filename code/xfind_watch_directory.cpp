

#if 1
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


enum FileEventType
{
	FileEvent_none,
	FileEvent_deleted,
	FileEvent_renamed,
	FileEvent_renamed_old,

	FileEvent_count,
};

struct FileEvent
{
	String name;
	String old_name;
	bool created;
	bool deleted;
	bool modified;

	bool existed;
};

void free(FileEvent& evt)
{
	free(evt.name.str);
	free(evt.old_name.str);
	evt = {};
}

FileEvent file_event_buffer[4096];
volatile i32 file_event_size = 0;


// Method to start watching a directory. Call it on a separate thread so it wont block the main thread.  
// From : https://developersarea.wordpress.com/2014/09/26/win32-file-watcher-api-to-monitor-directory-changes/
void WatchDirectory(char* path)
{
	char buf[2048];
	DWORD nRet;
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
	int changes_count = 0;
	for(;;)
	{
		BOOL result = ReadDirectoryChangesW(
			DirInfo[0].hDir,// handle to the directory to be watched
			&buf,// pointer to the buffer to receive the read results
			sizeof(buf),// length of lpBuffer
			TRUE,// flag for monitoring directory or directory tree
			0
			| FILE_NOTIFY_CHANGE_FILE_NAME // file name changed
			| FILE_NOTIFY_CHANGE_DIR_NAME // directory name changed
			//| FILE_NOTIFY_CHANGE_SIZE // file size changed
			| FILE_NOTIFY_CHANGE_LAST_WRITE // last write time changed
			//| FILE_NOTIFY_CHANGE_LAST_ACCESS // last access time changed
			//| FILE_NOTIFY_CHANGE_CREATION // Creation time changed
			,
			&nRet,// number of bytes returned
			&PollingOverlap,// pointer to structure needed for overlapped I/O
			NULL);

		if (!result) break;

		for (;;)
		{
			// Timeout after 1ms to see if there is something available right away or not.
			DWORD waitStatus = WaitForSingleObject(PollingOverlap.hEvent, 10);
			if (waitStatus != WAIT_TIMEOUT)
				break;

			// If no event is available right now, we send the modifications and we wait infinitely.

			// TODO: Here we need to push all the modifications to the search
			//printf("END OF MODIFICATIONS : %d\n", changes_count);
			for (i32 i = 0; i < changes_count; ++i)
			{
				FileEvent* evt = file_event_buffer + ((file_event_size + i) % sizeof(file_event_buffer));
				if (evt->created)
					printf("+ ");
				else if (evt->deleted)
					printf("- ");
				else if (evt->modified)
					printf("~ ");
				else if (evt->existed && evt->old_name.size)
					printf("  ");
				else
					continue;

				if (evt->old_name.size)
					printf(" %s -> %s", evt->old_name.str, evt->name.str);
				else
					printf(" %s",evt->name.str);

				printf("\n");
			}
			printf("\n");
			changes_count = 0;

			WaitForSingleObject(PollingOverlap.hEvent, INFINITE);
		}
		offset = 0;
		//int rename = 0;
		//char oldName[260];
		//char newName[260];
		do
		{
			pNotify = (FILE_NOTIFY_INFORMATION*)((char*)buf + offset);
			filename[0] = 0;
			int filenamelen = WideCharToMultiByte(CP_ACP, 0, pNotify->FileName, pNotify->FileNameLength / 2, filename, sizeof(filename), NULL, NULL);
			filename[pNotify->FileNameLength / 2] = '\0';
			switch (pNotify->Action)
			{
			case FILE_ACTION_ADDED: {
				//printf("+ %s\n", filename);
				FileEvent* old_event = 0;
				for (i32 i = changes_count-1; i >= 0; --i)
				{
					FileEvent* evt = file_event_buffer + ( (file_event_size + i) % sizeof(file_event_buffer) );
					if (match(evt->name, filename))
					{
						old_event = evt;
						break;
					}
				}
				if (old_event)
				{
					old_event->created = false;
					old_event->deleted = false;
					old_event->modified = true;
					if (!old_event->existed)
						old_event->created = true;
				}
				else
				{
					i32 evt_index = (file_event_size + changes_count) % sizeof(file_event_buffer);
					FileEvent* evt = file_event_buffer + evt_index;
					*evt = {};
					evt->existed = false;
					evt->created = true;
					evt->name.str = strdup(filename);
					evt->name.size = filenamelen;
					++changes_count;
				}
			} break;


			case FILE_ACTION_REMOVED: {
				//printf("- %s\n", filename);
				FileEvent* old_event = 0;
				for (i32 i = changes_count-1; i >= 0; --i)
				{
					FileEvent* evt = file_event_buffer + ((file_event_size + i) % sizeof(file_event_buffer));
					if (match(evt->name, filename))
					{
						old_event = evt;
						break;
					}
				}
				if (old_event)
				{
					FileEvent* mevt = 0;
					if (!old_event->created && old_event->old_name.size)
					{
						for (i32 i = 0; i < changes_count; ++i)
						{
							FileEvent* evt = file_event_buffer + ((file_event_size + i) % sizeof(file_event_buffer));
							if (match(evt->name, old_event->old_name))
							{
								mevt = evt;
								break;
							}
						}
					}
					if (mevt)
					{
						if (!mevt->created)
						{
							free(old_event->old_name.str);
							old_event->old_name = {};
							old_event->name = mevt->name;
							mevt->name = mevt->old_name;
							mevt->old_name = {};
							mevt->created = false;
							mevt->modified = false;
							mevt->deleted = true;
							mevt->existed = true;
							old_event->created = false;
							old_event->modified = true;
							old_event->deleted = false;
						}
						else
						{
							free(mevt->old_name.str);
							mevt->old_name = {};
							mevt->created = false;
							mevt->modified = true;
							mevt->existed = true;
							free(old_event->old_name.str);
							free(old_event->name.str);
							*old_event = {};
						}
					}
					else
					{

						old_event->created = false;
						old_event->deleted = false;
						old_event->modified = false;
						if (old_event->existed)
						{
							old_event->modified = true;
							old_event->deleted = true;
						}
						// NOTE: cannot do that, in the case where the file is renamed and another file with the same name is created.
						//if (old_event->old_name.size)
						//{
							//free(old_event->name.str);
							//old_event->name = old_event->old_name;
							//old_event->old_name = {};
						//}
					}
				}
				else
				{
					i32 evt_index = (file_event_size + changes_count) % sizeof(file_event_buffer);
					FileEvent* evt = file_event_buffer + evt_index;
					*evt = {};
					evt->existed = true;
					evt->deleted = true;
					evt->name.str = strdup(filename);
					evt->name.size = filenamelen;
					++changes_count;
				}
			} break;

			case FILE_ACTION_MODIFIED: {
				char pathbuff[MAX_PATH];
				sprintf(pathbuff, "%s\\%s", path, filename);
				DWORD dwAttrib = GetFileAttributes(pathbuff);
				if (dwAttrib != INVALID_FILE_ATTRIBUTES && dwAttrib & FILE_ATTRIBUTE_DIRECTORY)
					break;
				//printf("~ %s\n", filename);
				FileEvent* old_event = 0;
				for (i32 i = changes_count-1; i >= 0; --i)
				{
					FileEvent* evt = file_event_buffer + ((file_event_size + i) % sizeof(file_event_buffer));
					if (match(evt->name, filename))
					{
						old_event = evt;
						break;
					}
				}
				if (old_event)
				{
					old_event->modified = true;
				}
				else
				{
					i32 evt_index = (file_event_size + changes_count) % sizeof(file_event_buffer);
					FileEvent* evt = file_event_buffer + evt_index;
					*evt = {};
					evt->existed = true;
					evt->modified = true;
					evt->name.str = strdup(filename);
					evt->name.size = filenamelen;
					++changes_count;
				}
			} break;


			case FILE_ACTION_RENAMED_OLD_NAME: {
				//printf("< %s\n", filename);
				FileEvent* old_event = 0;
				for (i32 i = changes_count-1; i >= 0; --i)
				{
					FileEvent* evt = file_event_buffer + ((file_event_size + i) % sizeof(file_event_buffer));
					if (match(evt->name, filename))
					{
						old_event = evt;
						break;
					}
				}
				if (old_event)
				{
					if (old_event->old_name.size)
					{
						free(old_event->name.str);
						old_event->name = {};
					}
					else
					{
						old_event->old_name = old_event->name;
						old_event->name = {};
					}
				}
				else
				{
					i32 evt_index = (file_event_size + changes_count) % sizeof(file_event_buffer);
					FileEvent* evt = file_event_buffer + evt_index;
					*evt = {};
					evt->existed = true;
					evt->old_name.str = strdup(filename);
					evt->old_name.size = filenamelen;
					++changes_count;
				}
			} break;

			case FILE_ACTION_RENAMED_NEW_NAME: {
				//printf("> %s\n", filename);
				FileEvent* old_event = 0;
				for (i32 i = changes_count-1; i >= 0; --i)
				{
					FileEvent* evt = file_event_buffer + ((file_event_size + i) % sizeof(file_event_buffer));
					if (!evt->name.size && evt->old_name.size)
					{
						old_event = evt;
						break;
					}
				}
				if (old_event)
				{
					old_event->name.str = strdup(filename);
					old_event->name.size = filenamelen;
				}
				else
				{
					printf("Error: could not find matching event to rename to : %s\n", filename);
				}
			} break;

			default:
				printf("\nUnknown action %d.\n", pNotify->Action);
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

#if 0
struct WatchData
{
	HANDLE* change_handles = 0;
	String* paths = 0;
	u32 paths_size = 0
	MemoryArena arena;
};

WatchData watch_init(String* paths, i32 paths_size)
{
	WatchData result = {};
	result.change_handles = pushArray(result.arena, HANDLE, paths_size);
	result.paths = pushArray(result.arena, String, paths_size);
	result.paths_size = paths_size;
	for (i32 i = 0; i < paths_size; ++i)
	{
		result.paths[i] = pushStringZeroTerminated(result.arena, paths[i]);
		result.change_handles[i] = FindFirstChangeNotification(result.paths[i].str, TRUE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
	}
}

void watch_close(WatchData* watch)
{
	for (i32 i = 0; i < watch->paths_size; ++i)
	{
		FindCloseChangeNotification(changeHandles[i]);
	}
	watch->arena.clear();
}

void watch_list_changes(WatchData* watch)
{
	for (;;)
	{
		DWORD waitStatus = WaitForMultipleObjects(changeHandlesSize, changeHandles, FALSE, 0);
		if (waitStatus == WAIT_TIMEOUT)
		{
			break;
		}

		if ( !( (WAIT_OBJECT_0 <= waitStatus) && (waitStatus < WAIT_OBJECT_0 + watch->paths_size) ) )
		{
			break;
		}

		i32 index = waitStatus - WAIT_OBJECT_0;
		printf("Directory %d changed : %.*s\n", index, strexp(paths[index]));


	}
}
#endif


#if 0
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
#endif

