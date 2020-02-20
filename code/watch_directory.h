
// Helper for watching directory modifications on windows
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


#define SMART_EVENT_MERGING 1

#if APP_INTERNAL
#define DEBUG_WD 0
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
#else
#ifndef assert
#define assert(...)
#endif
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
	int32_t max_size_reached = 0;
	WatchDirEvent* data = 0;
	int32_t max_size = 0;
	int32_t start = 0; // First event index
	int32_t next = 0; // Next empty slot index
	int32_t last = -1; // Last returned event
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



void free_event(WatchDirEvent& evt)
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

static WatchDirEvent* go_to_next_event(WatchDirEventBuffer& events)
{
	if (events.last >= 0)
	{
		free_event(events.data[events.last]);
		events.last = -1;
	}
	if (events.start != events.next)
	{
		WatchDirEvent* result = events.data + events.start;
		events.last = events.start;
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

void watchdir_stop(WatchDir& wd)
{
	while (go_to_next_event(wd.events));
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

// @param execute function: void f(WatchDirEvent*) called for each event
template<typename F>
static void foreach(WatchDirEventBuffer events, F execute)
{
	if (events.next == events.start)
		return;

	int32_t index = events.start;
	if (events.start > events.next)
	{
		for (; index < events.max_size; ++index)
			execute(events.data + index);
		index = 0;
	}

	for (; index < events.next; ++index)
		execute(events.data + index);
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
	return find_last_event(events, [=](WatchDirEvent* evt) { return 0 == strcmp(evt->name, filename) || (evt->old_name ? 0 == strcmp(evt->old_name, filename) : false); });
}

inline WatchDirEvent* get_last_event(WatchDirEventBuffer events)
{
	return find_last_event(events, [](auto* evt) { return true; });
}

WatchDirEvent* get_next_event(WatchDirEventBuffer& events)
{
	WatchDirEvent* evt = events.data + events.next;
	*evt = {};
	events.next++;
	if (events.next > events.max_size_reached)
		events.max_size_reached = events.next;
	if (events.next >= events.max_size)
		events.next = 0;
	assert(events.next != events.start && events.next != events.last);
	return evt;
}

void remove_event(WatchDirEventBuffer& events, WatchDirEvent* old_event)
{
	if (!old_event) return;

	free_event(*old_event);
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

WatchDirEvent* add_event(WatchDirEventBuffer& events, WatchDirEvent evt)
{
	auto* result = get_next_event(events);
	*result = evt;
	return result;
}


#if DEBUG_WD
#define TRACE(...) printf(##__VA_ARGS__)
#else
#define TRACE(...)
#endif


WatchDirEvent* add_event_created_raw(WatchDirEventBuffer& events, char* filename, int filenamelen)
{
	WatchDirEvent evt = {};
	evt.created = true;
	evt.name = filename;
	evt.name_size = filenamelen;
	return add_event(events, evt);
}

WatchDirEvent* add_event_removed_raw(WatchDirEventBuffer& events, char* filename, int filenamelen)
{
	WatchDirEvent evt = {};
	evt.existed = true;
	evt.deleted = true;
	evt.name = filename;
	evt.name_size = filenamelen;
	return add_event(events, evt);
}

WatchDirEvent* add_event_modified_raw(WatchDirEventBuffer& events, char* filename, int filenamelen, bool existed = true)
{
	WatchDirEvent evt = {};
	evt.existed = existed;
	evt.modified = true;
	evt.name = filename;
	evt.name_size = filenamelen;
	return add_event(events, evt);
}

WatchDirEvent create_event_renamed_old_raw(char* filename, int filenamelen)
{
	WatchDirEvent evt = {};
	evt.existed = true;
	evt.old_name = filename;
	evt.old_name_size = filenamelen;
	return evt;
}

WatchDirEvent* add_event_renamed_raw(WatchDirEventBuffer& events, WatchDirEvent renamed_old, char* filename, int filenamelen)
{
	WatchDirEvent evt = renamed_old;
	evt.name = filename;
	evt.name_size = filenamelen;
	return add_event(events, evt);
}


#if SMART_EVENT_MERGING

void add_event_created(WatchDirEventBuffer& events, char* filename, int filenamelen, bool dup);
void add_event_removed(WatchDirEventBuffer& events, char* filename, int filenamelen, bool dup);
void add_event_modified(WatchDirEventBuffer& events, char* filename, int filenamelen, bool dup);
void add_event_renamed(WatchDirEventBuffer& events, WatchDirEvent rename_event, char* filename, int filenamelen, bool dup);


void add_event_created(WatchDirEventBuffer& events, char* filename, int filenamelen, bool dup)
{
	WatchDirEvent* old_event = find_last_event_named(events, filename);
	if (old_event)
	{
		WatchDirEvent evt = *old_event;

		*old_event = {};
		remove_event(events, old_event);

		if (evt.old_name && 0 == strcmp(evt.old_name, filename))
		{
			//assert(!evt.existed);
			// Can only happen when
			// create A && rename A -> B && create A
			// But: create A && rename A -> B  =>  create B
			// => create B && create A


			// rename A -> B && create A
			// => create B && edit A

			add_event_created(events, evt.name, evt.name_size, false);
			add_event_modified(events, evt.old_name, evt.old_name_size, false);
		}
		else
		{
			assert(0 == strcmp(evt.name, filename));

			if (!evt.existed)
			{
				assert(0);
				// path never used because it is handled in the rename
				// create A && rename A -> B && create A
				// => create B && create A
				add_event_created(events, evt.name, evt.name_size, false);
				add_event_created(events, filename, filenamelen, dup);
				free(evt.old_name);
			}
			else
			{
				// delete A && create A
				// => edit A
				add_event_modified(events, evt.name, evt.name_size, false);
				free(evt.old_name);
			}
		}
	}
	else
	{
		// create A
		char* filenameCreate = filename;
		add_event_created_raw(events, dup ? strdup(filenameCreate) : filename, filenamelen);
	}
}

void add_event_removed(WatchDirEventBuffer& events, char* filename, int filenamelen, bool dup)
{
	WatchDirEvent* old_event = find_last_event_named(events, filename);
	if (old_event)
	{
		assert(0 == strcmp(old_event->name, filename));

		WatchDirEvent ov = *old_event;
		// If there is an earlier event on the same file. We prefer updating this old event instead of creating a new one.

		WatchDirEvent* mevt = 0;
		if (!ov.created && ov.old_name_size)
		{
			// If this old event is a rename, we look for the first event on this old name.
			mevt = find_first_event_named(events, ov.old_name);
		}

		if (mevt)
		{
			WatchDirEvent evt = *mevt;
			if (!evt.created)
			{
				// rename OLD -> NEW (old_event) && rename OTHER -> OLD (mevt) && delete NEW
				// => delete OHTER (evt) && edit OLD (evt2)

				mevt->old_name = 0;
				old_event->old_name = 0;
				remove_event(events, mevt);
				remove_event(events, old_event);

				add_event_removed(events, evt.old_name, evt.old_name_size, false);
				add_event_modified_raw(events, ov.old_name, ov.old_name_size, ov.existed);
			}
			else
			{
				// rename OLD -> NEW (old_event) && create OLD (mevt) && delete NEW
				// => edit OLD (evt)

				assert(mevt->name);
				mevt->name = 0;
				remove_event(events, mevt);
				remove_event(events, old_event);

				add_event_modified_raw(events, evt.name, evt.name_size, ov.existed);
			}
		}
		else
		{
			if (ov.existed)
			{
				// If we are deleting a file that existed before.
			
				
				if (ov.old_name)
				{
					// rename B -> A && delete A
					// => delete B

					assert(old_event->old_name);
					old_event->old_name = 0;
					remove_event(events, old_event);

					add_event_removed(events, ov.old_name, ov.old_name_size, false);
				}
				else
				{
					// deleting a modified file
					// edit A && delete A

					assert(old_event->modified);
					old_event->name = 0;
					remove_event(events, old_event);
					add_event_removed(events, ov.name, ov.name_size, false);
				}
			}
			else
			{
				// If we are deleting a file that did not previously existed.
				// create A && delete A
				// => nop
				remove_event(events, old_event);
			}
		}
	}
	else
	{
		// delete A
		const char* filenameRemoved = filename;
		add_event_removed_raw(events, dup ? strdup(filenameRemoved) : filename, filenamelen);
	}
}

void add_event_modified(WatchDirEventBuffer& events, char* filename, int filenamelen, bool dup)
{
	WatchDirEvent* old_event = find_last_event_named(events, filename);
	if (old_event)
	{
		assert(0 == strcmp(old_event->name, filename));

		// rename B -> A && edit A
		// rename+edit B -> A
		if (!old_event->created)
			old_event->modified = true;

		assert(!old_event->deleted);
		old_event->deleted = false;
	}
	else
	{
		// edit A
		const char* filenameEdit = filename;
		add_event_modified_raw(events, dup ? strdup(filenameEdit) : filename, filenamelen);
	}
}

WatchDirEvent create_event_renamed_old(WatchDirEventBuffer& events, char* filename, int filenamelen, bool dup)
{
	WatchDirEvent* old_event = find_last_event_named(events, filename);
	WatchDirEvent evt = {};
	if (old_event)
	{
		assert(0 == strcmp(old_event->name, filename));

		// Renaming a file that already has an event.

		evt = *old_event;
		*old_event = {};
		remove_event(events, old_event);

		if (evt.old_name_size)
		{
			// rename A -> B && rename B -> C
			// => rename A -> C
			free(evt.name);
			evt.name = 0;
			evt.name_size = 0;
		}
		else if (evt.modified)
		{
			// edit A && rename A -> B
			// => rename+edit A -> B
			evt.old_name = evt.name;
			evt.old_name_size = evt.name_size;
			evt.name = 0;
			evt.name_size = 0;
		}
		else
		{
			// Renaming a file that was previously created.
			// create A && rename A -> B
			// => create B
			assert(evt.created);
			free(evt.name);
			evt.name = 0;
			evt.name_size = 0;
			evt.created = true;
		}
	}
	else
	{
		// rename A -> B (1/2)
		const char* filenameRenaming = filename;
		evt = create_event_renamed_old_raw(dup ? strdup(filenameRenaming) : filename, filenamelen);
	}

	return evt;
}

void add_event_renamed(WatchDirEventBuffer& events, WatchDirEvent rename_event, char* filename, int filenamelen, bool dup)
{
#if APP_INTERNAL
	assert(rename_event.name == 0 && rename_event.name_size == 0);
#endif

	WatchDirEvent* old_event = find_last_event_named(events, filename);
	if (old_event)
	{

		WatchDirEvent old = *old_event;

		if (rename_event.old_name)
		{
			*old_event = {};
			if (!(old.existed && old.old_name))
				remove_event(events, old_event);
			if (old.existed)
			{
				if (old.old_name)
				{
					// rename A -> B && rename C -> A
					assert(0 == strcmp(old.old_name, filename));

					const char* filenameRenamed1 = filename;
					add_event_renamed_raw(events, rename_event, dup ? strdup(filenameRenamed1) : filename, filenamelen);
				}
				else if (old.deleted)
				{
					// delete B && rename A -> B
					// => delete A && edit B
					add_event_removed(events, rename_event.old_name, rename_event.old_name_size, false);
					add_event_modified(events, old.name, old.name_size, false);

					free(old.old_name);
				}
				else
				{
					// create A & delete B & rename A -> B
					// => delete B & create B  (create A & rename A -> B  =>  create B)
					// => edit B
					assert(rename_event.modified && !rename_event.created);

					add_event_modified(events, rename_event.old_name, rename_event.old_name_size, false);

					free(old.name);
					free(old.old_name);
				}
			}
			else
			{
				// create B && rename A -> B
				// => rename A -> B
#if APP_INTERNAL
				assert(0); // should not happen
				// Will be split in create B && delete B && rename A -> B
#endif
				add_event_renamed(events, rename_event, filename, filenamelen, dup);

				free(old.name);
				free(old.old_name);
			}
		}
		else
		{
			// current event is either a create or edit

			if (rename_event.created)
			{
				// create A && rename A -> B
				// => create B
				add_event_created(events, filename, filenamelen, dup);

				free(rename_event.old_name);
				free(rename_event.name);
			}
			else if (rename_event.modified)
			{
				*old_event = {};
				remove_event(events, old_event);
				// edit A && rename A -> B
				// => rename+edit A -> B
				add_event_renamed(events, rename_event, filename, filenamelen, dup);
				free(old.name);
				free(old.old_name);
			}
			else
			{
				assert(0);
				*old_event = {};
				remove_event(events, old_event);
				free_event(rename_event);
				free(old.name);
				free(old.old_name);
			}

		}
	}
	else if (rename_event.old_name && 0 == strcmp(rename_event.old_name, filename))
	{
		// rename A -> A
		// => nop
		if (rename_event.modified)
		{
			// rename A -> B & edit B & rename B -> A
			// => edit A
			rename_event.name = rename_event.old_name;
			rename_event.name_size = rename_event.old_name_size;
			rename_event.old_name = 0;
			rename_event.old_name_size = 0;
		}
		else
		{
			free(rename_event.old_name);
			if (!dup) free(filename);
		}
	}
	else
	{
		// rename A -> B (2/2)
		const char* filenameRenamed = filename;
		add_event_renamed_raw(events, rename_event, dup ? strdup(filenameRenamed) : filename, filenamelen);
	}
}

#else

#define add_event_created(events, filename, filenamelen, dup) add_event_created_raw(events, (dup) ? strdup(filename) : filename, filenamelen)
#define add_event_removed(events, filename, filenamelen, dup) add_event_removed_raw(events, (dup) ? strdup(filename) : filename, filenamelen)
#define add_event_modified(events, filename, filenamelen, dup) add_event_modified_raw(events, (dup) ? strdup(filename) : filename, filenamelen)
#define create_event_renamed_old(events, filename, filenamelen, dup) create_event_renamed_old_raw((dup) ? strdup(filename) : filename, filenamelen)
#define add_event_renamed(events, rename_event, filename, filenamelen, dup) add_event_renamed_raw(events, rename_event, (dup) ? strdup(filename) : filename, filenamelen)

#endif

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
	WatchDirEvent renamed_old = {};

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
				TRACE("#+ %s\n", filename);
				add_event_created(events, filename, filenamelen, true);
			} break;

			case FILE_ACTION_REMOVED: {
				TRACE("#- %s\n", filename);
				add_event_removed(events, filename, filenamelen, true);
			} break;

			case FILE_ACTION_MODIFIED: {
				//sprintf(pathbuff, "%s\\%s", path, filename);

				// Ignore modifications of folders.
				DWORD dwAttrib = GetFileAttributesA(filename);
				if (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY))
					break;
				TRACE("#~ %s\n", filename);
				add_event_modified(events, filename, filenamelen, true);
			} break;

			case FILE_ACTION_RENAMED_OLD_NAME: {
				TRACE("#< %s\n", filename);
				free_event(renamed_old);
				renamed_old = create_event_renamed_old(events, filename, filenamelen, true);
			} break;

			case FILE_ACTION_RENAMED_NEW_NAME: {
				TRACE("#> %s\n", filename);
				add_event_renamed(events, renamed_old, filename, filenamelen, true);
				renamed_old = {};
			} break;

			default: {
#if APP_INTERNAL
				printf("\nUnknown action %d.\n", notify->Action);
#endif
			} break;
			}

#if APP_INTERNAL
			if (notify->Action != 4)
			{
				foreach(events, [](auto* evt) {
					assert(evt->name && evt->name_size);
					assert(!(evt->created && evt->deleted));
					assert(!(evt->created && evt->modified));
					assert(!(evt->created && (evt->old_name_size > 0)));
					assert(!(evt->deleted && evt->modified));
					assert(!(evt->deleted && (evt->old_name_size > 0)));
				});

#if DEBUG_WD
				foreach(events, [](auto* evt) {
					if (evt->created)
						printf("*+ ");
					else if (evt->deleted)
						printf("*- ");
					else if (evt->modified)
						printf("*~ ");
					else if (evt->existed && evt->old_name_size)
						printf("*  ");
					else
					{
						assert(0);
						return;
					}

					if (evt->old_name_size)
						printf(" %s -> %s", evt->old_name, evt->name);
					else
						printf(" %s", evt->name);

					printf("\n");
				});
				printf("\n");
#endif
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


