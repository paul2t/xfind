// TODO:
// Look for modified files to update the index. (partial)
// Mouse click on the result list.
// Search history
// Paths history
// Extensions history
// Faster search function (see https://en.wikipedia.org/wiki/String-searching_algorithm)
// hot key to give focus to xfind : RegisterHotKey ? (https://docs.microsoft.com/en-us/windows/desktop/api/winuser/nf-winuser-registerhotkey)
// Show a few lines above and below the selected result.
// Wildcard and regex matching


#include "xfind.h"

internal void initState(State& state, Config iconfig)
{
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
		state.config.fontFile = make_lit_string("liberation-mono.ttf");

}

void handleFrame(GLFWwindow* window, ImGuiContext& g, State& state)
{
	ImGuiIO& io = g.IO;

	int framewidth, frameheight;
	glfwGetFramebufferSize(window, &framewidth, &frameheight);
	ImGui::SetNextWindowSize(ImVec2((float)framewidth, (float)frameheight));
	ImGui::SetNextWindowPos(ImVec2());
	ImGui::Begin("MainWindow", 0, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoSavedSettings);

	bool showAbout = false;

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
			if (ImGui::DragFloat("Font", &state.config.fontSize, 0.1f, 4, 100))
			{
				if (state.config.fontSize < 4)
					state.config.fontSize = 4;
				if (state.config.fontSize > 100)
					state.config.fontSize = 100;
			}
			ImGui::Checkbox("Edit program's command", &state.config.showProgram);
			ImGui::Checkbox("Edit folders and extensions", &state.config.showFolderAndExt);
			ImGui::Checkbox("Show relative paths", &state.config.showRelativePaths);
			ImGui::Checkbox("Show hidden files/dirs", &state.config.showHiddenFiles);
			if (ImGui::Checkbox("Search file names", &state.config.searchFileNames))
				state.needToSearchAgain = true;
			if (ImGui::Checkbox("Case sensitive", &state.config.caseSensitive))
				state.needToSearchAgain = true;
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
				showAbout = true;
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


	b32 inputModified = false;
	b32 searchModified = false;

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
				ImGui::Text("You can hide this input in the options");
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

		inputModified = modifProgram || modifExtensions || modifFolders || modifSearch;
		searchModified = modifSearch;
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
	ImGui::Text("Debug : Index time %llums (tt:%llu) | Search time %llums", indexTime, treeTraversalTime, searchTime);
#endif

	// Lauch search if input needed
	if (!state.needToGenerateIndex && !indexingInProgress && (state.needToSearchAgain || searchModified) && state.searchPathExists)
	{
		searchForPatternInFiles(&state.mainSearch, &state, &state.pool.queue, state.results, &state.resultsSize, state.resultsMaxSize, &state.index, state.searchBuffer);
		state.needToSearchAgain = false;
		state.selectedLine = 0;
	}

	// Draw results
	{
		ImGui::BeginChild("Results");
		showResults(state, state.results, state.resultsSize, state.resultsMaxSize, &state.index, state.selectedLine);
		ImGui::EndChild();
	}

	ImGui::End();

	if (showAbout)
		ImGui::OpenPopup("About");
	if (ImGui::BeginPopupModal("About", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("xfind alpha by paul2t");
		ImGui::BulletText("Using imgui and glfw");
		ImGui::BulletText("Font: liberation-mono.ttf");
		if (ImGui::Button("OK"))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}



	// Save params if something has been changed or we are closing the application.
	if (inputModified || !state.running)
	{
		// Get window position and dimensions.
		WINDOWPLACEMENT wplacement = {};
		GetWindowPlacement(glfwGetWin32Window(window), &wplacement);
		state.config.maximized = (wplacement.showCmd == SW_SHOWMAXIMIZED);
		// If the windows is maximized, then we keep the dimensions of the restored window.
		if (!state.config.maximized)
		{
			glfwGetWindowSize(window, &state.config.width, &state.config.height);
		}

		writeConfig(state.config);
	}

}



#if APP_INTERNAL && 1
int main()
#else
int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#endif
{
	State state = {};
	state.index.onePastLastFile = &state.index.firstFile; // This is because of an internal error in vs compiler.
	Config iconfig = readConfig(state.arena);

	GLFWwindow* window = createAndInitWindow(iconfig.width, iconfig.height, iconfig.maximized);
	if (!window) return 1;
	ImGuiContext& g = *ImGui::GetCurrentContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = NULL;

	initThreadPool(state.arena, state.pool);

	initState(state, iconfig);

    while (state.running)
    {
		reloadFontIfNeeded(state.config.fontFile, state.config.fontSize);

		b32 isActiveWindow = (GetActiveWindow() == glfwGetWin32Window(window));
		readInputs(window, state.running, state.shouldWaitForEvent, isActiveWindow);
		if (!indexingInProgress && !searchInProgress)
			state.shouldWaitForEvent = true;
        
		imguiBeginFrame();


		handleFrame(window, g, state);


		imguiEndFrame(window);
    }

	imguiCleanup(window);
    return 0;
}
