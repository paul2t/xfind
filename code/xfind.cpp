// TODO:
// Look for modified files to update the index.
// Mouse click on the result list.
// Search history
// Paths history
// Extensions history
// Faster search function (see https://en.wikipedia.org/wiki/String-searching_algorithm)


#include <stdio.h>
#include <windows.h>
#include <intrin.h>
#include "imgui/src.cpp"
#define FSTRING_IMPLEMENTATION 1
#include "4coder_string.h"
#include "macros.h"
#include "utils.cpp"
#include "directory.h"


#define DEFAULT_WIDTH 1280
#define DEFAULT_HEIGHT 720
#define DEFAULT_MAXIMIZED 0

#define SHOW_RELATIVE_PATH 1
ImColor matchingColor = ImVec4(0.145f, 0.822f, 0.07f, 1.0f);
ImColor filenameColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
ImColor highlightColor = ImVec4(0.43f, 0, 0, 255);

struct FileIndex
{
	String path;
	String relpath;
	String content;
	FILETIME lastWriteTime;
};

struct Match
{
	i32 index;
	i32 lineIndex;
	i32 line_start_offset_in_file;
	i32 offset_in_line;
	i32 matching_length;
	String line;
};



struct Config
{
	String content;

	i32 width = DEFAULT_WIDTH, height = DEFAULT_HEIGHT;
	b32 maximized = DEFAULT_MAXIMIZED;
	bool showProgram = true;
	bool showFolderAndExt = true;
	bool showRelativePaths = true;
	float fontSize = 20.0f;
	String fontFile;


	String path; // Should be first String ! (same order as configKeys)
	String ext;
	String tool;
};

String configKeys[] = {
	make_lit_string("folders="),
	make_lit_string("extensions="),
	make_lit_string("tool="),
};

#define CONFIG_FILE_NAME "xfind.ini"
Config readConfig(MemoryArena& arena)
{
	Config config = {};
	config.content = readEntireFile(arena, CONFIG_FILE_NAME);
	if (config.content.size)
	{
		TempMemory _tmp(arena);
		i32 nbLines = 0;
		String* lines = getLines(arena, config.content, nbLines, true, true, true);
		String* oconfig = &config.path;
		for (i32 linei = 0; linei < nbLines; ++linei)
		{
			String line = lines[linei];
			for (int ki = 0; ki < ArrayCount(configKeys); ++ki)
			{
				if (match_part(line, configKeys[ki]))
				{
					oconfig[ki] = substr(line, configKeys[ki].size);
					break;
				}
			}
			String key = make_lit_string("window=");
			if (match_part(line, key))
			{
				char* end;
				config.width = fastStringToU32(line.str + key.size, end);
				++end;
				config.height = fastStringToU32(end, end);
				++end;
				config.maximized = fastStringToU32(end, end);
			}
			String keyf = make_lit_string("font=");
			if (match_part(line, keyf))
			{
				char* end;
				config.fontSize = strtof(line.str + keyf.size, &end);
				++end;
				config.fontFile = substr(line, (i32)(end - line.str));
			}
			if (match(line, make_lit_string("hide_program")))
				config.showProgram = false;
			if (match(line, make_lit_string("hide_folder_and_ext")))
				config.showFolderAndExt = false;
			if (match(line, make_lit_string("show_full_path")))
				config.showRelativePaths = false;
		}
	}
	return config;
}

void writeConfig(Config config)
{
	FILE* configFile = fopen(CONFIG_FILE_NAME, "w");
	if (configFile)
	{
		String* oconfig = &config.path;
		fprintf(configFile, "window=%u %u %u\n", config.width, config.height, config.maximized);
		fprintf(configFile, "font=%f %.*s\n", config.fontSize, strexp(config.fontFile));
		if (!config.showProgram)
			fprintf(configFile, "hide_program\n");
		if (!config.showFolderAndExt)
			fprintf(configFile, "hide_folder_and_ext\n");
		if (!config.showRelativePaths)
			fprintf(configFile, "show_full_path\n");
			for (int ki = 0; ki < ArrayCount(configKeys); ++ki)
		{
			fprintf(configFile, "%.*s%.*s\n", strexp(configKeys[ki]), strexp(oconfig[ki]));
		}
		fclose(configFile);
	}
}




