

#if OPENGL
typedef GLFWwindow* WINDOW;
#elif DX12

struct WINDOW
{
	HWND hwnd = 0;
	WNDCLASSEX wc = {};

	operator bool() const
	{
		return hwnd != 0;
	}
};
#else
#error Which graphic driver should we use ?
#endif

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


inline ImVec2 ScreenToWindowPosition(const ImVec2& pos)
{
	ImVec2 wPos = ImGui::GetWindowPos();
	ImVec2 scroll = ImGui::GetCurrentWindowRead()->Scroll;
	return pos - wPos + scroll;
}

inline ImVec2 WindowToScreenPosition(const ImVec2& pos)
{
	ImVec2 wPos = ImGui::GetWindowPos();
	ImVec2 scroll = ImGui::GetCurrentWindowRead()->Scroll;
	return pos + wPos - scroll;
}

inline ImVec2 GetContensRegionDimensions()
{
	ImGuiWindow* window = ImGui::GetCurrentWindowRead();
	return ImVec2(window->ContentsRegionRect.GetWidth(), window->ContentsRegionRect.GetHeight());
}

static bool IsScreenLineOnScreen(float screenLineY)
{
	float availY = GetContensRegionDimensions().y;
	float windowY = ImGui::GetWindowPos().y;
	bool isLineOnScreen = screenLineY >= 0 && screenLineY - windowY - availY <= 0;
	return isLineOnScreen;
}

inline bool IsLineOnScreen(float lineY)
{
	//float screenLineY = WindowToScreenPosition(ImVec2(0, lineY)).y;
	return IsScreenLineOnScreen(lineY);
}

inline bool IsCurrentLineOnScreen()
{
	return IsScreenLineOnScreen(ImGui::GetCursorScreenPos().y);
}


inline b32 IsActiveWindow(WINDOW window)
{
	b32 isActiveWindow = false;
#if OPENGL
	isActiveWindow = (GetActiveWindow() == glfwGetWin32Window(window));
#elif DX12
	if (HWND active_window = ::GetForegroundWindow())
		if (active_window == window.hwnd || ::IsChild(active_window, window.hwnd))
			isActiveWindow = true;
#endif
	return isActiveWindow;
}


#if DX12

#define DX12_ENABLE_DEBUG_LAYER 0

struct FrameContext
{
	ID3D12CommandAllocator* CommandAllocator;
	UINT64                  FenceValue;
};

// Data
static int const                    NUM_FRAMES_IN_FLIGHT = 3;
static FrameContext                 g_frameContext[NUM_FRAMES_IN_FLIGHT] = {};
//static UINT                         g_frameIndex = 0;

static int const                    NUM_BACK_BUFFERS = 3;
//static ID3D12Device*                g_pd3dDevice = NULL;
static ID3D12DescriptorHeap*        g_pd3dRtvDescHeap = NULL;
static ID3D12DescriptorHeap*        g_pd3dSrvDescHeap = NULL;
static ID3D12CommandQueue*          g_pd3dCommandQueue = NULL;
static ID3D12GraphicsCommandList*   g_pd3dCommandList = NULL;
static ID3D12Fence*                 g_fence = NULL;
static HANDLE                       g_fenceEvent = NULL;
static UINT64                       g_fenceLastSignaledValue = 0;
static IDXGISwapChain3*             g_pSwapChain = NULL;
static HANDLE                       g_hSwapChainWaitableObject = NULL;
static ID3D12Resource*              g_mainRenderTargetResource[NUM_BACK_BUFFERS] = {};
static D3D12_CPU_DESCRIPTOR_HANDLE  g_mainRenderTargetDescriptor[NUM_BACK_BUFFERS] = {};



// Forward declarations of helper functions
static bool CreateDeviceD3D(HWND hWnd);
static void CleanupDeviceD3D();
static void CreateRenderTarget();
static void CleanupRenderTarget();
static void WaitForLastSubmittedFrame();
static FrameContext* WaitForNextFrameResources();
static void ResizeSwapChain(HWND hWnd, int width, int height);
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);



// Helper functions


