

volatile b32 watchPathsChanged = false;

WORK_QUEUE_CALLBACK(directory_watcher)
{
	State* state = (State*)data;
	String* paths = state->watchPaths;
	i32 pathsSize = state->watchPathsSize;

	char** cpaths = new char*[pathsSize];
	for (int i = 0; i < pathsSize; ++i)
	{
		terminate_with_null(&paths[i]);
		cpaths[i] = paths[i].str;
	}

	WatchDir wd = watchdir_start(cpaths, pathsSize);
	for (;;)
	{
		WatchDirEvent* evt = watchdir_get_event(wd);
		if (watchPathsChanged) break;

		assert(evt);
		if (!evt) continue;

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


