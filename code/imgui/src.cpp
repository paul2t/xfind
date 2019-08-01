
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
