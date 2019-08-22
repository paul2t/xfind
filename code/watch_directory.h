
// Helper for watching directory modifications
// Will merge several modifications into one.
// For example, when saving in Visual Studio, we see the following events:
// CREATE TMP1
// EDIT   TMP1
// CREATE TMP2
// DELETE TMP2
// RENAME FILE => TMP2
// RENAME TMP1 => FILE
// DELETE TMP2
//
// All these events will be converted into a single event:
// EDIT   FILE
//
// It works for any group of events, not just for this special case.

#if 0
void usage()
{
	char* dirs[] = { "path\\to\\watch" };
	int dirs_size = sizeof(dirs) / sizeof(*dirs);

	WatchDir wd = watchdir_start(dirs, dirs_size);

	for (;;)
	{
		WatchDirEvent* evt = watchdir_get_event(wd);
		if (!evt) continue;

		// NOTE: Can be renamed && modified

		if (evt->created)
			printf("+ ");
		else if (evt->deleted)
			printf("- ");
		else if (evt->modified)
			printf("~ ");
		else
			printf("  ");

		if (evt->old_name_size) // renamed
			printf("%s -> %s", evt->old_name, evt->name);
		else
			printf("%s", evt->name);

		printf("\n");
	}

	watchdir_stop(wd);
}
#endif


#if APP_WIN32
#include <Windows.h>
#pragma comment(lib, "kernel32.lib")

#ifndef strdup
#define strdup _strdup
#endif
#else
#error Only working on Windows
#endif

#include <cstdio>
#include <stdint.h>

#if APP_INTERNAL
#include <cassert>
#endif



struct WatchDirEvent
{
	char* name;
	int32_t name_size;
	char* old_name;
	int32_t old_name_size;
	bool created;
	bool deleted;
	bool modified;

	bool existed;
};

struct WatchDirEventBuffer
{
	WatchDirEvent* data = 0;
	int32_t max_size = 0;
	int32_t start = 0; // First event index
	int32_t next = 0; // Next empty slot index
};

struct WatchDir
{
	int32_t dirs_size = 0;
	struct _WatchDir* dirs = 0;
	HANDLE* watch_handles = 0;
	int32_t result_buffers_size = 0;
	FILE_NOTIFY_INFORMATION** result = 0;

	WatchDirEventBuffer events = {};
};


WatchDirEvent* watchdir_get_event(WatchDir& wd, uint32_t timeout_ms = INFINITE);
WatchDir watchdir_start(char** dirs, int32_t dirs_size);
void watchdir_stop(WatchDir& wd);




struct _WatchDir
{
	char* path;
	int32_t path_size;
	HANDLE hdir;
	OVERLAPPED overlapped;
};



void free(WatchDirEvent& evt)
{
	free(evt.name);
	free(evt.old_name);
	evt = {};
}


static bool read_changes(WatchDir& wd, int32_t i);

WatchDir watchdir_start(char** dirs, int32_t dirs_size)
{
	WatchDir wd = {};

	wd.dirs_size = dirs_size;

	wd.dirs = (_WatchDir*)(malloc(dirs_size * sizeof(_WatchDir)));
	memset(wd.dirs, 0, dirs_size * sizeof(_WatchDir));

	wd.watch_handles = (HANDLE*)(malloc(dirs_size * sizeof(HANDLE)));
	memset(wd.watch_handles, 0, dirs_size * sizeof(HANDLE));

	wd.result_buffers_size = 8192;
	wd.result = (FILE_NOTIFY_INFORMATION**)(malloc(dirs_size * sizeof(FILE_NOTIFY_INFORMATION*)));


	if (wd.events.max_size <= 0)
		wd.events.max_size = 4096;
	wd.events.data = (WatchDirEvent*)(malloc(wd.events.max_size * sizeof(WatchDirEvent)));
	memset(wd.events.data, 0, wd.events.max_size * sizeof(WatchDirEvent));

	for (int32_t i = 0; i < dirs_size; ++i)
	{
		wd.result[i] = (FILE_NOTIFY_INFORMATION*)(malloc(wd.result_buffers_size));
		memset(wd.result[i], 0, wd.result_buffers_size);

		_WatchDir* _wd = wd.dirs + i;
		_wd->path = strdup(dirs[i]);
		_wd->path_size = (int32_t)strlen(dirs[i]);
		_wd->hdir = CreateFileA(_wd->path, GENERIC_READ | FILE_LIST_DIRECTORY,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
			NULL);
		
		if (_wd->hdir == INVALID_HANDLE_VALUE) continue;

		_wd->overlapped.OffsetHigh = 0;
		_wd->overlapped.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);

		// NOTE: needed for initialization of the events.
		read_changes(wd, i);
	}

	return wd;
}

