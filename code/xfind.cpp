// TODO:
// Show a few lines above and below the selected result.
// Look for modified files to update the index. (partial)
	// Should only update the files that have been modified.
// Search history : 
	// ImGuiInputTextFlags_CallbackHistory = 1 << 7,   // Callback on pressing Up/Down arrows (for history handling)
// Paths history
// Faster search function (see https://en.wikipedia.org/wiki/String-searching_algorithm)
// Wildcard and regex matching
// select search input text on focus (using callbacks)
	// ImGuiInputTextFlags_CallbackAlways = 1 << 8,   // Callback on each iteration. User code may query cursor position, modify text buffer.
// sort output
// auto complete paths
	// ImGuiInputTextFlags_CallbackCompletion = 1 << 6,   // Callback on pressing TAB (for completion handling)
// open folder : GetOpenFileNameA(OPENFILENAMEA*)
// let user create different search config presets (folders + extensions) : to easily switch between several presets


#define DEBUG_IMGUI 0

#include "resources/liberation-mono.cpp"
#include "resources/icon.cpp"
#include "xfind.h"
#include "xfind_watch_directory.cpp"


static void initState(State& state, Config iconfig)
{
	TIMED_FUNCTION();
	state.config = iconfig;
	state.config.path = pushNewString(state.arena, 1024);
	copy(&state.config.path, iconfig.path);
	terminate_with_null(&state.config.path);

	state.config.ext = pushNewString(state.arena, 1024);
	copy(&state.config.ext, iconfig.ext);
	if (!state.config.ext.size)
		copy(&state.config.ext, make_lit_string("c;cpp;h;hpp;txt;"));
	terminate_with_null(&state.config.ext);

	state.config.tool = pushNewString(state.arena, 1024);
	copy(&state.config.tool, iconfig.tool);
	if (!state.config.tool.size)
		copy(&state.config.tool, programStrings[0].command);
	terminate_with_null(&state.config.tool);
	state.searchBuffer = pushNewString(state.arena, 1024);


	state.setFocusToFolderInput = !state.config.path.size;
	state.setFocusToSearchInput = !state.setFocusToFolderInput;

	state.searchPaths = pushArray(state.arena, String, state.searchPathsSizeMax);
	state.searchPathExists = false;
	state.searchPathsSize = parsePaths(state.arena, state.searchPaths, state.searchPathsSizeMax, state.config.path.str, state.searchPathExists);
	updateWatchedDirectories(state);
	

	state.extensions = pushArray(state.arena, String, state.extensionsMaxSize);
	state.extensionsSize = parseExtensions(state.extensions, state.extensionsMaxSize, state.config.ext.str);

	if (state.searchPathExists)
	{
		computeFileIndex(&state);
	}

	state.results = pushArray(state.arena, Match, state.resultsMaxSize + 1);

	state.config.fontSize = iconfig.fontSize;
	if (state.config.fontSize < 6)
		state.config.fontSize = 20;
	state.config.fontFile = iconfig.fontFile;
	if (state.config.fontFile.size <= 0)
		state.config.fontFile = {};

	state.index = createFileIndex();
}