inline FILETIME GetLastWriteTime(char* filename)
{
	FILETIME lastWriteTime = {};

	WIN32_FILE_ATTRIBUTE_DATA data;
	if (GetFileAttributesEx(filename, GetFileExInfoStandard, &data))
	{
		lastWriteTime = data.ftLastWriteTime;
	}

	return lastWriteTime;
}



struct WorkQueue;
#define WORK_QUEUE_CALLBACK(name) void name(WorkQueue *queue, void *data)
typedef WORK_QUEUE_CALLBACK(WorkQueueCallback);

struct WorkQueueEntry
{
	WorkQueueCallback* callback;
	void* data;
};

struct WorkQueue
{
	u32 volatile completionGoal;
	u32 volatile completionCount;

	u32 volatile nextEntryToWrite;
	u32 volatile nextEntryToRead;

	HANDLE semaphore;

	WorkQueueEntry entries[4096];
};

b32 executeNextWorkQueueEntry(WorkQueue* queue)
{
	b32 shouldSleep = false;

	u32 originalNextEntryToRead = queue->nextEntryToRead;
	u32 newNextEntryToRead = (originalNextEntryToRead + 1) % ArrayCount(queue->entries);
	if (originalNextEntryToRead != queue->nextEntryToWrite)
	{
		u32 index = InterlockedCompareExchange((LONG volatile *)&queue->nextEntryToRead, newNextEntryToRead, originalNextEntryToRead);
		if (index == originalNextEntryToRead)
		{
			WorkQueueEntry entry = queue->entries[index];
			entry.callback(queue, entry.data);
			InterlockedIncrement((LONG volatile *)&queue->completionCount);
		}
	}
	else
	{
		shouldSleep = true;
	}

	return shouldSleep;
}

internal void finishWorkQueue(WorkQueue* queue)
{
	while (queue->completionGoal != queue->completionCount)
	{
		executeNextWorkQueueEntry(queue);
	}

	queue->completionGoal = 0;
	queue->completionCount = 0;
}

internal void addEntryToWorkQueue(WorkQueue* queue, WorkQueueCallback* callback, void* data)
{
	// TODO: Switch to InterlockedCompareExchange eventually so that any thread can add ?
	u32 newNextEntryToWrite = (queue->nextEntryToWrite + 1) % ArrayCount(queue->entries);
	while (newNextEntryToWrite == queue->nextEntryToRead) { executeNextWorkQueueEntry(queue); }
	WorkQueueEntry *entry = queue->entries + queue->nextEntryToWrite;
	entry->callback = callback;
	entry->data = data;
	++queue->completionGoal;
	_WriteBarrier();
	queue->nextEntryToWrite = newNextEntryToWrite;
	ReleaseSemaphore(queue->semaphore, 1, 0);
}






internal i32 splitByChar(String* outputs, i32 outputsMaxSize, char* input, char c)
{
	i32 outputsSize = 0;
	char* at = input;
	String current = {};
	current.str = at;
	while (*at)
	{
		if (*at == c)
		{
			if (current.size > 0)
				outputs[outputsSize++] = current;
			++at;
			current = {};
			current.str = at;
			if (outputsSize >= outputsMaxSize)
				break;
		}
		else
		{
			++current.size;
			++current.memory_size;
			++at;
		}
	}
	if (current.size > 0)
		outputs[outputsSize++] = current;
	return outputsSize;
}


internal i32 parseExtensions(String* extensions, i32 extensionsMaxSize, char* input)
{
	return splitByChar(extensions, extensionsMaxSize, input, ';');
}


// tempArena is restored to original memory usage.
internal i32 parsePaths(MemoryArena& tempArena, String* paths, i32 pathsMaxSize, char* input, b32& allPathsExist)
{
	i32 pathsSize = splitByChar(paths, pathsMaxSize, input, ';');

	allPathsExist = pathsSize > 0;
	for (int pi = 0; pi < pathsSize; ++pi)
	{
		String& path = paths[pi];
		if (path.size > 0 && char_is_slash(path.str[path.size - 1]))
			path.size--;
		b32 pathExists = PathFileExists(tempArena, path);
		allPathsExist = allPathsExist && pathExists;
		//printf("search path: %.*s (%s)\n", strexp(path), pathExists ? "valid" : "invalid");
	}

	return pathsSize;
}

internal b32 findStringInArrayInsensitive(String* list, i32 listSize, String str)
{
	b32 found = false;
	for (i32 di = 0; di < listSize; ++di)
	{
		if (match_insensitive(list[di], str))
		{
			found = true;
			break;
		}
	}
	return found;
}


