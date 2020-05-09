#pragma once

#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")

#include <stdio.h>
#include <windows.h>
#include <intrin.h>
#include "imgui/src.cpp"
#define FSTRING_IMPLEMENTATION 1
#include "4coder_string.h"
#include "macros.h"
#include "utils.cpp"
#include "directory.h"
#include "threads.h"
#include "search_state_machine.cpp"
#include "watch_directory.h"


const char argChar = '%';
#define ARG_PATH "%p"
#define ARG_LINE "%l"
#define ARG_COL "%c"
#define ARG_HOME "%HOME%"

struct ProgramString
{
	String name;
	String command;
	b32 untested;
};

static ProgramString programStrings[] =
{
	{ make_lit_string("Sublime Text 3"), make_lit_string("\"C:\\Program Files\\Sublime Text 3\\sublime_text.exe\" \""ARG_PATH":"ARG_LINE":"ARG_COL"\""), },
	{ make_lit_string("Notepad++"), make_lit_string("\"C:\\Program Files (x86)\\Notepad++\\notepad++.exe\" \""ARG_PATH"\" -n"ARG_LINE" -c"ARG_COL), },
	{ make_lit_string("VS Code"), make_lit_string("\""ARG_HOME"\\AppData\\Local\\Programs\\Microsoft VS Code\\Code.exe\" -g \""ARG_PATH":"ARG_LINE":"ARG_COL"\""), },
	{ make_lit_string("Emacs"), make_lit_string("emacsclient +"ARG_LINE":"ARG_COL" \""ARG_PATH"\""), true, },
	{ make_lit_string("GVim"), make_lit_string("gvim '+normal "ARG_LINE"G"ARG_COL"|' \""ARG_PATH"\""), true, },
};

#define XFIND_VERSION 0.5f
#define XFIND_VERSION_STRING "alpha 0.5"
#define XFIND_APP_TITLE "xfind " XFIND_VERSION_STRING


#define DEFAULT_WINDOW_WIDTH 1280
#define DEFAULT_WINDOW_HEIGHT 720
#define DEFAULT_MAXIMIZED 0

#define SHOW_RELATIVE_PATH 1
static ImColor matchingColor = ImVec4(0.145f, 0.822f, 0.07f, 1.0f);
static ImColor filenameColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
static ImColor highlightColor = ImVec4(0.43f, 0, 0, 255);



struct Config
{
#define CONFIG_LATEST_VERSION 1
	String content;
	i32 version = 0;

	i32 width = DEFAULT_WINDOW_WIDTH, height = DEFAULT_WINDOW_HEIGHT;
	b32 maximized = DEFAULT_MAXIMIZED;
	bool showProgram = true;
	bool showFolderAndExt = true;
	bool showRelativePaths = true;
	bool showHiddenFiles = false;
	bool searchFileNames = true;
	bool caseSensitive = false;
	float fontSize = 20.0f;
	String fontFile;
	i32 contextLines = 4;
	bool showContextLines = false;
	bool showContextLinesOnMouse = false;


	String path; // Should be first String ! (same order as configKeys)
	String ext;
	String tool;
};

static String configKeys[] = {
	make_lit_string("folders="),
	make_lit_string("extensions="),
	make_lit_string("tool="),
};



struct FileIndexEntry
{
	String path = {};
	String relpath = {};
	String content = {};
	FILETIME lastWriteTime = {};
	FileIndexEntry* next = 0;
	FileIndexEntry* nextInPathHash = 0;
	b32 truncated = false;
	b32 seenInIndex = false;
	volatile b32 modifiedSinceLastSearch = false;
	MutexRW mutex = {};
};

#define PATH_HASH_SIZE 4096
struct FileIndex
{
	FileIndexEntry* firstFile = 0;
	FileIndexEntry** filePathHash;
	u32 filePathHashSize = 0;
	volatile i32 filesSize = 0;
	volatile b32 modifiedSinceLastSearch = false;
	MutexRW mutex = {};
};


struct Match
{
	FileIndexEntry* file = 0;
	i32 lineIndex = 0;
	i32 line_start_offset_in_file = 0;
	i32 offset_in_line = 0;
	i32 matching_length = 0;
	String line = {};
};



struct MainSearchPatternData
{
	String pattern = {};
	volatile i32* resultsSize = 0;
	volatile b32* waiting_for_event = 0;
	Match* results = 0;
	i32 resultsSizeLimit = 0;
	FileIndex* fileIndex = 0;
	struct State* state = 0;
};

struct EventListItem
{
	WatchDirEvent evt;
	EventListItem* next;
};

struct State
{
	MemoryArena arena = {};
	Config config = {};
	ThreadPool pool = {};

	MemoryArena watchArena = {};
	ThreadPool dirWatchThread = {};
	char** watchPaths = 0;
	i32 watchPathsSize = 0;
	WatchDir wd = {};
	HANDLE wdSemaphore = 0;
	EventListItem* file_events_list = 0;

	b32 running = true;

	b32 setFocusToFolderInput = false;
	b32 setFocusToSearchInput = false;
	b32 selectSearchInputText = false;
	b32 shouldWaitForEvent = false;
	volatile b32 waiting_for_event = false;
	i32 selectedLine = 0;

	FileIndex index = {};

	Match* results = 0;
	volatile i32 resultsMaxSize = 1000;
	volatile i32 resultsSize = 0;

	i32 searchPathsSizeMax = 1024;
	String* searchPaths = 0;
	b32 searchPathExists = 0;
	i32 searchPathsSize = 0;
	MutexRW searchPathsMutex = {};

	i32 extensionsMaxSize = 1024;
	String* extensions = 0;
	i32 extensionsSize = 0;

	String searchBuffer = {};
	MemoryArena searchArena = {};
	b32 needToSearchAgain = false;
	volatile b32 needToGenerateIndex = false;


	MainSearchPatternData mainSearch = {};

	bool useSSM = true;

	b32 showAbout = false;
};

#if APP_INTERNAL
internal volatile u64 indexTimeStart = 0;
internal volatile u64 indexTime = 0;
internal volatile u64 treeTraversalTimeStart = 0;
internal volatile u64 treeTraversalTime = 0;
internal volatile u64 searchTimeStart = 0;
internal volatile u64 searchTime = 0;
#endif


#include "xfind_config.cpp"

#include "imgui_utils.cpp"
#include "xfind_utils.cpp"
#include "xfind_ui.cpp"
#include "xfind_index.cpp"
#include "xfind_worker_index.cpp"
#include "xfind_worker_search.cpp"