static void drawMenuBar(ImGuiIO& io, State& state)
{
	TIMED_FUNCTION();
	state.showAbout = false;

	// Draw menu bar
	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Exit"))
			{
				state.running = false;
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Settings"))
		{
#if !DX12
			if (ImGui::DragFloat("Font", &state.config.fontSize, 0.1f, 4, 100))
			{
				if (state.config.fontSize < 4)
					state.config.fontSize = 4;
				if (state.config.fontSize > 100)
					state.config.fontSize = 100;
			}
#endif
			ImGui::Checkbox("Edit program's command", &state.config.showProgram);
			ImGui::Checkbox("Edit folders and extensions", &state.config.showFolderAndExt);
			ImGui::Checkbox("Show relative paths", &state.config.showRelativePaths);
			ImGui::Checkbox("Show hidden files/dirs", &state.config.showHiddenFiles);
			if (ImGui::Checkbox("Search file names", &state.config.searchFileNames))
				state.needToSearchAgain = true;
			if (ImGui::Checkbox("Case sensitive", &state.config.caseSensitive))
				state.needToSearchAgain = true;

			ImGui::DragInt("Ctx lines", &state.config.contextLines, 0.05f, 0, 10);
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::Text("The number of lines to show before and after the selected/hovered line.");
				ImGui::Text("Press F4 to hide/show.");
				ImGui::EndTooltip();
			}

			bool showContextLines = !state.config.hideContextLines;
			if (ImGui::Checkbox("Show context lines", &showContextLines))
				state.config.hideContextLines = !showContextLines;
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::Text("Show lines above and below the selected/hovered line.");
				ImGui::Text("Press F4 to toggle.");
				ImGui::EndTooltip();
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Tools"))
		{
			if (ImGui::MenuItem("Recompute the index"))
				state.needToGenerateIndex = true;
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Help"))
		{
			if (ImGui::BeginMenu("Program templates"))
			{
				for (i32 pi = 0; pi < ArrayCount(programStrings); ++pi)
				{
					ProgramString p = programStrings[pi];
					if (ImGui::MenuItem(p.name.str))
					{
						copy(&state.config.tool, p.command);
						terminate_with_null(&state.config.tool);
					}
					if (p.untested && ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text("This command has not been tested.");
						ImGui::Text("If you find a working command, please let me know.");
						ImGui::EndTooltip();
					}
				}

				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("About"))
			{
				state.showAbout = true;
			}
			ImGui::EndMenu();
		}

	#if APP_INTERNAL
		ImGui::PushStyleColor(ImGuiCol_Text, (ImU32)ImColor(1.f, 0.f, 0.f));
		if (ImGui::BeginMenu("Debug"))
		{
			if (ImGui::Checkbox("Search State Machine", &state.useSSM))
				state.needToSearchAgain = true;
			ImGui::EndMenu();
		}
		ImGui::PopStyleColor();
	#endif

		ImGui::EndMenuBar();
	}
}

static b32 handleInputs(ImGuiIO& io, State& state)
{
	TIMED_FUNCTION();
	b32 inputModified = false;
	b32 searchModified = false;

	if (ImGui::IsKeyPressed(GLFW_KEY_F4, false))
		state.config.hideContextLines = !state.config.hideContextLines;

	// Keep focus on search input field
	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter), false) || ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape), false))
		state.setFocusToSearchInput = true;


	// Handle keyboard on selected result.
	if (0 <= state.selectedLine && state.selectedLine < state.resultsSize)
	{
		Match match = state.results[state.selectedLine];
		String file = match.file->path;

		// Copy file name on CTRL+C
		if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_C), false) && io.KeyCtrl)
		{
			copyToClipboard(file);
		}

		// Open file in editor when Enter is pressed
		if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter)) && 0 <= state.selectedLine && state.selectedLine < state.resultsSize)
		{
			execOpenFile(state.config.tool, file, match.lineIndex, match.offset_in_line + 1);
		}
	}

	// Draw inputs
	{
		//ImGui::ColorEdit3("highlight", &highlightColor.Value.x);
		//ImGui::ColorEdit3("matching", &matchingColor.Value.x);
		//ImGui::ColorEdit3("filename", &filenameColor.Value.x);
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

		if (state.config.showProgram)
		{
			modifProgram = showInput("Program:", "##Program", state.config.tool, w, w0);
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::Text("%s = path of the file to open", ARG_PATH);
				ImGui::Text("%s = line in the file to open", ARG_LINE);
				ImGui::Text("%s = column in the file to open", ARG_COL);
				ImGui::Text("Make sure the path is contained withing quotes");
				ImGui::Text("For Sublime Text, the arguments are : \"%s:%s:%s\"", ARG_PATH, ARG_LINE, ARG_COL);
				ImGui::Text("You can hide this input in the settings");
				ImGui::Text("You can set some templates from the Help menu.");
				ImGui::EndTooltip();
			}
		}

		if (state.config.showFolderAndExt)
		{
			char filesSizeBuffer[32];
			snprintf(filesSizeBuffer, sizeof(filesSizeBuffer), "%d files", state.index.filesSize);
			float filesSizeWidth = ImGui::CalcTextSize(filesSizeBuffer).x;

			showInput(textFolders, w, w1, indexingInProgress ? redLabel : defaultTextColor);
			modifFolders = showInput("##Folders", state.config.path, !indexingInProgress ? -filesSizeWidth - ImGui::GetTextLineHeight() : -1.0f, &state.setFocusToFolderInput, state.searchPathExists ? defaultTextColor : redLabel);
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::Text("Search in several folders by separating them with semicolons ';'");
				ImGui::EndTooltip();
			}
			if (!indexingInProgress)
			{
				ImGui::SameLine();
				ImGui::Text(filesSizeBuffer);
			}

			modifExtensions = showInput("Extensions:", "##Extensions", state.config.ext, w, w2);
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::Text("Choose several extensions by separating them with semicolons ';'");
				ImGui::Text("Extensions should not contain the '.' or any wildcard.");
				ImGui::EndTooltip();
			}
		}

		showInput(textsearch, w, w3, searchInProgress ? redLabel : defaultTextColor);
		modifSearch = showInput("##Search", state.searchBuffer, -1, &state.setFocusToSearchInput);
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::Text("No regex or wildcards for now. Sorry... Maybe later.");
			ImGui::EndTooltip();
		}

		inputModified = modifProgram || modifExtensions || modifFolders;
		searchModified = modifSearch;
		if (searchModified)
		{
			state.selectedLine = 0;
		}

		if (modifFolders || modifExtensions || state.needToGenerateIndex)
		{
			if (indexingInProgress)
			{
				stopFileIndex(&state);
			}
		}
		if (modifFolders)
		{
			state.searchPathsSize = parsePaths(state.arena, state.searchPaths, state.searchPathsSizeMax, state.config.path.str, state.searchPathExists);
			if (state.searchPathExists)
				updateWatchedDirectories(state);
		}

		if (modifExtensions)
		{
			state.extensionsSize = parseExtensions(state.extensions, state.extensionsMaxSize, state.config.ext.str);
		}

		if (modifFolders || modifExtensions || state.needToGenerateIndex)
		{
			if (state.searchPathExists)
			{
				state.resultsSize = 0;
				computeFileIndex(&state);
				state.needToSearchAgain = true;
				state.needToGenerateIndex = false;
			}
		}
	}