volatile b32 workerSearchPatternShouldStop;
volatile b32 workerLoadIndexShouldStop;
volatile b32 workerIndexerShouldStop;

volatile u32 indexingInProgress;
volatile u32 searchInProgress;


internal WORK_QUEUE_CALLBACK(workerLoadFileToMemory)
{
	if (workerLoadIndexShouldStop) return;
	FileIndex* fileIndex = (FileIndex*)data;
	fileIndex->lastWriteTime = GetLastWriteTime(fileIndex->path.str);
	FILE* file = fopen(fileIndex->path.str, "rb");
	if (file)
	{
		memid nitemsRead = fread(fileIndex->content.str, 1, fileIndex->content.memory_size-1, file);
		fileIndex->content.size = (i32)nitemsRead;
		fclose(file);
	}
	InterlockedDecrement(&indexingInProgress);
}

struct IndexWorkerData
{
	String* paths;
	i32 pathsSize;
	FileIndex* files;
	volatile i32* filesSize;
	String* extensions;
	i32 extensionsSize;
	i32 filesSizeLimit;
};

MemoryArena indexArena = {};
internal WORK_QUEUE_CALLBACK(workerComputeIndex)
{
	if (workerIndexerShouldStop) return;
	//u64 ticksStart = getTickCount();
	indexArena.Release();

	IndexWorkerData* wdata = (IndexWorkerData*)data;
	String* searchPaths = wdata->paths;
	i32 searchPathsSize = wdata->pathsSize;
	FileIndex* files = wdata->files;
	i32 filesSizeLimit = wdata->filesSizeLimit;
	String* searchExtensions = wdata->extensions;
	i32 searchExtensionsSize = wdata->extensionsSize;

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
					if (filesSize < filesSizeLimit)
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
							String file = pushNewString(indexArena, searchPath.size + filename.size + 1);
							append(&file, searchPath);
							append(&file, filename);
							terminate_with_null(&file);
							files[filesSize].path = file;
							files[filesSize].relpath = substr(file, searchPaths[pi].size + 1);
							umm allocSize = getFileSize(file.str) + 1;
							files[filesSize].content.memory_size = (i32)allocSize;
							files[filesSize].content.size = 0;
							files[filesSize].content.str = pushArray(indexArena, char, allocSize, pushpNoClear());
							InterlockedIncrement(&indexingInProgress);
							addEntryToWorkQueue(queue, workerLoadFileToMemory, files + filesSize);
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

	*wdata->filesSize = filesSize;

	InterlockedDecrement(&indexingInProgress);

	//u64 ticksEnd = getTickCount();
	//printf("Found %d files in %llums\n", filesSize, (ticksEnd - ticksStart));
	//printMemUsage(indexArena);
}

internal void computeIndex(IndexWorkerData& iwdata, WorkQueue* queue, String* searchPaths, i32 searchPathsSize, String* searchExtensions, i32 searchExtensionsSize, FileIndex* files, volatile i32* filesSize, i32 filesSizeLimit)
{
	//u64 ticksStart = getTickCount();
	workerIndexerShouldStop = true;
	workerLoadIndexShouldStop = true;
	workerSearchPatternShouldStop = true;
	finishWorkQueue(queue);
	workerSearchPatternShouldStop = false;
	workerLoadIndexShouldStop = false;
	workerIndexerShouldStop = false;
	//u64 ticksEnd = getTickCount();
	//printf("%llums to finish the index queue\n", ticksEnd - ticksStart);

	*filesSize = 0;

	iwdata.paths = searchPaths;
	iwdata.pathsSize = searchPathsSize;
	iwdata.extensions = searchExtensions;
	iwdata.extensionsSize = searchExtensionsSize;
	iwdata.files = files;
	iwdata.filesSize = filesSize;
	iwdata.filesSizeLimit = filesSizeLimit;
	indexingInProgress = 1;
	addEntryToWorkQueue(queue, workerComputeIndex, &iwdata);
}

