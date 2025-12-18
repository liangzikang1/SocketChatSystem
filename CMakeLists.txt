cmake_minimum_required(VERSION 3.10)
project(SocketChatSystem VERSION 1.0)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Compiler Options
add_compile_options(-Wall -Wextra -g)

# ==========================================
# Dependencies
# ==========================================
find_package(Threads REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glfw3 3.3 REQUIRED)

# ==========================================
# Server Build
# ==========================================
add_executable(server src/Server.cpp)
target_link_libraries(server PRIVATE Threads::Threads)

# ==========================================
# Console Client Build
# ==========================================
add_executable(client src/Client.cpp)
target_link_libraries(client PRIVATE Threads::Threads)

# ==========================================
# GUI Client Build (ImGui)
# ==========================================

# ImGui source files
set(IMGUI_DIR "lib/imgui")
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

# GUI Client executable
add_executable(client_gui src/client_gui.cpp src/file_dialog.mm ${IMGUI_SOURCES})

# Include directories for ImGui
target_include_directories(client_gui PRIVATE 
    ${IMGUI_DIR} 
    ${IMGUI_DIR}/backends
    src
)

# Link libraries
target_link_libraries(client_gui PRIVATE 
    Threads::Threads
    glfw
    OpenGL::GL
)

# macOS specific: Link to Cocoa framework
if(APPLE)
    target_link_libraries(client_gui PRIVATE "-framework Cocoa" "-framework IOKit")
endif()