static bool CreateDeviceD3D(HWND hWnd)
{
	// Setup swap chain
	DXGI_SWAP_CHAIN_DESC1 sd;
	{
		ZeroMemory(&sd, sizeof(sd));
		sd.BufferCount = NUM_BACK_BUFFERS;
		sd.Width = 0;
		sd.Height = 0;
		sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		sd.Scaling = DXGI_SCALING_STRETCH;
		sd.Stereo = FALSE;
	}

#if DX12_ENABLE_DEBUG_LAYER
	{
		ID3D12Debug* dx12Debug = NULL;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dx12Debug))))
		{
			dx12Debug->EnableDebugLayer();
			dx12Debug->Release();
		}
	}
#endif

	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
	if (D3D12CreateDevice(NULL, featureLevel, IID_PPV_ARGS(&g_pd3dDevice)) != S_OK)
		return false;

	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		desc.NumDescriptors = NUM_BACK_BUFFERS;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		desc.NodeMask = 1;
		if (g_pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dRtvDescHeap)) != S_OK)
			return false;

		SIZE_T rtvDescriptorSize = g_pd3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_pd3dRtvDescHeap->GetCPUDescriptorHandleForHeapStart();
		for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
		{
			g_mainRenderTargetDescriptor[i] = rtvHandle;
			rtvHandle.ptr += rtvDescriptorSize;
		}
	}

	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors = 1;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (g_pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dSrvDescHeap)) != S_OK)
			return false;
	}

	{
		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		desc.NodeMask = 1;
		if (g_pd3dDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(&g_pd3dCommandQueue)) != S_OK)
			return false;
	}

	for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
		if (g_pd3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_frameContext[i].CommandAllocator)) != S_OK)
			return false;

	if (g_pd3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frameContext[0].CommandAllocator, NULL, IID_PPV_ARGS(&g_pd3dCommandList)) != S_OK ||
		g_pd3dCommandList->Close() != S_OK)
		return false;

	if (g_pd3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)) != S_OK)
		return false;

	g_fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (g_fenceEvent == NULL)
		return false;

	{
		IDXGIFactory4* dxgiFactory = NULL;
		IDXGISwapChain1* swapChain1 = NULL;
		if (CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)) != S_OK ||
			dxgiFactory->CreateSwapChainForHwnd(g_pd3dCommandQueue, hWnd, &sd, NULL, NULL, &swapChain1) != S_OK ||
			swapChain1->QueryInterface(IID_PPV_ARGS(&g_pSwapChain)) != S_OK)
			return false;
		swapChain1->Release();
		dxgiFactory->Release();
		g_pSwapChain->SetMaximumFrameLatency(NUM_BACK_BUFFERS);
		g_hSwapChainWaitableObject = g_pSwapChain->GetFrameLatencyWaitableObject();
	}

	CreateRenderTarget();
	return true;
}

static void CleanupDeviceD3D()
{
	CleanupRenderTarget();
	if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = NULL; }
	if (g_hSwapChainWaitableObject != NULL) { CloseHandle(g_hSwapChainWaitableObject); }
	for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
		if (g_frameContext[i].CommandAllocator) { g_frameContext[i].CommandAllocator->Release(); g_frameContext[i].CommandAllocator = NULL; }
	if (g_pd3dCommandQueue) { g_pd3dCommandQueue->Release(); g_pd3dCommandQueue = NULL; }
	if (g_pd3dCommandList) { g_pd3dCommandList->Release(); g_pd3dCommandList = NULL; }
	if (g_pd3dRtvDescHeap) { g_pd3dRtvDescHeap->Release(); g_pd3dRtvDescHeap = NULL; }
	if (g_pd3dSrvDescHeap) { g_pd3dSrvDescHeap->Release(); g_pd3dSrvDescHeap = NULL; }
	if (g_fence) { g_fence->Release(); g_fence = NULL; }
	if (g_fenceEvent) { CloseHandle(g_fenceEvent); g_fenceEvent = NULL; }
	if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = NULL; }
}

static void CreateRenderTarget()
{
	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
	{
		ID3D12Resource* pBackBuffer = NULL;
		g_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
		g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, g_mainRenderTargetDescriptor[i]);
		g_mainRenderTargetResource[i] = pBackBuffer;
	}
}

