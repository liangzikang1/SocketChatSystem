# Windows 部署指南 (BuildForWindows.md)

本文档详细说明将 SocketChatSystem 项目从 macOS 移植到 Windows 平台所需的修改。

---

## 📋 概述

SocketChatSystem 当前使用了多个 macOS 和 POSIX 特定的 API，需要进行以下主要修改才能在 Windows 上运行：

### 需要修改的核心部分

1. **网络编程** - POSIX Socket → Winsock2
2. **文件对话框** - Cocoa (NSOpenPanel) → Windows API (或第三方库)
3. **构建系统** - CMake 配置需要适配 Windows
4. **系统调用** - `mkdir()`, `system()` 等需要替换
5. **依赖管理** - GLFW 和 OpenGL 的 Windows 版本安装

---

## 🔧 详细修改清单

### 1. 网络编程 (POSIX → Winsock2)

#### 受影响的文件
- `src/Server.cpp`
- `src/Client.cpp`
- `src/client_gui.cpp`

#### 需要修改的代码

##### 头文件替换

**macOS/Linux (POSIX):**
```cpp
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
```

**Windows (Winsock2):**
```cpp
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")  // 链接 Winsock 库
```

---

##### Winsock 初始化

Windows 需要在使用 Socket 之前初始化 Winsock。

**在 `main()` 函数开头添加:**
```cpp
// Windows: 初始化 Winsock
WSADATA wsaData;
int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
if (iResult != 0) {
    std::cerr << "WSAStartup failed: " << iResult << std::endl;
    return -1;
}
```

**在程序结束前添加:**
```cpp
// Windows: 清理 Winsock
WSACleanup();
```

---

##### Socket 操作替换

| POSIX (macOS/Linux) | Windows (Winsock2) | 说明 |
|---------------------|--------------------|----|
| `close(sock)` | `closesocket(sock)` | 关闭 socket |
| `int sock` | `SOCKET sock` | socket 类型 |
| 错误码: `errno` | `WSAGetLastError()` | 获取错误码 |
| `read(sock, ...)` | `recv(sock, ...)` | Windows 推荐统一用 recv |
| `write(sock, ...)` | `send(sock, ...)` | Windows 推荐统一用 send |

**示例修改 (Server.cpp):**
```cpp
// macOS 版本:
close(client_fd);

// Windows 版本:
closesocket(client_fd);
```

---

##### SO_REUSEPORT 选项

**问题位置:** `src/Server.cpp` 第 211-214 行

```cpp
// macOS 特定选项，Windows 不支持
if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt))) {
    log("setsockopt SO_REUSEPORT failed: " + std::string(strerror(errno)));
    return -1;
}
```

**Windows 修改方案:**
```cpp
#ifdef _WIN32
    // Windows: 只需要 SO_REUSEADDR
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt))) {
        log("setsockopt SO_REUSEADDR failed");
        return -1;
    }
#else
    // macOS/Linux: 需要 SO_REUSEADDR 和 SO_REUSEPORT
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        log("setsockopt SO_REUSEADDR failed: " + std::string(strerror(errno)));
        return -1;
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt))) {
        log("setsockopt SO_REUSEPORT failed: " + std::string(strerror(errno)));
        return -1;
    }
#endif
```

---

### 2. 文件对话框 (file_dialog.mm → Windows 实现)

#### 受影响的文件
- `src/file_dialog.mm` (需要创建 Windows 版本)
- `CMakeLists.txt` (构建配置)

#### 方案 A: 使用 Windows 原生 API

**创建新文件:** `src/file_dialog_win.cpp`

```cpp
#include "file_dialog.h"
#include <windows.h>
#include <commdlg.h>
#include <string>

std::string open_file_dialog() {
    OPENFILENAMEA ofn;
    char szFile[260] = {0};
    
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = "Select File to Send";
    
    if (GetOpenFileNameA(&ofn) == TRUE) {
        return std::string(szFile);
    }
    
    return ""; // 用户取消选择
}
```

#### 方案 B: 使用跨平台库 (推荐)

使用 **nativefiledialog** 或 **tinyfiledialogs** 库，可以同时支持 Windows、macOS 和 Linux。