internal void showInput(char* name, float maxWidth, float lablelWidth, const ImVec4& labelColor = *(ImVec4*)0)
{
	ImGui::SetCursorPosX(maxWidth + 10 - lablelWidth);
	if (&labelColor)
		ImGui::TextColored(labelColor, name);
	else
		ImGui::Text(name);
}
internal bool showInput(char* id, String& input, float width = -1, b32* setFocus = 0, const ImVec4& textColor = *(ImVec4*)0)
{
	ImGui::SameLine();
	if (setFocus && *setFocus)
	{
		ImGui::SetKeyboardFocusHere();
		*setFocus = false;
	}
	if (&textColor)
		ImGui::PushStyleColor(ImGuiCol_Text, textColor);
	ImGui::PushItemWidth(width);
	bool modified = ImGui::InputText(id, input.str, input.memory_size);
	ImGui::PopItemWidth();
	if (&textColor)
		ImGui::PopStyleColor();
	if (modified) input.size = str_size(input.str);
	return modified;
}

internal bool showInput(char* name, char* id, String& input, float maxWidth, float lablelWidth)
{
	showInput(name, maxWidth, lablelWidth);
	return showInput(id, input);
}

internal void showHighlightedText(String text, i32 highlightedOffset, i32 highlightedLen, bool sameLine = false)
{
	String beforeMatch = substr(text, 0, highlightedOffset);
	String matchingText = substr(text, highlightedOffset, highlightedLen);
	String afterMatch = substr(text, highlightedOffset + highlightedLen);


	if (beforeMatch.size)
	{
		if (sameLine)
			ImGui::SameLine(0, 0);
		sameLine = true;
		ImGui::Text("%.*s", strexp(beforeMatch));
	}
	if (matchingText.size)
	{
		if (sameLine)
			ImGui::SameLine(0, 0);
		sameLine = true;
		ImGui::TextColored(matchingColor, "%.*s", strexp(matchingText));
	}
	if (afterMatch.size)
	{
		if (sameLine)
			ImGui::SameLine(0, 0);
		sameLine = true;
		ImGui::Text("%.*s", strexp(afterMatch));
	}
}



bool
CubicUpdateFixedDuration1(float *P0, float *V0, float P1, float V1, float Duration, float dt)
{
	bool Result = false;

	if (dt > 0)
	{
		if (Duration < dt)
		{
			*P0 = P1 + (dt - Duration)*V1;
			*V0 = V1;
			Result = true;
		}
		else
		{
			float t = (dt / Duration);
			float u = (1.0f - t);

			float C0 = 1 * u*u*u;
			float C1 = 3 * u*u*t;
			float C2 = 3 * u*t*t;
			float C3 = 1 * t*t*t;

			float dC0 = -3 * u*u;
			float dC1 = -6 * u*t + 3 * u*u;
			float dC2 = 6 * u*t - 3 * t*t;
			float dC3 = 3 * t*t;

			float B0 = *P0;
			float B1 = *P0 + (Duration / 3.0f) * *V0;
			float B2 = P1 - (Duration / 3.0f) * V1;
			float B3 = P1;

			*P0 = C0 * B0 + C1 * B1 + C2 * B2 + C3 * B3;
			*V0 = (dC0*B0 + dC1 * B1 + dC2 * B2 + dC3 * B3) * (1.0f / Duration);
		}
	}

	return(Result);
}


float time = 0;
float dy;
float targetScroll = 0;

