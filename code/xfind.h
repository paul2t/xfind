#pragma once


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


const char argChar = '?';
#define ARG_PATH "?p"
#define ARG_LINE "?l"
#define ARG_COL "?c"
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
	{ make_lit_string("VS Code"), make_lit_string("\"%HOME%\\AppData\\Local\\Programs\\Microsoft VS Code\\Code.exe\" -g \""ARG_PATH":"ARG_LINE":"ARG_COL"\""), },
	{ make_lit_string("Emacs"), make_lit_string("emacsclient +"ARG_LINE":"ARG_COL" \""ARG_PATH"\""), true, },
	{ make_lit_string("GVim"), make_lit_string("gvim '+normal "ARG_LINE"G"ARG_COL"|' \""ARG_PATH"\""), true, },
};




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
};

struct FileIndex
{
	MemoryArena arena = {};
	FileIndexEntry* firstFile = 0;
	FileIndexEntry** onePastLastFile = 0;
	volatile i32 filesSize = 0;
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
	Match* results = 0;
	i32 resultsSizeLimit = 0;
	FileIndex* fileIndex = 0;
	struct State* state = 0;
};

struct State
{
	MemoryArena arena = {};
	Config config = {};
	ThreadPool pool = {};
	
	b32 running = true;

	b32 setFocusToFolderInput = false;
	b32 setFocusToSearchInput = false;
	b32 shouldWaitForEvent = false;
	i32 selectedLine = 0;

	FileIndex index = {};

	Match* results = 0;
	volatile i32 resultsMaxSize = 1000;
	volatile i32 resultsSize = 0;

	i32 searchPathsSizeMax = 1024;
	String* searchPaths = 0;
	b32 searchPathExists = 0;
	i32 searchPathsSize = 0;

	i32 extensionsMaxSize = 1024;
	String* extensions = 0;
	i32 extensionsSize = 0;

	String searchBuffer = {};
	MemoryArena searchArena = {};
	b32 needToSearchAgain = false;
	b32 needToGenerateIndex = false;


	MainSearchPatternData mainSearch = {};

	bool useSSM = true;
};

#if APP_INTERNAL
internal volatile u64 searchTimeStart = 0;
internal volatile u64 searchTime = 0;
#endif



#include "xfind_config.cpp"

#include "imgui_utils.cpp"
#include "xfind_utils.cpp"
#include "xfind_ui.cpp"
#include "xfind_worker_index.cpp"
#include "xfind_worker_search.cpp"