**nativefiledialog 示例:**
```cpp
#include "file_dialog.h"
#include <nfd.h>

std::string open_file_dialog() {
    nfdchar_t *outPath = NULL;
    nfdresult_t result = NFD_OpenDialog(NULL, NULL, &outPath);
    
    if (result == NFD_OKAY) {
        std::string path(outPath);
        free(outPath);
        return path;
    }
    
    return "";
}
```

---

### 3. 系统调用修改

#### 3.1 目录创建 (`mkdir`)

**问题位置:** `src/client_gui.cpp` 第 217 行

```cpp
// POSIX 版本 (macOS/Linux):
mkdir("./downloads", 0755);
```

**Windows 版本:**
```cpp
#ifdef _WIN32
    #include <direct.h>
    _mkdir("./downloads");  // Windows 不需要权限参数
#else
    #include <sys/stat.h>
    mkdir("./downloads", 0755);
#endif
```

---

#### 3.2 打开文件夹命令 (`system`)

**问题位置:** `src/client_gui.cpp` 第 554 行

```cpp
// macOS 版本: 使用 Finder 打开并选中文件
std::string cmd = "open -R \"" + transfer.saved_path + "\"";
system(cmd.c_str());
```

**Windows 版本:**
```cpp
#ifdef _WIN32
    // Windows: 使用 Explorer 打开并选中文件
    std::string cmd = "explorer /select,\"" + transfer.saved_path + "\"";
    system(cmd.c_str());
#elif __APPLE__
    // macOS: 使用 Finder
    std::string cmd = "open -R \"" + transfer.saved_path + "\"";
    system(cmd.c_str());
#else
    // Linux: 使用文件管理器打开目录
    std::string cmd = "xdg-open \"" + get_directory(transfer.saved_path) + "\"";
    system(cmd.c_str());
#endif
```

---

#### 3.3 睡眠函数

**问题位置:** `src/Client.cpp` 第 58, 60 行

```cpp
// POSIX 版本:
#include <unistd.h>
sleep(1);  // 秒为单位
```

**Windows 版本:**
```cpp
#ifdef _WIN32
    #include <windows.h>
    Sleep(1000);  // 毫秒为单位
#else
    #include <unistd.h>
    sleep(1);
#endif
```

---

### 4. CMake 构建配置修改

#### 修改 `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.10)
project(SocketChatSystem VERSION 1.0)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# ==========================================
# Platform-specific settings
# ==========================================
if(WIN32)
    # Windows: 添加 Winsock2 库
    set(PLATFORM_LIBS ws2_32)
    set(FILE_DIALOG_SRC src/file_dialog_win.cpp)
    
    # Windows: 禁用控制台窗口 (可选)
    # set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS")
elseif(APPLE)
    # macOS: 添加 Cocoa 框架
    set(PLATFORM_LIBS "-framework Cocoa" "-framework IOKit")
    set(FILE_DIALOG_SRC src/file_dialog.mm)
else()
    # Linux
    set(PLATFORM_LIBS "")
    set(FILE_DIALOG_SRC src/file_dialog.cpp)  # 需要创建 Linux 版本
endif()

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
target_link_libraries(server PRIVATE Threads::Threads ${PLATFORM_LIBS})

# ==========================================
# Console Client Build
# ==========================================
add_executable(client src/Client.cpp)
target_link_libraries(client PRIVATE Threads::Threads ${PLATFORM_LIBS})

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
add_executable(client_gui src/client_gui.cpp ${FILE_DIALOG_SRC} ${IMGUI_SOURCES})

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
    ${PLATFORM_LIBS}
)

# Windows: 可能需要额外的链接选项
if(WIN32)
    # 如果使用 nativefiledialog，添加:
    # target_link_libraries(client_gui PRIVATE nfd)
    
    # 如果使用 Windows API 文件对话框，添加:
    target_link_libraries(client_gui PRIVATE comdlg32)
endif()
```

---

### 5. 字体文件路径

#### 问题位置
`src/client_gui.cpp` 第 337, 352 行

```cpp
// 相对路径可能在 Windows 上有问题
ImFont* font = io.Fonts->AddFontFromFileTTF("../lib/fonts/Menlo.ttc", 16.0f, &config);
ImFont* chinese_font = io.Fonts->AddFontFromFileTTF("../lib/fonts/Songti.ttc", 16.0f, &chinese_config, glyph_ranges);
```

#### Windows 建议

1. **使用绝对路径或可靠的相对路径:**
```cpp
#ifdef _WIN32
    const char* font_path = "..\\lib\\fonts\\Menlo.ttc";  // Windows 路径分隔符
    const char* chinese_font_path = "..\\lib\\fonts\\Songti.ttc";