void watchdir_stop(WatchDir& wd)
{
	for (int32_t i = 0; i < wd.dirs_size; ++i)
	{
		auto* _wd = wd.dirs + i;
		CloseHandle(_wd->hdir);
		CloseHandle(_wd->overlapped.hEvent);
		free(_wd->path);
		free(wd.result[i]);
	}
	free(wd.dirs);
	free(wd.result);
	free(wd.events.data);
	free(wd.watch_handles);
	wd = {};
}

// @return The index of the first directory that triggered an event (there can be several). -1 if timeout.
static int32_t wait_for_event(WatchDir& wd, uint32_t timeout_ms = INFINITE)
{
	DWORD waitStatus = WaitForMultipleObjects(wd.dirs_size, wd.watch_handles, FALSE, timeout_ms);
	//DWORD waitStatus = WaitForSingleObject(wd.dirs[0].overlapped.hEvent, timeout_ms);
	if (WAIT_OBJECT_0 <= waitStatus && waitStatus < WAIT_OBJECT_0 + wd.dirs_size)
		return waitStatus - WAIT_OBJECT_0;
	return -1;
}

// @param condition function WatchDirEvent* -> bool
template<typename F>
static WatchDirEvent* find_first_event(WatchDirEventBuffer events, F condition)
{
	if (events.next == events.start)
		return 0;

	int32_t index = events.start;
	if (events.start > events.next)
	{
		for (; index < events.max_size; ++index)
		{
			WatchDirEvent* evt = events.data + index;
			if (condition(evt))
				return evt;
		}
		index = 0;
	}

	for (; index < events.next; ++index)
	{
		WatchDirEvent* evt = events.data + index;
		if (condition(evt))
			return evt;
	}

	return 0;
}

// @param condition function WatchDirEvent* -> bool
template<typename F>
static WatchDirEvent* find_last_event(WatchDirEventBuffer events, F condition)
{
	if (events.next == events.start)
		return 0;

	int32_t index = events.next - 1;
	if (events.next < events.start)
	{
		for (; index >= 0; --index)
		{
			WatchDirEvent* evt = events.data + index;
			if (condition(evt))
				return evt;
		}
		index = events.max_size - 1;
	}

	for (; index >= events.start; --index)
	{
		WatchDirEvent* evt = events.data + index;
		if (condition(evt))
			return evt;
	}

	return 0;
}

inline WatchDirEvent* find_first_event_named(WatchDirEventBuffer events, char* filename)
{
	return find_first_event(events, [=](WatchDirEvent* evt) { return 0 == strcmp(evt->name, filename); });
}

inline WatchDirEvent* find_last_event_named(WatchDirEventBuffer events, char* filename)
{
	return find_last_event(events, [=](WatchDirEvent* evt) { return 0 == strcmp(evt->name, filename); });
}

WatchDirEvent* get_next_event(WatchDirEventBuffer& events)
{
	WatchDirEvent* evt = events.data + events.next;
	*evt = {};
	events.next++;
	if (events.next >= events.max_size)
		events.next = 0;
	return evt;
}

