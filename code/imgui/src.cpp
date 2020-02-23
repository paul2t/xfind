
//---- Using 32-bits vertex indices (default is 16-bits) is one way to allow large meshes with more than 64K vertices. 
// Your renderer back-end will need to support it (most example renderer back-ends support both 16/32-bits indices).
// Another way to allow large meshes while keeping 16-bits indices is to handle ImDrawCmd::VtxOffset in your renderer. 
// Read about ImGuiBackendFlags_RendererHasVtxOffset for details.
#define ImDrawIdx unsigned int


#include "imgui/imgui.cpp"
#include "imgui/imgui_widgets.cpp"
#include "imgui/imgui_draw.cpp"

#if OPENGL
#include "imgui/imgui_impl_glfw.cpp"
#include "imgui/imgui_impl_opengl2.cpp"
#elif DX12
#include "imgui/imgui_impl_win32.cpp"
#include "imgui/imgui_impl_dx12.cpp"
#endif