static void CleanupRenderTarget()
{
	WaitForLastSubmittedFrame();

	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
		if (g_mainRenderTargetResource[i]) { g_mainRenderTargetResource[i]->Release(); g_mainRenderTargetResource[i] = NULL; }
}

static void WaitForLastSubmittedFrame()
{
	FrameContext* frameCtxt = &g_frameContext[g_frameIndex % NUM_FRAMES_IN_FLIGHT];

	UINT64 fenceValue = frameCtxt->FenceValue;
	if (fenceValue == 0)
		return; // No fence was signaled

	frameCtxt->FenceValue = 0;
	if (g_fence->GetCompletedValue() >= fenceValue)
		return;

	g_fence->SetEventOnCompletion(fenceValue, g_fenceEvent);
	WaitForSingleObject(g_fenceEvent, INFINITE);
}

static FrameContext* WaitForNextFrameResources()
{
	UINT nextFrameIndex = g_frameIndex + 1;
	g_frameIndex = nextFrameIndex;

	HANDLE waitableObjects[] = { g_hSwapChainWaitableObject, NULL };
	DWORD numWaitableObjects = 1;

	FrameContext* frameCtxt = &g_frameContext[nextFrameIndex % NUM_FRAMES_IN_FLIGHT];
	UINT64 fenceValue = frameCtxt->FenceValue;
	if (fenceValue != 0) // means no fence was signaled
	{
		frameCtxt->FenceValue = 0;
		g_fence->SetEventOnCompletion(fenceValue, g_fenceEvent);
		waitableObjects[1] = g_fenceEvent;
		numWaitableObjects = 2;
	}

	WaitForMultipleObjects(numWaitableObjects, waitableObjects, TRUE, INFINITE);

	return frameCtxt;
}

static void ResizeSwapChain(HWND hWnd, int width, int height)
{
	DXGI_SWAP_CHAIN_DESC1 sd;
	g_pSwapChain->GetDesc1(&sd);
	sd.Width = width;
	sd.Height = height;

	IDXGIFactory4* dxgiFactory = NULL;
	g_pSwapChain->GetParent(IID_PPV_ARGS(&dxgiFactory));

	g_pSwapChain->Release();
	CloseHandle(g_hSwapChainWaitableObject);

	IDXGISwapChain1* swapChain1 = NULL;
	dxgiFactory->CreateSwapChainForHwnd(g_pd3dCommandQueue, hWnd, &sd, NULL, NULL, &swapChain1);
	swapChain1->QueryInterface(IID_PPV_ARGS(&g_pSwapChain));
	swapChain1->Release();
	dxgiFactory->Release();

	g_pSwapChain->SetMaximumFrameLatency(NUM_BACK_BUFFERS);

	g_hSwapChainWaitableObject = g_pSwapChain->GetFrameLatencyWaitableObject();
	assert(g_hSwapChainWaitableObject != NULL);
}

// Win32 message handler
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	switch (msg)
	{
	case WM_SIZE:
		if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED)
		{
			ImGui_ImplDX12_InvalidateDeviceObjects();
			CleanupRenderTarget();
			ResizeSwapChain(hWnd, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam));
			CreateRenderTarget();
			ImGui_ImplDX12_CreateDeviceObjects();
		}
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
			return 0;
		break;
	case WM_DESTROY:
		::PostQuitMessage(0);
		return 0;
	}
	return ::DefWindowProc(hWnd, msg, wParam, lParam);
}

#endif

HWND GetWindowRawPointer(WINDOW window)
{
#if OPENGL
	return glfwGetWin32Window(window);
#elif DX12
	return window.hwnd;
#endif
}

static void GetFramebufferSize(WINDOW window, int* width, int* height)
{
#if OPENGL
	glfwGetFramebufferSize(window, width, height);
#elif DX12
	//MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, width, width);
	DXGI_SWAP_CHAIN_DESC1 sd;
	g_pSwapChain->GetDesc1(&sd);
	*width = sd.Width;
	*height = sd.Height;
#endif
}

static void GetWindowSize(WINDOW window, int* width, int* height)
{
#if OPENGL
	glfwGetWindowSize(window, width, height);
#elif DX12
	RECT rect;
	GetWindowRect(window.hwnd, &rect);
	*width = rect.right - rect.left;
	*height = rect.top - rect.bottom;
#endif
}