void remove_event(WatchDirEventBuffer& events, WatchDirEvent* old_event)
{
	if (!old_event) return;

	free(*old_event);
	// Shift all events by one.
	if (old_event > events.data + events.next)
	{
		for (; old_event < events.data + events.max_size - 1; ++old_event)
		{
			*old_event = *(old_event + 1);
		}
		*old_event = events.data[0];
		old_event = events.data;
	}
	for (; old_event < events.data + events.next - 1; ++old_event)
	{
		*old_event = *(old_event + 1);
	}
	events.next--;
	if (events.next < 0)
		events.next = events.max_size - 1;
}

static bool parse_file_notify_informations(char* path, int path_size, FILE_NOTIFY_INFORMATION* notify_informations, WatchDirEventBuffer& events)
{
	if (notify_informations->Action == 0 && notify_informations->NextEntryOffset == 0) return false;
	if (path_size > 0 && !path) return false;

	char filename[MAX_PATH];
	memcpy(filename, path, path_size);
	if (path_size > 0 && filename[path_size - 1] != '\\')
		filename[path_size++] = '\\';
	filename[path_size] = 0;

	char basename[MAX_PATH];

#if APP_INTERNAL
	WatchDirEventBuffer old_buffer = events;
	old_buffer.data = (WatchDirEvent*)malloc(old_buffer.next * sizeof(WatchDirEvent));
	memcpy(old_buffer.data, events.data, old_buffer.next * sizeof(WatchDirEvent));
#endif

	bool has_changes = false;

	int offset = 0;
	FILE_NOTIFY_INFORMATION* notify;
	do
	{
		notify = (FILE_NOTIFY_INFORMATION*)((char*)notify_informations + offset);
		int len = notify->FileNameLength / 2;
		basename[0] = 0;
		int filenamelen = WideCharToMultiByte(CP_ACP, 0, notify->FileName, len, basename, sizeof(basename), NULL, NULL);
		basename[len] = 0;

		filenamelen += path_size;
		memcpy(filename + path_size, basename, len);
		filename[filenamelen] = 0;

		if (notify->Action)
		{
			has_changes = true;
			switch (notify->Action)
			{
			case FILE_ACTION_ADDED: {
#if APP_INTERNAL
				printf("+ %s\n", filename);
#endif
				WatchDirEvent* old_event = find_last_event_named(events, filename);
				WatchDirEvent* evt = get_next_event(events);
				if (old_event)
				{
					*evt = *old_event;
					*old_event = {};
					if (!evt->existed)
					{
						// path never used because it is handled in the rename
						// create A && rename A -> B && create A
						// => create B && create A
						evt->created = true;
						evt->deleted = false;
						evt->modified = false;
					}
					else
					{
						// delete A && create A
						// => edit A
						evt->created = false;
						evt->deleted = false;
						evt->modified = true;
					}
					remove_event(events, old_event);
				}
				else
				{
					// create A
					evt->existed = false;
					evt->created = true;
					evt->name = strdup(filename);
					evt->name_size = filenamelen;
				}
			} break;

			case FILE_ACTION_REMOVED: {
#if APP_INTERNAL
				printf("- %s\n", filename);
#endif
				WatchDirEvent* old_event = find_last_event_named(events, filename);
				WatchDirEvent* evt = get_next_event(events);
				if (old_event)
				{
					// If there is an earlier event on the same file. We prefer updating this old event instead of creating a new one.

					WatchDirEvent* mevt = 0;
					if (!old_event->created && old_event->old_name_size)
					{
						// If this old event is a rename, we look for the first event on this old name.
						mevt = find_first_event_named(events, old_event->old_name);
					}

					if (mevt)
					{
						*evt = *mevt;
						*mevt = {};
						if (!evt->created)
						{
							// rename OLD -> NEW (old_event) && rename OTHER -> OLD (mevt) && delete NEW
							// => delete OHTER (evt) && edit OLD (evt2)
							WatchDirEvent* evt2 = get_next_event(events);
							*evt2 = *old_event;
							*old_event = {};
							free(evt2->old_name);
							evt2->old_name = 0;
							evt2->old_name_size = 0;
							free(evt2->name);
							evt2->name = evt->name;
							evt2->name_size = evt->name_size;
							evt->name = evt->old_name;
							evt->name_size = evt->old_name_size;
							evt->old_name = 0;
							evt->old_name_size = 0;
							evt->created = false;
							evt->modified = false;
							evt->deleted = true;
							evt->existed = true;
							evt2->created = false;
							evt2->modified = true;
							evt2->deleted = false;
						}
						else
						{
							// rename OLD -> NEW (old_event) && create OLD (mevt) && delete NEW
							// => edit OLD (evt)
							free(evt->old_name);
							evt->old_name = {};
							evt->old_name_size = 0;
							evt->created = false;
							evt->modified = true;
							evt->existed = true;
						}
						remove_event(events, mevt);
					}
					else
					{
						if (old_event->existed)
						{
							// If we are deleting a file that existed before.
							// rename B -> A && delete A
							// => delete B // It is not doing that here !
							*evt = *old_event;
							*old_event = {};
							evt->created = false;
							evt->deleted = true;
							evt->modified = false;
						}
						else
						{
							// If we are deleting a file that did not previously existed.
							// create A && delete A
							// => nop
							remove_event(events, evt);
						}
					}
					remove_event(events, old_event);
				}
				else
				{
					// delete A
					evt->existed = true;
					evt->deleted = true;
					evt->name = strdup(filename);
					evt->name_size = filenamelen;
				}
			} break;

			case FILE_ACTION_MODIFIED: {
				//sprintf(pathbuff, "%s\\%s", path, filename);

				// Ignore modifications of folders.
				DWORD dwAttrib = GetFileAttributesA(filename);
				if (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY))
					break;
#if APP_INTERNAL
				printf("~ %s\n", filename);
#endif

				WatchDirEvent* old_event = find_last_event_named(events, filename);
				if (old_event)
				{
					// rename B -> A && edit A
					// edit A && edit A
					if (!old_event->created)
						old_event->modified = true;
				}
				else
				{
					// edit A
					WatchDirEvent* evt = get_next_event(events);
					evt->existed = true;
					evt->modified = true;
					evt->name = strdup(filename);
					evt->name_size = filenamelen;
				}
			} break;

			case FILE_ACTION_RENAMED_OLD_NAME: {
#if APP_INTERNAL
				printf("< %s\n", filename);
#endif
				WatchDirEvent* old_event = find_last_event_named(events, filename);
				WatchDirEvent* evt = get_next_event(events);
				if (old_event)
				{
					// Renaming a file that already has an event.

					*evt = *old_event;
					*old_event = {};
					if (evt->old_name_size)
					{
						// rename A -> B && rename B -> C
						// => rename A -> C
						evt->old_name = evt->name;
						evt->old_name_size = evt->name_size;
						evt->name = 0;
						evt->name_size = 0;
					}
					else
					{
						// Renaming a file that was previously created.
						// create A && rename A -> B
						// => create B
						free(evt->name);
						evt->name = 0;
						evt->name_size = 0;
						evt->created = true;
					}
					remove_event(events, old_event);
				}
				else
				{
					// rename A -> B (1/2)
					evt->existed = true;
					evt->old_name = strdup(filename);
					evt->old_name_size = filenamelen;
				}
			} break;

			case FILE_ACTION_RENAMED_NEW_NAME: {
#if APP_INTERNAL
				printf("> %s\n", filename);
#endif
				WatchDirEvent* rename_event = find_last_event(events, [=](auto* evt) { return !evt->name_size; });
				if (rename_event)
				{
					// rename A -> B (2/2)
					rename_event->name = "";
					rename_event->name_size = 0;
					WatchDirEvent* old_event = find_last_event_named(events, filename);
					if (old_event)
					{
						// delete B && rename A -> B
						// => delete A && edit B
						if (old_event->existed)
						{
							WatchDirEvent* evt = get_next_event(events);
							evt->deleted = true;
							evt->existed = true;
							evt->name = rename_event->old_name;
							evt->name_size = rename_event->old_name_size;
							WatchDirEvent* evt2 = get_next_event(events);
							evt2->modified = true;
							evt2->existed = true;
							evt2->name = old_event->name;
							evt2->name_size = old_event->name_size;
							*rename_event = {};
							remove_event(events, rename_event);
						}
						else
						{
							// create B && rename A -> B
							// => rename A -> B
#if APP_INTERNAL
							assert(0); // should not happen
							// Will be split in create B && delete B && rename A -> B
#endif
							rename_event->name = strdup(filename);
							rename_event->name_size = filenamelen;
						}
						*old_event = {};
						remove_event(events, old_event);
					}
					else
					{
						rename_event->name = strdup(filename);
						rename_event->name_size = filenamelen;
					}
				}
#if APP_INTERNAL
				else
				{
					printf("Error: could not find matching event to rename to : %s\n", filename);
				}
#endif
			} break;

			default:
#if APP_INTERNAL
				printf("\nUnknown action %d.\n", notify->Action);
#endif
				break;
			}

#if APP_INTERNAL
			if (notify->Action != 4)
			{
				auto* evt = find_last_event(events, [=](auto* e) { return true; });
				if (evt) assert(evt->name || evt->old_name);
			}
#endif
		}

		offset += notify->NextEntryOffset;

	} while (notify->NextEntryOffset);