#else
    const char* font_path = "../lib/fonts/Menlo.ttc";
    const char* chinese_font_path = "../lib/fonts/Songti.ttc";
#endif
```

2. **或者使用跨平台路径:**
```cpp
// 统一使用正斜杠 (现代 Windows 也支持)
const char* font_path = "../lib/fonts/Menlo.ttc";
```

3. **Windows 系统字体替代方案:**
```cpp
#ifdef _WIN32
    // 使用 Windows 自带字体
    ImFont* font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 16.0f);  // Consolas
    ImFont* chinese_font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", 16.0f, &chinese_config, glyph_ranges);  // 微软雅黑
#else
    // macOS 字体
    ImFont* font = io.Fonts->AddFontFromFileTTF("../lib/fonts/Menlo.ttc", 16.0f, &config);
    ImFont* chinese_font = io.Fonts->AddFontFromFileTTF("../lib/fonts/Songti.ttc", 16.0f, &chinese_config, glyph_ranges);
#endif
```

---

## 🔨 Windows 开发环境配置

### 1. 安装必要工具

#### Visual Studio (推荐)
- 下载并安装 **Visual Studio 2019/2022 Community**
- 选择工作负载: "使用 C++ 的桌面开发"
- 包含组件: CMake, MSVC, Windows SDK

#### 或者使用 MinGW-w64
```bash
# 使用 MSYS2 安装 MinGW
pacman -S mingw-w64-x86_64-gcc
pacman -S mingw-w64-x86_64-cmake
```

---

### 2. 安装依赖库

#### GLFW

**方法 A: vcpkg (推荐)**
```bash
# 安装 vcpkg
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat

# 安装 GLFW
.\vcpkg install glfw3:x64-windows
```

**方法 B: 手动编译**
```bash
# 下载 GLFW 源码
git clone https://github.com/glfw/glfw.git
cd glfw
mkdir build && cd build
cmake .. -G "Visual Studio 16 2019" -A x64
cmake --build . --config Release
cmake --install . --prefix "C:/Program Files/GLFW"
```

---

#### OpenGL

Windows 默认包含 OpenGL 头文件和库，但可能需要安装 GLEW：

```bash
# 使用 vcpkg
.\vcpkg install glew:x64-windows
```

---

### 3. CMake 配置

```bash
# 创建构建目录
mkdir build
cd build

# 配置项目 (使用 vcpkg)
cmake .. -G "Visual Studio 16 2019" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake

# 或者使用 MinGW
cmake .. -G "MinGW Makefiles"

# 编译
cmake --build . --config Release
```

---

## 📝 完整修改建议的文件列表

### 需要创建的新文件
1. ✅ `src/file_dialog_win.cpp` - Windows 文件对话框实现

### 需要修改的现有文件

#### 1. `src/Server.cpp`
- [ ] 添加 Winsock2 头文件和初始化
- [ ] 替换 `close()` 为 `closesocket()`
- [ ] 修改 `SO_REUSEPORT` 为条件编译
- [ ] 替换 `strerror(errno)` 为 `WSAGetLastError()`

#### 2. `src/Client.cpp`
- [ ] 添加 Winsock2 头文件和初始化
- [ ] 替换 `close()` 为 `closesocket()`
- [ ] 替换 `sleep()` 为平台兼容版本

#### 3. `src/client_gui.cpp`
- [ ] 添加 Winsock2 头文件和初始化
- [ ] 替换 `close()` 为 `closesocket()`
- [ ] 修改 `mkdir()` 为跨平台版本
- [ ] 修改 `system("open -R ...")` 为跨平台版本
- [ ] 调整字体文件路径

#### 4. `CMakeLists.txt`
- [ ] 添加平台检测 (`if(WIN32)`)
- [ ] 添加 Winsock2 库链接
- [ ] 条件编译文件对话框源文件
- [ ] Windows 特定链接选项

---

## ✅ 验证步骤

### 编译测试
```bash
cd build
cmake --build . --config Release