static WINDOW createAndInitWindow(const char* name, int width, int height, b32 maximized)
{
#if OPENGL

	if (!glfwInit()) return 0;
	GLFWwindow* window = glfwCreateWindow(width, height, name, NULL, NULL);
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

	GLFWwindow* result = window;

#else

	// Create application window
	WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, _T(name), NULL };
	::RegisterClassEx(&wc);
	HWND window = ::CreateWindow(wc.lpszClassName, _T(name), WS_OVERLAPPEDWINDOW, 0, 0, width, height, NULL, NULL, wc.hInstance, NULL);
	if (window == NULL) return {};

	// Initialize Direct3D
	if (!CreateDeviceD3D(window))
	{
		CleanupDeviceD3D();
		::UnregisterClass(wc.lpszClassName, wc.hInstance);
		return {};
	}

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();

	// Setup Platform/Renderer bindings
	ImGui_ImplWin32_Init(window);
	ImGui_ImplDX12_Init(g_pd3dDevice, NUM_FRAMES_IN_FLIGHT,
		DXGI_FORMAT_R8G8B8A8_UNORM,
		g_pd3dSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
		g_pd3dSrvDescHeap->GetGPUDescriptorHandleForHeapStart());

	ImGui_ImplDX12_NewFrame();

	// Show the window
	::ShowWindow(window, SW_SHOWDEFAULT);
	::UpdateWindow(window);

	WINDOW result = {};
	result.hwnd = window;
	result.wc = wc;

#endif

	return result;
}