#if APP_INTERNAL
	if (events.next > 1)// && events.data[1].deleted == true)
	{
		//if (0 != strcmp(events.data[1].name, "C:\\work\\xfind\\code\\watch_directory.h"))
		{
			int breakhere = 1;
		}
	}

	free(old_buffer.data);
#endif

	*notify_informations = {};
	return has_changes;
}

static WatchDirEvent* go_to_next_event(WatchDirEventBuffer& events)
{
	if (events.start != events.next)
	{
		WatchDirEvent* result = events.data + events.start;
		events.start++;
		if (events.start >= events.max_size)
			events.start = 0;
		return result;
	}
	else
	{
		events.start = 0;
		events.next = 0;
	}
	return 0;
}

static bool read_changes(WatchDir& wd, int32_t i)
{
	_WatchDir& dir = wd.dirs[i];

	// Read the directory changes from the buffer.
	bool has_changes = parse_file_notify_informations(dir.path, dir.path_size, wd.result[i], wd.events);

	// Setup the new directory changes waiting object
	DWORD nRet;
	BOOL succeeded = ReadDirectoryChangesW(
		dir.hdir, // handle to the directory to be watched
		wd.result[i], // pointer to the buffer to receive the read results
		wd.result_buffers_size, // length of lpBuffer
		TRUE, // flag for monitoring directory or directory tree
		0
		| FILE_NOTIFY_CHANGE_FILE_NAME // file name changed
		| FILE_NOTIFY_CHANGE_DIR_NAME // directory name changed
		//| FILE_NOTIFY_CHANGE_SIZE // file size changed
		| FILE_NOTIFY_CHANGE_LAST_WRITE // last write time changed
		//| FILE_NOTIFY_CHANGE_LAST_ACCESS // last access time changed
		//| FILE_NOTIFY_CHANGE_CREATION // Creation time changed
		,
		&nRet, // number of bytes returned
		&dir.overlapped, // pointer to structure needed for overlapped I/O
		NULL);
	wd.watch_handles[i] = dir.overlapped.hEvent;

	return has_changes;
}


WatchDirEvent* watchdir_get_event(WatchDir& wd, uint32_t timeout_ms)
{
	for (;;)
	{
		if (WatchDirEvent* evt = go_to_next_event(wd.events))
			return evt;

		int32_t first_triggered = wait_for_event(wd, timeout_ms);
		if (first_triggered < 0)
			return 0;

		for (;;)
		{
			bool changes_found = false;
			for (int32_t i = first_triggered; i < wd.dirs_size; ++i)
			{
				bool has_changes = read_changes(wd, i);
				if (has_changes)
					changes_found = true;
			}

			if (changes_found)
			{
				// Timeout after 10ms to see if there is something available right away or not.
				first_triggered = wait_for_event(wd, 10);
				if (first_triggered >= 0)
					continue;
			}
			break;
		}
	}
}


