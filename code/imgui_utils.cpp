
#if 0
internal HICON createIcon(void* iconData, i32 iconSize)
{
	//PBYTE data = (PBYTE)iconData;
	HMODULE hMod = GetModuleHandleA(0);
	HRSRC hResource = FindResourceA(hMod, "MAINICON", RT_GROUP_ICON);
	HGLOBAL hMem = LoadResource(hMod, hResource);
	PBYTE lpResource = (PBYTE)LockResource(hMem);
	DWORD lpResourceSize = SizeofResource(hMod, hResource);
	HICON hIcon = 0;// CreateIconFromResourceEx(lpResource, lpResourceSize, TRUE, 0x00030000, iconSize, iconSize, LR_DEFAULTCOLOR);
	int offset = LookupIconIdFromDirectoryEx(lpResource, TRUE, iconSize, iconSize, LR_DEFAULTCOLOR);
	if (offset != 0)
		hIcon = CreateIconFromResourceEx(lpResource + offset, 0, TRUE, 0x30000, iconSize, iconSize, LR_DEFAULTCOLOR);
	u32 err = GetLastError();
	// Ahhh, this is the magic API.     
	//int offset = LookupIconIdFromDirectoryEx(data, TRUE, iconSize, iconSize, LR_DEFAULTCOLOR);
	//if (offset != 0)
		//hIcon = CreateIconFromResourceEx(data + offset, 0, TRUE, 0x30000, iconSize, iconSize, LR_DEFAULTCOLOR);
	return hIcon;
}

//static HICON hWindowIcon = NULL;
//static HICON hWindowIconBig = NULL;
internal void SetIcon(HWND hwnd, void* data, i32 imageCount)
{
	//if (hWindowIcon != NULL)
		//DestroyIcon(hWindowIcon);
	//if (hWindowIconBig != NULL)
		//DestroyIcon(hWindowIconBig);
	if (!data)
	{
		SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)NULL);
		SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)NULL);
	}
	else
	{
		HICON hWindowIcon    = createIcon((u8*)data, 16);
		HICON hWindowIconBig = createIcon((u8*)data, 32);
		//HICON hWindowIcon = (HICON)LoadImage(NULL, stricon.str, IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
		//HICON hWindowIconBig = (HICON)LoadImage(NULL, stricon.str, IMAGE_ICON, 32, 32, LR_LOADFROMFILE);
		SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hWindowIcon);
		SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hWindowIconBig);
	}
}
#endif

internal GLFWwindow* createAndInitWindow(int width, int height, b32 maximized)
{
	if (!glfwInit()) return 0;
	GLFWwindow* window = glfwCreateWindow(width, height, "xfind", NULL, NULL);
	if (window == NULL) return 0;

	glfwMakeContextCurrent(window);
	if (maximized)
		glfwMaximizeWindow(window);
	glfwSwapInterval(1); // Enable vsync

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	// Setup ImGui binding
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL2_Init();

	ImGui::StyleColorsDark();

	return window;
}

void reloadFontIfNeeded(void* data, int dataSize, float& fontSize)
{
	if (fontSize != ImGui::GetFontSize())
	{
		ImGui_ImplOpenGL2_DestroyFontsTexture();
		ImGuiIO& io = ImGui::GetIO();
		io.Fonts->Clear();
		if (data && dataSize > 0)
		{
			ImFontConfig fconfig;
			fconfig.FontDataOwnedByAtlas = false;
			io.Fonts->AddFontFromMemoryTTF(data, dataSize, fontSize, &fconfig);
		}
	}
}

void reloadFontIfNeeded(String fontFile, float& fontSize)
{
	if (fontSize != ImGui::GetFontSize())
	{
		ImGui_ImplOpenGL2_DestroyFontsTexture();
		ImGuiIO& io = ImGui::GetIO();
		io.Fonts->Clear();
		if (fontFile.size && PathFileExists(fontFile.str))
			io.Fonts->AddFontFromFileTTF(fontFile.str, fontSize);
	}
}

void readInputs(GLFWwindow* window, b32& running, b32& shouldWaitForEvent, b32 isActiveWindow = true)
{
	if (shouldWaitForEvent && !isActiveWindow)
		glfwWaitEvents();
	else
		glfwPollEvents();
	running = glfwWindowShouldClose(window) == 0;
	shouldWaitForEvent = false;
}

void imguiBeginFrame()
{
	ImGui_ImplOpenGL2_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
}

void imguiEndFrame(GLFWwindow* window)
{
	ImGui::Render();
	int display_w, display_h;
	glfwGetFramebufferSize(window, &display_w, &display_h);
	glViewport(0, 0, display_w, display_h);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

	glfwMakeContextCurrent(window);
	glfwSwapBuffers(window);
}

void imguiCleanup(GLFWwindow* window)
{
	ImGui_ImplOpenGL2_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	glfwDestroyWindow(window);
	glfwTerminate();
}