static void reloadFontIfNeeded(void* data, int dataSize, float& fontSize)
{
	TIMED_FUNCTION();
	if (fontSize != ImGui::GetFontSize())
	{
#if OPENGL
		ImGui_ImplOpenGL2_DestroyFontsTexture();
#elif DX12
		ImGui_ImplDX12_InvalidateDeviceObjects();
#endif
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

static void reloadFontIfNeeded(String fontFile, float& fontSize)
{
	if (fontSize != ImGui::GetFontSize())
	{
#if OPENGL
		ImGui_ImplOpenGL2_DestroyFontsTexture();
#elif DX12
		ImGui_ImplDX12_InvalidateDeviceObjects();
		//ImGui_ImplDX12_CreateDeviceObjects();
#endif
		ImGuiIO& io = ImGui::GetIO();
		io.Fonts->Clear();
		if (fontFile.size && PathFileExists(fontFile.str))
			io.Fonts->AddFontFromFileTTF(fontFile.str, fontSize);
	}
}

static void post_empty_event(volatile b32* waiting_for_event) {
	if (!waiting_for_event) return;
	b32 original_value = *waiting_for_event;
	if (*waiting_for_event)
	if (original_value) {
		if (original_value == (u32)InterlockedCompareExchange((LONG volatile *)waiting_for_event, original_value, 0)) {
			glfwPostEmptyEvent();
		}
	}
}

// @return true if should wait for event.
static b32 start_waiting_for_event(volatile b32* waiting_for_event) {
	if (!waiting_for_event) return true;
	b32 original_value = *waiting_for_event;
	if (!original_value) {
		if (original_value == (u32)InterlockedCompareExchange((LONG volatile *)waiting_for_event, original_value, 1)) {
			return true;
		}
	}
	return original_value;
}

static void end_waiting_for_event(volatile b32* waiting_for_event) {
	if (!waiting_for_event) return;
	b32 original_value = *waiting_for_event;
	if (original_value) {
		InterlockedCompareExchange((LONG volatile *)waiting_for_event, original_value, 0);
	}
}

static void readInputs(WINDOW window, b32& running, b32& shouldWaitForEvent, b32 isActiveWindow = true, volatile b32* waiting_for_event = 0)
{
	TIMED_FUNCTION();
#if OPENGL

	if (shouldWaitForEvent && !isActiveWindow && start_waiting_for_event(waiting_for_event))
	{
		TIMED_BLOCK("glfwWaitEvents");
		glfwWaitEvents();
		end_waiting_for_event(waiting_for_event);
	}
	else
	{
		TIMED_BLOCK("glfwPollEvents");
		glfwPollEvents();
	}
	running = glfwWindowShouldClose(window) == 0;

#elif DX12
	// Poll and handle messages (inputs, window resize, etc.)
	// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
	// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
	// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
	// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
	MSG msg;
	ZeroMemory(&msg, sizeof(msg));
	bool hasMsg = false;
	for (;;)
	{
		while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
		{
			hasMsg = true;
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
			if (msg.message == WM_QUIT)
			{
				running = false;
				break;
			}
		}

		if (!running) break;
		if (hasMsg) break;
		if (!shouldWaitForEvent) break;
		Sleep(1);
	}

#endif
	shouldWaitForEvent = false;
}

static void imguiBeginFrame()
{
#if OPENGL

	ImGui_ImplOpenGL2_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

#else

	// Start the Dear ImGui frame
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

#endif
}

static void imguiEndFrame(WINDOW window)
{
	TIMED_FUNCTION();
    ImVec4 clear_color = ImVec4(0.f, 0.f, 0.f, 1.00f);

#if OPENGL
	TIMED_BLOCK_BEGIN("ImGui::Render");
	ImGui::Render();
	TIMED_BLOCK_END("ImGui::Render");

	int display_w, display_h;
	TIMED_BLOCK_BEGIN("glfwGetFramebufferSize");
	glfwGetFramebufferSize(window, &display_w, &display_h);
	TIMED_BLOCK_END("glfwGetFramebufferSize");

	TIMED_BLOCK_BEGIN("glClear");
	glViewport(0, 0, display_w, display_h);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	TIMED_BLOCK_END("glClear");

	TIMED_BLOCK_BEGIN("ImGui_ImplOpenGL2_RenderDrawData");
	ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
	TIMED_BLOCK_END("ImGui_ImplOpenGL2_RenderDrawData");

	TIMED_BLOCK_BEGIN("glfwMakeContextCurrent");
	glfwMakeContextCurrent(window);
	TIMED_BLOCK_END("glfwMakeContextCurrent");

	TIMED_BLOCK_BEGIN("glfwSwapBuffers");
	glfwSwapBuffers(window);
	TIMED_BLOCK_END("glfwSwapBuffers");

#elif DX12

	// Rendering
	FrameContext* frameCtxt = WaitForNextFrameResources();
	UINT backBufferIdx = g_pSwapChain->GetCurrentBackBufferIndex();
	frameCtxt->CommandAllocator->Reset();

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = g_mainRenderTargetResource[backBufferIdx];
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

	g_pd3dCommandList->Reset(frameCtxt->CommandAllocator, NULL);
	g_pd3dCommandList->ResourceBarrier(1, &barrier);
	g_pd3dCommandList->ClearRenderTargetView(g_mainRenderTargetDescriptor[backBufferIdx], (float*)&clear_color, 0, NULL);
	g_pd3dCommandList->OMSetRenderTargets(1, &g_mainRenderTargetDescriptor[backBufferIdx], FALSE, NULL);
	g_pd3dCommandList->SetDescriptorHeaps(1, &g_pd3dSrvDescHeap);
	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_pd3dCommandList);
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	g_pd3dCommandList->ResourceBarrier(1, &barrier);
	g_pd3dCommandList->Close();

	g_pd3dCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&g_pd3dCommandList);

	g_pSwapChain->Present(1, 0); // Present with vsync
	//g_pSwapChain->Present(0, 0); // Present without vsync

	UINT64 fenceValue = g_fenceLastSignaledValue + 1;
	g_pd3dCommandQueue->Signal(g_fence, fenceValue);
	g_fenceLastSignaledValue = fenceValue;
	frameCtxt->FenceValue = fenceValue;
#endif
}

static void imguiCleanup(WINDOW window)
{
#if OPENGL

	ImGui_ImplOpenGL2_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	glfwDestroyWindow(window);
	glfwTerminate();

#elif DX12

	WaitForLastSubmittedFrame();
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	CleanupDeviceD3D();
	::DestroyWindow(window.hwnd);
	::UnregisterClass(window.wc.lpszClassName, window.wc.hInstance);

#endif
}
