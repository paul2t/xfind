
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
