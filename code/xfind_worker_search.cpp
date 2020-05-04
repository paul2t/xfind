
struct WorkerSearchData
{
	String content;
	String pattern;
	SearchStateMachine ssm;
	FileIndexEntry* file;
	Match* results;
	volatile i32* resultsSize;
	i32 resultsSizeLimit;
	b32 trackLineIndex;
	b32 caseSensitive;
	volatile b32* waiting_for_event;
};


internal WORK_QUEUE_CALLBACK(workerSearchPattern)
{
	if (workerSearchPatternShouldStop)
		return;
	// not thread safe TIMED_FUNCTION();

	WorkerSearchData* wdata = (WorkerSearchData*)data;
	b32 trackLineIndex = wdata->trackLineIndex;
	String content = wdata->content;
	i32 resultsSizeLimit = wdata->resultsSizeLimit;
	Match* results = wdata->results;
	String pattern = wdata->pattern;
	SearchStateMachine ssm = wdata->ssm;
	b32 caseSensitive = wdata->caseSensitive;

	if (*wdata->resultsSize >= resultsSizeLimit)
	{
		u32 test = InterlockedDecrement(&searchInProgress);
		DEBUG_TEST_TIMER(!test, searchTimeStart, searchTime);

		post_empty_event(wdata->waiting_for_event);
		return;
	}

	FileIndexEntry* filei = wdata->file;
	if (filei)
	{
		if (workerSearchPatternShouldStop)
			return;

		FILETIME lastWriteTime = GetLastWriteTime(filei->path.str);
		if (CompareFileTime(&filei->lastWriteTime, &lastWriteTime))
		{
			ScopeMutexWrite(&filei->mutex);

			//printf("file %s has been modified since last index.\n", filei->path.str);
			FILE* file = fopen(filei->path.str, "rb");
			if (file)
			{
				memid nitemsRead = fread(filei->content.str, 1, filei->content.memory_size - 1, file);
				filei->content.size = (i32)nitemsRead;
				terminate_with_null(&filei->content);
				fclose(file);
			}

			filei->lastWriteTime = lastWriteTime;
		}
	}

#if APP_INTERNAL
	if (ssm.states)
#endif
	{
		for (SearchResult res = searchPattern(content, ssm); res.match.size; res = searchPattern(content, ssm, res))
		{
			i32 resultIndex = (i32)InterlockedIncrement((u32*)wdata->resultsSize) - 1;
			if (resultIndex >= resultsSizeLimit)
			{
				*wdata->resultsSize = resultsSizeLimit;
				break;
			}

			Match match = {};
			match.file = filei;
			match.lineIndex = 0;
			match.offset_in_line = (i32)(res.match.str - res.linestart);
			match.matching_length = pattern.size;

			if (trackLineIndex)
			{
				match.lineIndex = res.lineIndex;

				match.line_start_offset_in_file = (i32)(res.linestart - content.str);

				match.line.str = res.linestart;
				match.line.size = match.offset_in_line + match.matching_length;
				while (match.line.str[match.line.size] && match.line.str[match.line.size] != '\n' && match.line.str[match.line.size] != '\r') ++match.line.size;

				// We only need one match per line : Skip until the end of the line.
				char* at = match.line.str + match.line.size + 1;
				if (match.line.str[match.line.size] == '\r') ++at;

				res.state = ssm.states;
				res.match.str = at;
				res.match.size = 0;
				res.linestart = at;
				res.lineIndex++;
			}

			results[resultIndex] = match;
			if (!trackLineIndex || resultIndex >= resultsSizeLimit)
				break;
		}
	}
#if APP_INTERNAL
	else
	{
		i32 lineIndex = 1;
		char* linestart = content.str;
		for (int ati = 0; ati < content.size; ++ati)
		{
			if (workerSearchPatternShouldStop)
				break;

			char* at = content.str + ati;
			if (caseSensitive ? match_part(at, pattern) : match_part_insensitive(at, pattern))
			{
				i32 resultIndex = (i32)InterlockedIncrement((u32*)wdata->resultsSize) - 1;
				if (resultIndex >= resultsSizeLimit)
				{
					*wdata->resultsSize = resultsSizeLimit;
					break;
				}

				Match match = {};
				match.file = filei;
				match.lineIndex = 0;
				match.offset_in_line = (i32)(at - linestart);
				match.matching_length = pattern.size;

				if (trackLineIndex)
				{
					match.lineIndex = lineIndex;

					match.line_start_offset_in_file = (i32)(linestart - content.str);
					match.line.str = linestart;
					match.line.size = match.offset_in_line + match.matching_length;
					while (match.line.str[match.line.size] && match.line.str[match.line.size] != '\n' && match.line.str[match.line.size] != '\r') ++match.line.size;
					ati = match.line_start_offset_in_file + match.line.size;
					if (content.str[ati] == '\r') ++ati;
				}

				results[resultIndex] = match;
				if (!trackLineIndex || resultIndex >= resultsSizeLimit)
					break;
			}

			if (trackLineIndex && content.str[ati] == '\n')
			{
				++lineIndex;
				linestart = content.str + ati + 1;
			}
		}
	}
#endif

	u32 test = InterlockedDecrement(&searchInProgress);
	DEBUG_TEST_TIMER(!test, searchTimeStart, searchTime);

	post_empty_event(wdata->waiting_for_event);
}

