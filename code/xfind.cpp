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


#if APP_INTERNAL && 1
int main()
#else
int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#endif
{
	MemoryArena arena = {};

	// Read saved config.
	Config iconfig = readConfig(arena);

	// Init window and ImGui
	GLFWwindow* window = 0;
	{
		if (!glfwInit()) return 1;
		window = glfwCreateWindow(iconfig.width, iconfig.height, "xfind", NULL, NULL);
		if (window == NULL) return 1;

		glfwMakeContextCurrent(window);
		if (iconfig.maximized)
			glfwMaximizeWindow(window);
		glfwSwapInterval(1); // Enable vsync

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		// Setup ImGui binding
		ImGui_ImplGlfw_InitForOpenGL(window, true);
		ImGui_ImplOpenGL2_Init();

		ImGui::StyleColorsDark();
	}

	ImGuiIO& io = ImGui::GetIO();
	ImVec4 clear_color = ImColor(114, 144, 154);

	// Create worker threads
	WorkQueue workQueue = {};
	{
		i32 nbLogicalCores = getNumberOfLogicalThreads();
		ThreadData* threadsData = pushArray(arena, ThreadData, nbLogicalCores);
		workQueue.semaphore = CreateSemaphoreEx(0, 0, nbLogicalCores, 0, 0, SEMAPHORE_ALL_ACCESS);
		for (i32 i = 0; i < nbLogicalCores; ++i)
		{
			createWorkerThread(threadsData + i, &workQueue);
		}
	}


	// Allocate memory for input
	{
		state.config = iconfig;
		state.config.path = pushNewString(arena, 4 * 4096);
		copy(&state.config.path, iconfig.path);
		terminate_with_null(&state.config.path);

		state.config.ext = pushNewString(arena, 4096);
		copy(&state.config.ext, iconfig.ext);
		if (!state.config.ext.size)
			copy(&state.config.ext, make_lit_string("c;cpp;h;hpp;txt;"));
		terminate_with_null(&state.config.ext);

		state.config.tool = pushNewString(arena, 4096);
		copy(&state.config.tool, iconfig.tool);
		if (!state.config.tool.size)
			copy(&state.config.tool, programStrings[0].command);
		terminate_with_null(&state.config.tool);
	}
	String searchBuffer = pushNewString(arena, 1024);


	// Init global state
	b32 running = true;

	b32 setFocusToFolderInput = !state.config.path.size;
	state.setFocusToSearchInput = !setFocusToFolderInput;
	i32 selectedLine = 0;

	i32 searchPathsSizeMax = 1024;
	String* searchPaths = pushArray(arena, String, searchPathsSizeMax);
	b32 searchPathExists = false;
	i32 searchPathsSize = parsePaths(arena, searchPaths, searchPathsSizeMax, state.config.path.str, searchPathExists);

	i32 extensionsMaxSize = 1024;
	String* extensions = pushArray(arena, String, extensionsMaxSize);
	i32 extensionsSize = parseExtensions(extensions, extensionsMaxSize, state.config.ext.str);

	i32 filesMaxSize = 1024*1024;
	FileIndexEntry* files = pushArray(arena, FileIndexEntry, filesMaxSize);
	volatile i32 filesSize = 0;
	if (searchPathExists)
	{
		computeFileIndex(state.iwData, &workQueue, searchPaths, searchPathsSize, extensions, extensionsSize, files, &filesSize, filesMaxSize);
	}

	i32 resultsMaxSize = 1000;
	Match* results = pushArray(arena, Match, resultsMaxSize+1);
	volatile i32 resultsSize = 0;
	b32 needToSearchAgain = false;
	b32 needToGenerateIndex = false;

	MainSearchPatternData mainSearch = {};

	float oldFontSize = 0;
	state.config.fontSize = iconfig.fontSize;
	if (state.config.fontSize < 6)
		state.config.fontSize = 20;
	state.config.fontFile = iconfig.fontFile;
	if (state.config.fontFile.size <= 0)
		state.config.fontFile = make_lit_string("liberation-mono.ttf");
	b32 showProgram = true;

    // Main loop
    while (running)
    {

		// Reload font if size changed
		if (state.config.fontSize < 6)
			state.config.fontSize = 6;
		if (state.config.fontSize != oldFontSize)
		{
			ImGui_ImplOpenGL2_DestroyFontsTexture();
			io.Fonts->Clear();
			if (PathFileExists(state.config.fontFile.str))
				io.Fonts->AddFontFromFileTTF(state.config.fontFile.str, state.config.fontSize);
			oldFontSize = state.config.fontSize;
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
        

		// Draw Menu bar
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
				ImGui::DragFloat("Font", &state.config.fontSize, 0.2f, 6, 38);
				ImGui::Checkbox("Edit program's command", &state.config.showProgram);
				ImGui::Checkbox("Edit folders and extensions", &state.config.showFolderAndExt);
				ImGui::Checkbox("Show relative paths", &state.config.showRelativePaths);
				ImGui::Checkbox("Show hidden files/dirs", &state.config.showHiddenFiles);
				if (ImGui::Checkbox("Search file names", &state.config.searchFileNames))
					needToSearchAgain = true;
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Tools"))
			{
				if (ImGui::MenuItem("Recompute the index"))
					needToGenerateIndex = true;
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
				ImGui::EndMenu();
			}
            ImGui::EndMainMenuBar();
        }


		b32 inputModified = false;
		b32 searchModified = false;

		// Keep focus on search input field
		if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter), false) || ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape), false))
			state.setFocusToSearchInput = true;


		// Handle keyboard on selected result.
		if (0 <= selectedLine && selectedLine < resultsSize)
		{
			Match match = results[selectedLine];
			String file = files[match.index].path;

			// Copy file name on CTRL+C
			if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_C), false) && io.KeyCtrl)
			{
				copyToClipboard(file);
			}

			// Open file in editor when Enter is pressed
			if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter)) && 0 <= selectedLine && selectedLine < resultsSize)
			{
				execOpenFile(state.config.tool, file, match.lineIndex, match.offset_in_line+1);
			}
		}


		// Offset to show windows below the menu bar.
		float offset = g.Style.DisplaySafeAreaPadding.y + g.FontBaseSize + g.Style.FramePadding.y;
		ImGui::SetNextWindowSize(ImVec2((float)windowWidth, (float)windowHeight - offset));
		ImGui::SetNextWindowPos(ImVec2(0.0f, offset));
		ImGui::Begin("MainWindow", 0, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
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
					ImGui::Text("%%p = path of the file to open");
					ImGui::Text("%%l = line in the file to open");
					ImGui::Text("%%c = column in the file to open");
					ImGui::Text("Make sure the path is contained withing quotes");
					ImGui::Text("For Sublime Text, the arguments are : \"%%p:%%l:%%c\"");
					ImGui::Text("You can hide this input in the options");
					ImGui::Text("You can set some templates from the Help menu.");
					ImGui::EndTooltip();
				}
			}

			if (state.config.showFolderAndExt)
			{
				char filesSizeBuffer[32];
				snprintf(filesSizeBuffer, sizeof(filesSizeBuffer), "%d files", filesSize);
				float filesSizeWidth = ImGui::CalcTextSize(filesSizeBuffer).x;

				showInput(textFolders, w, w1, indexingInProgress ? redLabel : defaultTextColor);
				modifFolders = showInput("##Folders", state.config.path, !indexingInProgress ? -filesSizeWidth - ImGui::GetTextLineHeight() : -1.0f, &setFocusToFolderInput, searchPathExists ? defaultTextColor : redLabel);
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
			modifSearch = showInput("##Search", searchBuffer, -1, &state.setFocusToSearchInput);
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::Text("No regex or wildcards for now. Sorry... Maybe later.");
				ImGui::EndTooltip();
			}

			inputModified = modifProgram || modifExtensions || modifFolders || modifSearch;
			searchModified = modifSearch;
			if (modifFolders)
			{
				searchPathsSize = parsePaths(arena, searchPaths, searchPathsSizeMax, state.config.path.str, searchPathExists);
			}

			if (modifExtensions)
			{
				extensionsSize = parseExtensions(extensions, extensionsMaxSize, state.config.ext.str);
			}

			if (modifFolders || modifExtensions || needToGenerateIndex)
			{
				if (searchPathExists)
				{
					resultsSize = 0;
					computeFileIndex(state.iwData, &workQueue, searchPaths, searchPathsSize, extensions, extensionsSize, files, &filesSize, filesMaxSize);
					needToSearchAgain = true;
					needToGenerateIndex = false;
				}
				else if (indexingInProgress)
				{
					stopFileIndex(&workQueue, &filesSize);
				}
			}
		}

		if (!needToGenerateIndex && !indexingInProgress && (needToSearchAgain || searchModified) && searchPathExists)
		{
			searchForPatternInFiles(&mainSearch, &workQueue, results, &resultsSize, resultsMaxSize, files, filesSize, searchBuffer);
			needToSearchAgain = false;
			selectedLine = 0;
		}

		ImGui::BeginChild("Results", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
		{
			showResults(results, resultsSize, resultsMaxSize, files, selectedLine);
		}
		ImGui::EndChild();

		ImGui::End();





		// Save params if something has been changed or we are closing the application.
		if (inputModified || !running)
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
        
        
        
        
        
        

		// Rendering frame
		ImGui::Render();
		int display_w, display_h;
		glfwGetFramebufferSize(window, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
		glClear(GL_COLOR_BUFFER_BIT);
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
