cmake_minimum_required(VERSION 3.20)
project(BedrockClient CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
include(FetchContent)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.5)
FetchContent_Declare(minhook GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git GIT_TAG master)
FetchContent_MakeAvailable(imgui minhook)

add_library(client SHARED src/dllmain.cpp
  ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(client PRIVATE ${imgui_SOURCE_DIR} ${minhook_SOURCE_DIR}/include src)
target_link_libraries(client PRIVATE minhook d3d11 dxgi dwmapi)
set_target_properties(client PROPERTIES OUTPUT_NAME "BedrockClient")

add_executable(injector injector/injector.cpp)
target_link_libraries(injector PRIVATE advapi32)