volatile u32 mainWorkerSearchPatternShouldStop;

internal WORK_QUEUE_CALLBACK(mainWorkerSearchPattern)
{
	if (workerSearchPatternShouldStop)
		return;
	// Not thread safe TIMED_FUNCTION();

	MainSearchPatternData* wdata = (MainSearchPatternData*)data;
	String pattern = wdata->pattern;
	FileIndex* fileIndex = wdata->fileIndex;
	Match* results = wdata->results;
	i32 resultsSizeLimit = wdata->resultsSizeLimit;
	State* state = wdata->state;
	state->searchArena.Release();


	if (pattern.size > 0)
	{
		SearchStateMachine ssm = {};
		if (state->useSSM)
			ssm = buildSearchStateMachine(state->searchArena, pattern, !state->config.caseSensitive);

		if (state->config.searchFileNames)
		{
			// Search the file name
			for (FileIndexEntry* file = state->index.firstFile; file; file = file->next)
			{
				if (workerSearchPatternShouldStop)
					return;

				WorkerSearchData* searchData = pushStruct(state->searchArena, WorkerSearchData);
				searchData->content = file->relpath;
				searchData->pattern = pattern;
				searchData->ssm = ssm;
				searchData->results = results;
				searchData->resultsSize = wdata->resultsSize;
				searchData->resultsSizeLimit = resultsSizeLimit;
				searchData->file = file;
				searchData->trackLineIndex = false;
				searchData->caseSensitive = state->config.caseSensitive;
				searchData->waiting_for_event = &state->waiting_for_event;

				InterlockedIncrement(&searchInProgress);
				addEntryToWorkQueue(queue, workerSearchPattern, searchData);
			}
		}

		if (*wdata->resultsSize < resultsSizeLimit)
		{
			// Search the file content.
			for (FileIndexEntry* file = state->index.firstFile; file; file = file->next)
			{
				if (workerSearchPatternShouldStop)
					return;
				String filepath = file->path;
				String content = file->content;

				if (content.size <= 0)
					continue;

				WorkerSearchData* searchData = pushStruct(state->searchArena, WorkerSearchData);
				searchData->content = content;
				searchData->pattern = pattern;
				searchData->ssm = ssm;
				searchData->file = file;
				searchData->results = results;
				searchData->resultsSize = wdata->resultsSize;
				searchData->resultsSizeLimit = resultsSizeLimit;
				searchData->trackLineIndex = true;
				searchData->caseSensitive = state->config.caseSensitive;
				searchData->waiting_for_event = &state->waiting_for_event;

				InterlockedIncrement(&searchInProgress);
				addEntryToWorkQueue(queue, workerSearchPattern, searchData);
			}
		}
	}

	u32 test = InterlockedDecrement(&searchInProgress);
	DEBUG_TEST_TIMER(!test, searchTimeStart, searchTime);

	post_empty_event(wdata->waiting_for_event);
}


internal void searchForPatternInFiles(MainSearchPatternData* searchData, State* state, WorkQueue* queue, Match* results, volatile i32* resultsSize, u32 resultsMaxSize, FileIndex* fileIndex, String pattern)
{
	TIMED_FUNCTION();
#if APP_INTERNAL
	searchTimeStart = GetTickCount64();
#endif

	{
		// Ensure that the index has been loaded. And force the old search to abort.
		cleanWorkQueue(queue, &workerSearchPatternShouldStop);
		*resultsSize = 0;
	}

	if (pattern.size > 0)
	{
		searchData->fileIndex = fileIndex;
		searchData->pattern = pattern;
		searchData->results = results;
		searchData->resultsSize = resultsSize;
		searchData->resultsSizeLimit = resultsMaxSize;
		searchData->state = state;

		searchInProgress = 1;
		addEntryToWorkQueue(queue, mainWorkerSearchPattern, searchData);
	}
}
