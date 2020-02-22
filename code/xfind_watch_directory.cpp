

#if 0
volatile b32 watchPathsChanged = false;

WORK_QUEUE_CALLBACK(directory_watcher)
{
	State* state = (State*)data;
	String* paths = state->watchPaths;
	i32 pathsSize = state->watchPathsSize;
	static HANDLE semaphore = CreateSemaphoreExA(0, 0, 1, 0, 0, SEMAPHORE_ALL_ACCESS);

	char** cpaths = new char*[pathsSize];
	for (int i = 0; i < pathsSize; ++i)
	{
		terminate_with_null(&paths[i]);
		cpaths[i] = paths[i].str;
	}

	WatchDir wd = watchdir_start(cpaths, pathsSize, semaphore);
	for (;;)
	{
		WatchDirEvent* evt = watchdir_get_event(wd);
		if (watchPathsChanged) break;

		assert(evt);
		if (!evt) break;

		assert(evt->name && evt->name_size);
		assert(!(evt->created && evt->deleted));
		assert(!(evt->created && evt->modified));
		assert(!(evt->created && (evt->old_name_size > 0)));
		assert(!(evt->deleted && evt->modified));
		assert(!(evt->deleted && (evt->old_name_size > 0)));

#if DEBUG_WD
		printf("=");
#endif

		if (evt->created)
			printf("+ ");
		else if (evt->deleted)
			printf("- ");
		else if (evt->modified)
			printf("~ ");
		else if (evt->existed && evt->old_name_size)
			printf("  ");
		else
		{
			assert(0);
			continue;
		}

		if (evt->old_name_size)
			printf(" %s -> %s", evt->old_name, evt->name);
		else
			printf(" %s", evt->name);

		printf("\n");
#if DEBUG_WD
		printf("\n");
#endif

		state->needToGenerateIndex = true;
	}
	watchdir_stop(wd);

	delete[] cpaths;
}
#endif

void updateWatchedDirectories(State& state)
{
	watchdir_stop(state.wd);
	if (state.searchPathExists && state.searchPaths && state.searchPathsSize > 0)
	{
		state.watchArena.Release();
		state.watchPaths = pushArray(state.watchArena, char*, state.searchPathsSize);
		for (i32 i = 0; i < state.searchPathsSize; ++i)
		{
			state.watchPaths[i] = pushStringZeroTerminated(state.watchArena, state.searchPaths[i]).str;
		}
		state.watchPathsSize = state.searchPathsSize;

		state.wd = watchdir_start(state.watchPaths, state.watchPathsSize);
	}
}