# 应该生成:
# - server.exe
# - client.exe
# - client_gui.exe
```

### 功能测试

1. **启动服务器:**
```bash
.\server.exe
```

2. **启动 GUI 客户端:**
```bash
.\client_gui.exe
```

3. **测试文件传输:**
   - 点击 "Send File" 按钮
   - 应该弹出 Windows 文件选择对话框
   - 选择文件后测试传输
   - 传输完成后点击 "Open Folder"
   - 应该在 Windows Explorer 中打开并选中文件

---

## 🎯 推荐的跨平台代码组织

### 创建平台抽象层

**src/platform.h:**
```cpp
#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #include <direct.h>
    #pragma comment(lib, "ws2_32.lib")
    
    #define CLOSE_SOCKET(s) closesocket(s)
    #define SLEEP_MS(ms) Sleep(ms)
    #define MKDIR(path) _mkdir(path)
    typedef SOCKET socket_t;
#else
    #include <unistd.h>
    #include <arpa/inet.h>
    #include <sys/socket.h>
    #include <sys/stat.h>
    
    #define CLOSE_SOCKET(s) close(s)
    #define SLEEP_MS(ms) usleep((ms) * 1000)
    #define MKDIR(path) mkdir(path, 0755)
    typedef int socket_t;
#endif

// 跨平台函数
void platform_init();
void platform_cleanup();
void open_file_location(const std::string& filepath);

#endif
```

**src/platform.cpp:**
```cpp
#include "platform.h"
#include <iostream>

void platform_init() {
#ifdef _WIN32
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed: " << result << std::endl;
    }
#endif
}

void platform_cleanup() {
#ifdef _WIN32
    WSACleanup();
#endif
}

void open_file_location(const std::string& filepath) {
#ifdef _WIN32
    std::string cmd = "explorer /select,\"" + filepath + "\"";
#elif __APPLE__
    std::string cmd = "open -R \"" + filepath + "\"";
#else
    // Linux 实现
    std::string cmd = "xdg-open \"" + filepath + "\"";
#endif
    system(cmd.c_str());
}
```

---

## 📚 参考资源

### Windows Socket 编程
- [Winsock 官方文档](https://docs.microsoft.com/en-us/windows/win32/winsock)
- [POSIX to Winsock 移植指南](https://docs.microsoft.com/en-us/windows/win32/winsock/porting-socket-applications-to-winsock)

### CMake 跨平台构建
- [CMake 平台检测](https://cmake.org/cmake/help/latest/variable/WIN32.html)
- [vcpkg 包管理器](https://github.com/microsoft/vcpkg)

### 文件对话框库
- [nativefiledialog](https://github.com/mlabbe/nativefiledialog)
- [tinyfiledialogs](https://sourceforge.net/projects/tinyfiledialogs/)

---

## ⚠️ 常见问题

### Q1: 链接错误 "unresolved external symbol"
**A:** 确保添加了 `ws2_32.lib` 和其他必要的 Windows 库。

### Q2: 字体文件找不到
**A:** 使用绝对路径或确保工作目录正确。可以使用 Windows 系统字体。

### Q3: 文件对话框不显示
**A:** 确保正确链接了 `comdlg32.lib` 或使用了正确的第三方库。

### Q4: 中文显示乱码
**A:** 确保使用 UTF-8 编码，并且字体文件包含中文字符。

---

## 📊 总结

### 修改工作量评估

| 任务 | 难度 | 时间估计 |
|------|------|---------|
| Socket API 迁移 | 中 | 2-3 小时 |
| 文件对话框实现 | 低 | 1 小时 |
| CMake 配置 | 低 | 1 小时 |
| 系统调用修改 | 低 | 0.5 小时 |
| 测试和调试 | 中 | 2-3 小时 |
| **总计** | **中** | **约 6-8 小时** |

### 优先级建议

1. **高优先级** (必须修改才能编译):
   - Socket API (Winsock2)
   - CMake 构建配置
   - 文件对话框实现

2. **中优先级** (影响功能):
   - 目录创建 (mkdir)
   - 打开文件夹命令

3. **低优先级** (可选优化):
   - 字体路径优化
   - 平台抽象层重构

---

**祝你移植顺利！如有问题欢迎反馈。** 🚀
