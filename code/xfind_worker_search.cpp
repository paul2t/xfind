
struct WorkerSearchData
{
	String content;
	String pattern;
	FileIndexEntry* file;
	Match* results;
	volatile i32* resultsSize;
	i32 resultsSizeLimit;
	i32 fileIndex;
	b32 trackLineIndex;
};


internal WORK_QUEUE_CALLBACK(workerSearchPattern)
{
	if (workerSearchPatternShouldStop)
		return;

	WorkerSearchData* wdata = (WorkerSearchData*)data;
	b32 trackLineIndex = wdata->trackLineIndex;
	String content = wdata->content;
	i32 resultsSizeLimit = wdata->resultsSizeLimit;
	Match* results = wdata->results;
	String pattern = wdata->pattern;
	i32 fi = wdata->fileIndex;

	if (*wdata->resultsSize >= resultsSizeLimit)
	{
		InterlockedDecrement(&searchInProgress);
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

	i32 lineIndex = 1;
	char* linestart = content.str;
	for (int ati = 0; ati < content.size; ++ati)
	{
		if (workerSearchPatternShouldStop)
			break;

		char* at = content.str + ati;
		if (match_part_insensitive(at, pattern))
		{
			i32 resultIndex = (i32)InterlockedIncrement((u32*)wdata->resultsSize) - 1;
			if (resultIndex >= resultsSizeLimit)
			{
				*wdata->resultsSize = resultsSizeLimit;
				break;
			}

			Match match = {};
			match.index = fi;
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

	InterlockedDecrement(&searchInProgress);
}

struct MainSearchPatternData
{
	String pattern;
	volatile i32* resultsSize;
	Match* results;
	i32 resultsSizeLimit;
	FileIndexEntry* files;
	i32 filesSize;
};

volatile u32 mainWorkerSearchPatternShouldStop;

MemoryArena searchArena = {};
internal WORK_QUEUE_CALLBACK(mainWorkerSearchPattern)
{
	searchArena.Release();

	if (workerSearchPatternShouldStop)
		return;

	MainSearchPatternData* wdata = (MainSearchPatternData*)data;
	String pattern = wdata->pattern;
	FileIndexEntry* files = wdata->files;
	i32 filesSize = wdata->filesSize;
	Match* results = wdata->results;
	i32 resultsSizeLimit = wdata->resultsSizeLimit;

	if (pattern.size > 0)
	{
		if (state.config.searchFileNames)
		{
			// Search the file name
			for (i32 fi = 0; fi < filesSize; ++fi)
			{
				if (workerSearchPatternShouldStop)
					return;

				WorkerSearchData* searchData = pushStruct(searchArena, WorkerSearchData);
				searchData->content = files[fi].relpath;
				searchData->pattern = pattern;
				searchData->results = results;
				searchData->resultsSize = wdata->resultsSize;
				searchData->resultsSizeLimit = resultsSizeLimit;
				searchData->fileIndex = fi;
				searchData->trackLineIndex = false;

				InterlockedIncrement(&searchInProgress);
				addEntryToWorkQueue(queue, workerSearchPattern, searchData);
			}
		}

		if (*wdata->resultsSize < resultsSizeLimit)
		{
			// Search the file content.
			for (i32 fi = 0; fi < filesSize; ++fi)
			{
				if (workerSearchPatternShouldStop)
					return;
				String filepath = files[fi].path;
				String content = files[fi].content;

				if (content.size <= 0)
					continue;

				WorkerSearchData* searchData = pushStruct(searchArena, WorkerSearchData);
				searchData->content = content;
				searchData->pattern = pattern;
				searchData->file = files + fi;
				searchData->results = results;
				searchData->resultsSize = wdata->resultsSize;
				searchData->resultsSizeLimit = resultsSizeLimit;
				searchData->fileIndex = fi;
				searchData->trackLineIndex = true;

				InterlockedIncrement(&searchInProgress);
				addEntryToWorkQueue(queue, workerSearchPattern, searchData);
			}
		}
	}

	InterlockedDecrement(&searchInProgress);
}


internal void searchForPatternInFiles(MainSearchPatternData* searchData, WorkQueue* queue, Match* results, volatile i32* resultsSize, u32 resultsMaxSize, FileIndexEntry* files, i32 filesSize, String pattern)
{
	{
		//u64 ticksStart = getTickCount();
		// Ensure that the index has been loaded. And force the old search to abort.
		cleanWorkQueue(queue, &workerSearchPatternShouldStop);
		*resultsSize = 0;
		//u64 ticksEnd = getTickCount();
		//printf("%llums to finish the search queue\n", ticksEnd - ticksStart);
	}

	if (pattern.size > 0)
	{

		//u64 ticksStart = getTickCount();

		searchData->files = files;
		searchData->filesSize = filesSize;
		searchData->pattern = pattern;
		searchData->results = results;
		searchData->resultsSize = resultsSize;
		searchData->resultsSizeLimit = resultsMaxSize;

		searchInProgress = 1;
		addEntryToWorkQueue(queue, mainWorkerSearchPattern, searchData);

		//u64 ticksEnd = getTickCount();
		//printf("%llums to launch the search\n", ticksEnd - ticksStart);
	}
}