#if APP_INTERNAL
	ImGui::Text("Debug : Index time %llums (tt:%llu) | Search time %llums | Index Memory %u MB", indexTime, treeTraversalTime, searchTime, g_memory_usage / MegaBytes(1));
#endif

	// Lauch search if input needed
	if (!state.needToGenerateIndex && !indexingInProgress && (state.needToSearchAgain || searchModified) && state.searchPathExists)
	{
		searchForPatternInFiles(&state.mainSearch, &state, &state.pool.queue, state.results, &state.resultsSize, state.resultsMaxSize, &state.index, state.searchBuffer);
		state.needToSearchAgain = false;
	}

	return inputModified;
}

static void drawResults(State& state)
{
	TIMED_FUNCTION();
	ImGui::BeginChild("Results");
	showResults(state, state.results, state.resultsSize, state.resultsMaxSize, &state.index, state.selectedLine);
	ImGui::EndChild();
}

static void showAbout(b32& showAbout)
{
	if (showAbout)
		ImGui::OpenPopup("About");
	if (ImGui::BeginPopupModal("About", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("xfind " XFIND_VERSION_STRING " by paul2t");
		ImGui::Text("https://github.com/paul2t/xfind");
		ImGui::BulletText("Using imgui and glfw");
		ImGui::BulletText("Font: " DEFAULT_FONT_NAME);
		if (ImGui::Button("OK"))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		showAbout = false;
	}
}


static void notifyIndexUpdate(State& state, WatchDirEvent* evt)
{
	state.needToGenerateIndex = true;
}

inline void freeEventListItem(EventListItem* item)
{
	TIMED_FUNCTION();
	watchdir_free_event(item->evt);
	delete item;
}

static void watchDirectory(State& state)
{
	TIMED_FUNCTION();
	while (EventListItem* item = AtomicListPopFirst(state.file_events_list))
	{
		notifyIndexUpdate(state, &item->evt);
		freeEventListItem(item);
	}
}

static void handleFrame(WINDOW window, ImGuiContext& g, State& state)
{
	TIMED_FUNCTION();
	ImGuiIO& io = g.IO;

#if DEBUG_IMGUI
	ImGui::Begin("MainWindow", 0, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoSavedSettings);
#else
	int framewidth, frameheight;
	GetFramebufferSize(window, &framewidth, &frameheight);
	ImGui::SetNextWindowSize(ImVec2((float)framewidth, (float)frameheight));
	ImGui::SetNextWindowPos(ImVec2());
	ImGui::Begin("MainWindow", 0, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoSavedSettings);
#endif

	drawMenuBar(io, state);

	watchDirectory(state);

	b32 inputModified = handleInputs(io, state);

	drawResults(state);

	showAbout(state.showAbout);

	ImGui::End();


#if DEBUG_IMGUI
	ImGui::ShowMetricsWindow();
#endif

	// Save params if something has been changed or we are closing the application.
	if (inputModified || !state.running)
	{
		// Get window position and dimensions.
		WINDOWPLACEMENT wplacement = {};
		GetWindowPlacement(GetWindowRawPointer(window), &wplacement);
		state.config.maximized = (wplacement.showCmd == SW_SHOWMAXIMIZED);
		// If the windows is maximized, then we keep the dimensions of the restored window.
		if (!state.config.maximized)
		{
			state.config.width = (int)io.DisplaySize.x;
			state.config.height = (int)io.DisplaySize.y;
		}

		writeConfig(state.config);
	}

}


static DWORD directory_listener(void* _data)
{
	State* state = (State*)_data;
	assert(state->wdSemaphore != INVALID_HANDLE_VALUE);
	if (state->wdSemaphore == INVALID_HANDLE_VALUE) return 1;
	for (;;)
	{
		g_directory_listener_started = false;
		WaitForSingleObject(state->wdSemaphore, INFINITE);
		g_directory_listener_started = true;
		g_directory_listener_should_stop = false;

		int32_t wait_timeout = INFINITE;
		while (WatchDirEvent* evt = watchdir_get_event(state->wd, wait_timeout))
		{
			if (g_directory_listener_should_stop)
				wait_timeout = 0;

			EventListItem* item = new EventListItem;
			item->evt = *evt;

			AtomicListInsert(state->file_events_list, item, item->next);

			post_empty_event(&state->waiting_for_event);
		}
	}
}


static volatile bool g_set_active_window;
internal DWORD hot_key_listener(void* _data)
{
	if (RegisterHotKey(NULL, 1, MOD_ALT | MOD_NOREPEAT, 'X'))
	{
		printf("Hot key registered : ALT + X\n");
		MSG msg = {};
		while (GetMessage(&msg, NULL, 0, 0))
		{
			if (msg.message == WM_HOTKEY)
			{
				printf("WM_HOTKEY received\n");
				g_set_active_window = true;
				_WriteBarrier();
				glfwPostEmptyEvent();
			}
		}
	}
	else
	{
		printf("FAILED to register hot key\n");
	}

	return 0;
}

#if APP_INTERNAL && 1
int main()
#else
int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#endif
{
#if APP_INTERNAL
	HINSTANCE hInstance = GetModuleHandle(NULL);
#endif

	// Start Hot key listener
	{
		HANDLE threadHandle = CreateThread(0, 0, hot_key_listener, 0, 0, 0);
		CloseHandle(threadHandle);
	}



#if APP_INTERNAL && 0
	testSearch();
	return 1;
#endif

	State state = {};
	ScopeReleaseVar(state.arena);
	Config iconfig = readConfig(state.arena);

	// Start directory listener
	{
		state.wdSemaphore = CreateSemaphoreEx(0, 0, 1, 0, 0, SEMAPHORE_ALL_ACCESS);
		HANDLE threadHandle = CreateThread(0, 0, directory_listener, &state, 0, 0);
		CloseHandle(threadHandle);
	}

	WINDOW window = createAndInitWindow(XFIND_APP_TITLE, iconfig.width, iconfig.height, iconfig.maximized);
	if (!window) return 1;

#if OPENGL
	GLFWimage icon[2] = { { 16, 16, xfind_16_map }, { 32, 32, xfind_32_map}, };
	glfwSetWindowIcon(window, sizeof(icon)/sizeof(GLFWimage), icon);
	glfwSwapInterval(1);
#endif
	ImGuiContext& g = *ImGui::GetCurrentContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = NULL;

	initThreadPool(state.arena, state.pool);

	initState(state, iconfig);

	MUTE_PROFILE();

	// Main loop
	while (state.running)
	{
		//DEBUG_EVENT(ProfileType_FrameMarker, "FrameMarker");

		u64 ticks_start = getTickCount();
		TIMED_BLOCK_BEGIN("MainLoop");

		if (state.config.fontFile.size)
			reloadFontIfNeeded(state.config.fontFile, state.config.fontSize);
		else
			reloadFontIfNeeded(liberation_mono_ttf, sizeof(liberation_mono_ttf), state.config.fontSize);


		b32 isActiveWindow = IsActiveWindow(window);
		readInputs(window, state.running, state.shouldWaitForEvent, isActiveWindow, &state.waiting_for_event);
		if (!indexingInProgress && !searchInProgress)
			state.shouldWaitForEvent = true;
		
		if (g_set_active_window)
		{
			TIMED_BLOCK("g_set_active_window");
			int iconified = glfwGetWindowAttrib(window, GLFW_ICONIFIED);
			if (iconified)
				glfwRestoreWindow(window);
			glfwFocusWindow(window);
			g_set_active_window = false;
			state.shouldWaitForEvent = false;
			state.setFocusToSearchInput = true;
			state.selectSearchInputText = true;
		}
        
		imguiBeginFrame();


		handleFrame(window, g, state);


		imguiEndFrame(window);

		TIMED_BLOCK_END("MainLoop");
		u64 ticks_end = getTickCount();
		if (isActiveWindow && ((ticks_end - ticks_start) / 1000 > 33))
		{
			printProfile(&mainProfileState, true);
			int breakhere = 1;
		}
		resetProfileState();
    }

	stopWatchedDirectories(state);
	cleanWorkQueue(&state.pool.queue);
	clearFileIndex(&state.index);

	imguiCleanup(window);
    return 0;
}

