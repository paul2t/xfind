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



struct ProgramString
{
	String name;
	String command;
	b32 untested;
};

ProgramString programStrings[] =
{
	{ make_lit_string("Sublime Text 3"), make_lit_string("\"C:\\Program Files\\Sublime Text 3\\sublime_text.exe\" \"%p:%l:%c\""), },
	{ make_lit_string("Notepad++"), make_lit_string("\"C:\\Program Files (x86)\\Notepad++\\notepad++.exe\" \"%p\" -n%l -c%c"), },
	{ make_lit_string("VS Code"), make_lit_string("\"C:\\Users\\xf4\\AppData\\Local\\Programs\\Microsoft VS Code\\Code.exe\" -g \"%p:%l:%c\""), },
	{ make_lit_string("Emacs"), make_lit_string("emacsclient +%l:%c \"%p\""), true, },
	{ make_lit_string("GVim"), make_lit_string("gvim '+normal %lG%c|' \"%p\""), true, },
};




#define DEFAULT_WIDTH 1280
#define DEFAULT_HEIGHT 720
#define DEFAULT_MAXIMIZED 0

#define SHOW_RELATIVE_PATH 1
ImColor matchingColor = ImVec4(0.145f, 0.822f, 0.07f, 1.0f);
ImColor filenameColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
ImColor highlightColor = ImVec4(0.43f, 0, 0, 255);

b32 setFocusToSearchInput;

#include "xfind_config.cpp"


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


#include "xfind_ui.cpp"
#include "xfind_worker_index.cpp"
#include "xfind_worker_search.cpp"





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
		config = iconfig;
		config.path = pushNewString(arena, 4 * 4096);
		copy(&config.path, iconfig.path);
		terminate_with_null(&config.path);

		config.ext = pushNewString(arena, 4096);
		copy(&config.ext, iconfig.ext);
		if (!config.ext.size)
			copy(&config.ext, make_lit_string("c;cpp;h;hpp;txt;"));
		terminate_with_null(&config.ext);

		config.tool = pushNewString(arena, 4096);
		copy(&config.tool, iconfig.tool);
		if (!config.tool.size)
			copy(&config.tool, programStrings[0].command);
		terminate_with_null(&config.tool);
	}
	String searchBuffer = pushNewString(arena, 4096);


	// Init global state
	b32 running = true;

	b32 setFocusToFolderInput = !config.path.size;
	setFocusToSearchInput = !setFocusToFolderInput;
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
		computeFileIndex(iwData, &workQueue, searchPaths, searchPathsSize, extensions, extensionsSize, files, &filesSize, filesMaxSize);
	}

	i32 resultsMaxSize = 1000;
	Match* results = pushArray(arena, Match, resultsMaxSize+1);
	volatile i32 resultsSize = 0;
	b32 needToSearchAgain = false;
	b32 needToGenerateIndex = false;

	MainSearchPatternData mainSearch = {};

	float oldFontSize = 0;
	config.fontSize = iconfig.fontSize;
	if (config.fontSize < 6)
		config.fontSize = 20;
	config.fontFile = iconfig.fontFile;
	if (config.fontFile.size <= 0)
		config.fontFile = make_lit_string("liberation-mono.ttf");
	b32 showProgram = true;

    // Main loop
    while (running)
    {

		// Reload font if size changed
		if (config.fontSize < 6)
			config.fontSize = 6;
		if (config.fontSize != oldFontSize)
		{
			ImGui_ImplOpenGL2_DestroyFontsTexture();
			io.Fonts->Clear();
			if (PathFileExists(config.fontFile.str))
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
				ImGui::DragFloat("Font", &config.fontSize, 0.2f, 6, 38);
				ImGui::Checkbox("Edit program's command", &config.showProgram);
				ImGui::Checkbox("Edit folders and extensions", &config.showFolderAndExt);
				ImGui::Checkbox("Show relative paths", &config.showRelativePaths);
				ImGui::Checkbox("Show hidden files/dirs", &config.showHiddenFiles);
				if (ImGui::Checkbox("Search file names", &config.searchFileNames))
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
							copy(&config.tool, p.command);
							terminate_with_null(&config.tool);
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
			setFocusToSearchInput = true;


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
				execOpenFile(config.tool, file, match.lineIndex, match.offset_in_line+1);
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
					ImGui::Text("You can set some templates from the Help menu.");
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
					ImGui::Text("Search in several folders by separating them with semicolons ';'");
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
					ImGui::Text("Choose several extensions by separating them with semicolons ';'");
					ImGui::Text("Extensions should not contain the '.' or any wildcard.");
					ImGui::EndTooltip();
				}
			}

			showInput(textsearch, w, w3, searchInProgress ? redLabel : defaultTextColor);
			modifSearch = showInput("##Search", searchBuffer, -1, &setFocusToSearchInput);
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
				searchPathsSize = parsePaths(arena, searchPaths, searchPathsSizeMax, config.path.str, searchPathExists);
			}

			if (modifExtensions)
			{
				extensionsSize = parseExtensions(extensions, extensionsMaxSize, config.ext.str);
			}

			if (modifFolders || modifExtensions || needToGenerateIndex)
			{
				if (searchPathExists)
				{
					resultsSize = 0;
					computeFileIndex(iwData, &workQueue, searchPaths, searchPathsSize, extensions, extensionsSize, files, &filesSize, filesMaxSize);
					needToSearchAgain = true;
					needToGenerateIndex = false;
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
			config.maximized = (wplacement.showCmd == SW_SHOWMAXIMIZED);
			// If the windows is maximized, then we keep the dimensions of the restored window.
			if (!config.maximized)
			{
				glfwGetWindowSize(window, &config.width, &config.height);
			}

			writeConfig(config);
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