internal void showResults(Match* results, i32 resultsSize, i32 resultsSizeLimit, FileIndex* files, i32& selectedLine, bool showRelativePaths)
{
	if (resultsSize <= 0)
	{
		ImGui::TextColored(filenameColor, "No result to show.");
		return;
	}

	b32 selectionChanged = false;
	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_UpArrow)))
	{
		selectedLine--;
		selectionChanged = true;
	}
	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_DownArrow)))
	{
		selectedLine++;
		selectionChanged = true;
	}
	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_PageDown)))
	{
		selectedLine += 10;
		selectionChanged = true;
	}
	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_PageUp)))
	{
		selectedLine -= 10;
		selectionChanged = true;
	}

	if (selectedLine >= resultsSize)
		selectedLine = resultsSize - 1;

	if (selectedLine < 0)
		selectedLine = 0;

	float dt = ImGui::GetIO().DeltaTime;
	if (time > 0)
	{
		float scroll = ImGui::GetScrollY();
		CubicUpdateFixedDuration1(&scroll, &dy, targetScroll, 0.0f, time, dt);
		ImGui::SetScrollY(scroll);

		time -= dt;
		if (time < 0)
		{
			ImGui::SetScrollY(targetScroll);
			time = 0;
		}
	}
	

	float h = ImGui::GetTextLineHeightWithSpacing();
	float s = h - ImGui::GetTextLineHeight();
	ImVec2 avail = ImGui::GetContentRegionAvail();

	for (i32 ri = 0; ri < resultsSize && ri < resultsSizeLimit; ++ri)
	{
		bool highlighted = (ri == selectedLine);
		Match match = results[ri];
		FileIndex fileindex = files[match.index];
		String filename = showRelativePaths ? fileindex.relpath : fileindex.path;

		float scrollMax = ImGui::GetScrollMaxY();
		float scroll = ImGui::GetScrollY();
		if (highlighted)
		{
			ImVec2 lineStart = ImGui::GetCursorScreenPos() + ImVec2(0, -s / 2);
			ImVec2 lineEnd = lineStart + ImVec2(avail.x, h);
			ImGui::GetWindowDrawList()->AddRectFilled(lineStart, lineEnd, highlightColor);

			if (selectionChanged)
			{
				int padding = 2;
				float diffUp = (selectedLine - padding)*h - scroll;
				float diffDown = (selectedLine + 1 + padding)*h - avail.y - scroll;
				if (diffDown > 0)
				{
					//ImGui::SetScrollY((selectedLine + 1 + padding)*h - avail.y);
					targetScroll = (selectedLine + 1 + padding)*h - avail.y;
					time = 0.1f;
				}
				if (diffUp < 0)
				{
					//ImGui::SetScrollY((selectedLine - padding)*h);
					targetScroll = (selectedLine - padding)*h;
					time = 0.1f;
				}
				//ImGui::BeginTooltip();
				//ImGui::Text("%f / %f", scroll, avail.y);
				//ImGui::Text("%f -> %f", selectedLine*h, (selectedLine + 1)*h);
				//ImGui::Text("%f %f", diffDown, diffUp);
				//ImGui::EndTooltip();
			}
		}

		if (results[ri].lineIndex > 0)
		{
			ImGui::TextColored(filenameColor, "%.*s", strexp(filename));
			ImGui::SameLine(0, 0);
			ImGui::TextColored(filenameColor, "(%d): ", match.lineIndex);

			showHighlightedText(match.line, match.offset_in_line, match.matching_length, true);
		}
		else
		{
			showHighlightedText(filename, match.offset_in_line + (showRelativePaths ? 0 : (fileindex.path.size - fileindex.relpath.size)), match.matching_length);
		}
	}

	if (resultsSize >= resultsSizeLimit)
	{
		ImGui::TextColored(filenameColor, "... There are too many results, only showing the first %d ...", resultsSize);
	}
	else
	{
		ImGui::TextColored(filenameColor, "... %d results ...", resultsSize);
	}
}


struct WorkerSearchData
{
	String content;
	String pattern;
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
	FileIndex* files;
	i32 filesSize;
};

volatile u32 mainWorkerSearchPatternShouldStop;

internal void cleanWorkQueue(WorkQueue* queue, volatile u32* stopper)
{
	*stopper = true;
	finishWorkQueue(queue); // Ensure that the index has been loaded.
	_WriteBarrier();
	*stopper = false;
}

MemoryArena searchArena = {};
internal WORK_QUEUE_CALLBACK(mainWorkerSearchPattern)
{
	searchArena.Release();

	if (workerSearchPatternShouldStop)
		return;

	MainSearchPatternData* wdata = (MainSearchPatternData*)data;
	String pattern = wdata->pattern;
	FileIndex* files = wdata->files;
	i32 filesSize = wdata->filesSize;
	Match* results = wdata->results;
	i32 resultsSizeLimit = wdata->resultsSizeLimit;

	if (pattern.size > 0)
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

		if (*wdata->resultsSize < resultsSizeLimit)
		{
			// Search the file content.
			for (i32 fi = 0; fi < filesSize; ++fi)
			{
				if (workerSearchPatternShouldStop)
					return;
				String filepath = files[fi].path;
				String content = files[fi].content;


				WorkerSearchData* searchData = pushStruct(searchArena, WorkerSearchData);
				searchData->content = content;
				searchData->pattern = pattern;
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


internal void searchPattern(MainSearchPatternData* searchData, WorkQueue* queue, Match* results, volatile i32* resultsSize, u32 resultsMaxSize, FileIndex* files, i32 filesSize, String pattern)
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


void execProgram(char* program)
{
	STARTUPINFO infos = {};
	infos.cb = sizeof(STARTUPINFO);
	PROCESS_INFORMATION pinfos = {};
	CreateProcessA(0, program, 0, 0, FALSE, 0, 0, 0, &infos, &pinfos);
	CloseHandle(pinfos.hThread);
	CloseHandle(pinfos.hProcess);
}

void execOpenFile(String program, String filename, i32 fileline, i32 column)
{
	char buff[4096];
	String call = make_fixed_width_string(buff);
	for (i32 ci = 0; ci < program.size; ++ci)
	{
		char c = program.str[ci];
		if (c == '%')
		{
			char c2 = program.str[ci + 1];
			if (c2 == 'p')
			{
				append(&call, filename);
				++ci;
			}
			else if (c2 == 'l')
			{
				append_int_to_str(&call, fileline);
				++ci;
			}
			else if (c2 == 'c')
			{
				append_int_to_str(&call, column);
				++ci;
			}
			else
			{
				append(&call, c);
			}
		}
		else
		{
			append(&call, c);
		}
	}
	terminate_with_null(&call);
	execProgram(call.str);
}





struct ThreadData
{
	WorkQueue* queue;
};

internal DWORD worker_thread(void* _data)
{
	ThreadData* data = (ThreadData*)_data;
	WorkQueue* queue = data->queue;

	u32 threadid = GetCurrentThreadId();
	for (;;)
	{
		while (executeNextWorkQueueEntry(queue))
		{
			WaitForSingleObjectEx(queue->semaphore, INFINITE, FALSE);
		}
	}

	return 0;
}

#if APP_INTERNAL && 0
int main()
#else
int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#endif
{
	MemoryArena arena = {};
	Config iconfig = readConfig(arena);


	// Setup window
    if (!glfwInit())
        return 1;

    GLFWwindow* window = glfwCreateWindow(iconfig.width, iconfig.height, "xfind", NULL, NULL);
	if (window == NULL)
		return 1;
    glfwMakeContextCurrent(window);
	if (iconfig.maximized)
		glfwMaximizeWindow(window);
	glfwSwapInterval(1); // Enable vsync

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

    // Setup ImGui binding
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL2_Init();
    
    // Load Fonts
    // (there is a default font, this is only if you want to change it. see extra_fonts/README.txt for more details)
    ImGuiIO& io = ImGui::GetIO();

	ImGui::StyleColorsDark();
    ImVec4 clear_color = ImColor(114, 144, 154);


	SYSTEM_INFO sysinfos = {};
	GetSystemInfo(&sysinfos);
	i32 nbLogicalCores = sysinfos.dwNumberOfProcessors;
	//printf("%d cores on this machine\n", nbLogicalCores);

	WorkQueue workQueue = {};
	ThreadData* threadsData = pushArray(arena, ThreadData, nbLogicalCores);
	workQueue.semaphore = CreateSemaphoreEx(0, 0, nbLogicalCores, 0, 0, SEMAPHORE_ALL_ACCESS);
	for (i32 i = 0; i < nbLogicalCores; ++i)
	{
		threadsData[i].queue = &workQueue;
		HANDLE threadHandle = CreateThread(0, 0, worker_thread, threadsData + i, 0, 0);
		CloseHandle(threadHandle);
	}


	Config config = iconfig;
	config.path = pushNewString(arena, 4 * 4096);
	copy(&config.path, iconfig.path);
	terminate_with_null(&config.path);

	config.ext = pushNewString(arena, 4096);
	copy(&config.ext, iconfig.ext);
	if (!config.ext.size)
		copy(&config.ext, make_lit_string("c;cpp;h;hpp;"));
	terminate_with_null(&config.ext);

	config.tool = pushNewString(arena, 4096);
	copy(&config.tool, iconfig.tool);
	if (!config.tool.size)
		copy(&config.tool, make_lit_string("C:\\Program Files\\Sublime Text 3\\sublime_text.exe %p:%l"));
	terminate_with_null(&config.tool);

	String searchBuffer = pushNewString(arena, 4096);

	b32 running = true;
	b32 setFocusToFolderInput = !config.path.size;
	b32 setFocusToSearchInput = !setFocusToFolderInput;
	i32 selectedLine = 0;

	i32 searchPathsSizeMax = 1024;
	String* searchPaths = pushArray(arena, String, searchPathsSizeMax);
	b32 searchPathExists = false;
	i32 searchPathsSize = parsePaths(arena, searchPaths, searchPathsSizeMax, config.path.str, searchPathExists);

	i32 extensionsMaxSize = 1024;
	String* extensions = pushArray(arena, String, extensionsMaxSize);
	i32 extensionsSize = parseExtensions(extensions, extensionsMaxSize, config.ext.str);

	IndexWorkerData iwData = {};
	i32 filesMaxSize = 1024*1024;
	FileIndex* files = pushArray(arena, FileIndex, filesMaxSize);
	volatile i32 filesSize = 0;
	if (searchPathExists)
	{
		computeIndex(iwData, &workQueue, searchPaths, searchPathsSize, extensions, extensionsSize, files, &filesSize, filesMaxSize);
	}

	i32 resultsMaxSize = 100;
	Match* results = pushArray(arena, Match, resultsMaxSize+1);
	volatile i32 resultsSize = 0;
	b32 needToSearchAgain = false;

	MainSearchPatternData mainSearch = {};

	float oldFontSize = 0;
	config.fontSize = iconfig.fontSize;
	if (config.fontSize < 5)
		config.fontSize = 20;
	config.fontFile = iconfig.fontFile;
	if (config.fontFile.size <= 0)
		config.fontFile = make_lit_string("liberation-mono.ttf");
	b32 showProgram = true;

    // Main loop
    while (running)
    {
		if (config.fontSize < 6)
			config.fontSize = 6;
		if (config.fontSize != oldFontSize)
		{
			ImGui_ImplOpenGL2_DestroyFontsTexture();
			io.Fonts->ClearFonts();
			io.Fonts->AddFontFromFileTTF(config.fontFile.str, config.fontSize);
			oldFontSize = config.fontSize;
		}

        // NOTE(xf4): Save will happen when leaving.
        running = glfwWindowShouldClose(window) == 0;
        glfwPollEvents();
        
        int windowWidth, windowHeight;
        glfwGetFramebufferSize(window, &windowWidth, &windowHeight);

		ImGui_ImplOpenGL2_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
        ImGuiContext& g = *GImGui;
        
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Exit"))
                {
                    running = false;
                }
                ImGui::EndMenu();
            }
			if (ImGui::BeginMenu("Options"))
			{
				ImGui::DragFloat("Font", &config.fontSize, 0.2f, 6, 38);
				ImGui::Checkbox("Edit Program command", &config.showProgram);
				ImGui::Checkbox("Edit Folders and Extensions", &config.showFolderAndExt);
				ImGui::Checkbox("Show Relative Paths", &config.showRelativePaths);
				ImGui::EndMenu();
			}
            ImGui::EndMainMenuBar();
        }

		bool inputModified = false;
		bool searchModified = false;

		if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter), false) || ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape), false))
			setFocusToSearchInput = true;



		if (0 <= selectedLine && selectedLine < resultsSize)
		{
			Match match = results[selectedLine];
			String file = files[match.index].path;

			if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_C), false) && io.KeyCtrl)
			{
				//copyToClipboard(file);
			}

			if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter)) && 0 <= selectedLine && selectedLine < resultsSize)
			{
				execOpenFile(config.tool, file, match.lineIndex, match.offset_in_line+1);
			}
		}


        //glfwGetWindowSize(window, &windowWidth, &windowHeight);
        ImGui::SetNextWindowSize(ImVec2((float)windowWidth, (float)windowHeight - 26.0f));
        ImGui::SetNextWindowPos(ImVec2(0.0f, 26.0f));
        ImGui::Begin("MainWindow", 0, ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize);
		{
			/*
			ImGui::ColorEdit3("highlight", &highlightColor.Value.x);
			ImGui::ColorEdit3("matching", &matchingColor.Value.x);
			ImGui::ColorEdit3("filename", &filenameColor.Value.x);*/
			char texts[4] = {};
			float textsSize[4] = {};

			char* textsearch = searchInProgress ? "Searching:" : "Search:";
			char* textFolders = indexingInProgress ? "Indexing:" : "Folders:";
			float w0 = ImGui::CalcTextSize("Program:").x;
			float w1 = ImGui::CalcTextSize(textFolders).x;
			float w2 = ImGui::CalcTextSize("Extensions:").x;
			float w3 = ImGui::CalcTextSize(textsearch).x;
			float w = w0;
			if (w < w1) w = w1;
			if (w < w2) w = w2;
			if (w < w3) w = w3;

			const ImVec4& defaultTextColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
			ImVec4 redLabel = ImVec4(0.8f, 0.2f, 0.2f, 1);

			b32 modifProgram = false;
			b32 modifFolders = false;
			b32 modifExtensions = false;
			b32 modifSearch = false;

			if (config.showProgram)
			{
				modifProgram = showInput("Program:", "##Program", config.tool, w, w0);
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::Text("%%p = path of the file to open");
					ImGui::Text("%%l = line in the file to open");
					ImGui::Text("%%c = column in the file to open");
					ImGui::Text("Make sure the path is contained withing quotes");
					ImGui::Text("For Sublime Text, the arguments are : \"%%p:%%l:%%c\"");
					ImGui::Text("You can hide this input in the options");
					ImGui::EndTooltip();
				}
			}

			if (config.showFolderAndExt)
			{
				char filesSizeBuffer[32];
				snprintf(filesSizeBuffer, sizeof(filesSizeBuffer), "%d files", filesSize);
				float filesSizeWidth = ImGui::CalcTextSize(filesSizeBuffer).x;

				showInput(textFolders, w, w1, indexingInProgress ? redLabel : defaultTextColor);
				modifFolders = showInput("##Folders", config.path, !indexingInProgress ? -filesSizeWidth - ImGui::GetTextLineHeight() : -1.0f, &setFocusToFolderInput, searchPathExists ? defaultTextColor : redLabel);
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::Text("You can open multiple folders by separating them with semicolons ';'");
					ImGui::EndTooltip();
				}
				if (!indexingInProgress)
				{
					ImGui::SameLine();
					ImGui::Text(filesSizeBuffer);
				}

				modifExtensions = showInput("Extensions:", "##Extensions", config.ext, w, w2);
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::Text("You can select multiple extensions by separating them with semicolons ';'");
					ImGui::EndTooltip();
				}
			}

			showInput(textsearch, w, w3, searchInProgress ? redLabel : defaultTextColor);
			modifSearch = showInput("##Search", searchBuffer, -1, &setFocusToSearchInput);
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::Text("No regex nor wildcards for now. Sorry... Maybe later.");
				ImGui::EndTooltip();
			}

			inputModified = modifProgram || modifExtensions || modifFolders || modifSearch;
			searchModified = modifSearch;
			if (modifFolders)
			{
				searchPathsSize = parsePaths(arena, searchPaths, searchPathsSizeMax, config.path.str, searchPathExists);
			}

			if (modifExtensions)
			{
				extensionsSize = parseExtensions(extensions, extensionsMaxSize, config.ext.str);
			}

			if (modifFolders || modifExtensions)
			{
				if (searchPathExists)
				{
					resultsSize = 0;
					computeIndex(iwData, &workQueue, searchPaths, searchPathsSize, extensions, extensionsSize, files, &filesSize, filesMaxSize);
					needToSearchAgain = true;
				}
			}
		}

		if (!indexingInProgress && (needToSearchAgain || searchModified) && searchPathExists)
		{
			searchPattern(&mainSearch, &workQueue, results, &resultsSize, resultsMaxSize, files, filesSize, searchBuffer);
			needToSearchAgain = false;
			selectedLine = 0;
		}

		ImGui::BeginChild("Results", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
		{
			showResults(results, resultsSize, resultsMaxSize, files, selectedLine, config.showRelativePaths);
		}
		ImGui::EndChild();

		ImGui::End();





		if (inputModified || !running)
		{
			WINDOWPLACEMENT wplacement = {};
			GetWindowPlacement(glfwGetWin32Window(window), &wplacement);
			config.maximized = (wplacement.showCmd == SW_SHOWMAXIMIZED);
			if (!config.maximized)
			{
				glfwGetWindowSize(window, &config.width, &config.height);
			}

			writeConfig(config);
		}
        
        
        
        
        
        

		// Rendering
		ImGui::Render();
		int display_w, display_h;
		glfwGetFramebufferSize(window, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
		glClear(GL_COLOR_BUFFER_BIT);
		//glUseProgram(0); // You may want this if using this code in an OpenGL 3+ context where shaders may be bound, but prefer using the GL3+ code.
		ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

		glfwMakeContextCurrent(window);
		glfwSwapBuffers(window);
    }

	// Cleanup
	ImGui_ImplOpenGL2_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwDestroyWindow(window);
	glfwTerminate();
    
    return 0;
}
