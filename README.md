# SocketChatSystem

<div align="center">

![C++](https://img.shields.io/badge/C++-17-blue.svg)
![Platform](https://img.shields.io/badge/platform-macOS-lightgrey.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)

一个基于 C++17 和 ImGui 的高性能可视化聊天与文件传输系统

[功能特性](#功能特性) • [快速开始](#快速开始) • [使用方法](#使用方法) • [技术架构](#技术架构)

</div>

---

## 📖 项目简介

SocketChatSystem 是一个功能完整的局域网聊天应用，支持多用户实时通信和文件传输。项目采用客户端-服务器架构，使用 POSIX Socket 进行网络通信，ImGui 提供现代化的图形界面。

### 核心亮点

- 🎨 **现代化 GUI** - 使用 Dear ImGui 构建的流畅用户界面
- 📁 **文件传输** - 支持任意大小文件传输，实时进度显示
- 🍎 **原生 macOS 集成** - Cocoa 原生文件选择器，Finder 集成
- ⚡ **高性能** - 多线程架构，UI 不阻塞
- 🔒 **线程安全** - 使用互斥锁保护共享数据

---

## ✨ 功能特性

### 实时聊天
- ✅ 多用户在线聊天
- ✅ 用户上线/下线通知
- ✅ 在线用户列表实时更新
- ✅ 消息气泡式显示（发送者右对齐，接收者左对齐）

### 文件传输
- ✅ **原生文件选择器**（macOS NSOpenPanel）
- ✅ **分块传输**（4KB 块大小，支持大文件）
- ✅ **实时进度条**（百分比显示）
- ✅ **一键打开文件夹**（Finder 自动定位文件）
- ✅ **多文件并发传输**

### 用户体验
- ✅ 固定底部输入区域
- ✅ 聊天历史独立滚动
- ✅ 文件传输状态始终可见
- ✅ 系统消息提示
- ✅ 连接状态指示

---

## 🚀 快速开始

### 环境要求

- **操作系统**: macOS (M1/M2/Intel 均支持)
- **编译器**: Clang (支持 C++17)
- **构建工具**: CMake 3.10+
- **依赖库**:
  - OpenGL
  - GLFW 3.3+
  - Dear ImGui (已包含)

### 安装依赖

```bash
# 使用 Homebrew 安装 GLFW
brew install glfw cmake
```

### 编译项目

```bash
# 克隆仓库
git clone https://github.com/liangzikang1/SocketChatSystem.git
cd SocketChatSystem

# 创建构建目录
mkdir -p build
cd build

# 编译
cmake ..
make

# 编译成功后会生成三个可执行文件：
# - server (服务器)
# - client (控制台客户端)
# - client_gui (GUI 客户端)
```

---

## 📱 使用方法

### 1. 启动服务器

```bash
cd build
./server
```

服务器将在 **8080 端口**监听连接。

### 2. 启动客户端

#### GUI 客户端（推荐）

```bash
./client_gui
```

**连接设置：**
- **Server IP**: `127.0.0.1` (本地) 或实际服务器 IP
- **Port**: `8080`
- **Username**: 任意用户名（1-20 字符）

#### 控制台客户端

```bash
./client
```

### 3. 使用功能

#### 发送消息
1. 在输入框中输入消息
2. 按 `Enter` 或点击 **Send** 按钮

#### 发送文件
1. 点击 **Send File** 按钮
2. 在原生文件选择器中选择文件
3. 等待传输完成
4. 接收方看到 **📁 Open Folder** 按钮

#### 打开接收的文件
1. 文件传输完成后，显示绿色 `[Received]` 标签
2. 点击 **📁 Open Folder** 按钮
3. Finder 自动打开并选中文件

---

## 🏗️ 技术架构

### 系统架构

```
┌─────────────┐         ┌─────────────┐
│   Client A  │         │   Client B  │
│  (GUI/CLI)  │         │  (GUI/CLI)  │
└──────┬──────┘         └──────┬──────┘
       │                       │
       │    TCP/IP Socket      │
       └───────────┬───────────┘
                   │
            ┌──────▼──────┐
            │   Server    │
            │  (Port 8080)│
            └─────────────┘
```

### 客户端架构（GUI）

```
┌─────────────────────────────────┐
│         UI 主线程                │
│  - ImGui 渲染 (60 FPS)          │
│  - 用户交互处理                  │
└────────┬────────────────────────┘
         │
         │ SafeQueue (线程安全队列)
         │
┌────────▼────────────────────────┐
│      网络子线程                  │
│  - Socket 收发                   │
│  - 消息解析                      │
│  - 文件传输                      │
└─────────────────────────────────┘
```

### 协议设计

```cpp
// 消息头
struct Header {
    uint32_t length;  // 包体长度
    uint8_t type;     // 消息类型
};

// 消息类型
enum MessageType {
    MSG_LOGIN = 1,      // 登录
    MSG_CHAT = 2,       // 聊天消息
    MSG_FILE = 3,       // 文件元信息
    MSG_FILE_DATA = 4,  // 文件数据块
    MSG_PROGRESS = 5,   // 传输进度
};
```

---

## 📂 项目结构

```
SocketChatSystem/
├── CMakeLists.txt          # 构建配置
├── README.md               # 项目文档
├── NEED.md                 # 需求规格
├── PLAN.md                 # 开发计划
├── src/
│   ├── Server.cpp          # 服务器实现
│   ├── Client.cpp          # 控制台客户端
│   ├── client_gui.cpp      # GUI 客户端
│   ├── Protocol.h          # 通信协议定义
│   ├── SafeQueue.h         # 线程安全队列
│   ├── file_dialog.h       # 文件对话框接口
│   └── file_dialog.mm      # macOS 原生文件选择器
├── lib/
│   └── imgui/              # Dear ImGui 库
└── build/
    ├── server              # 服务器可执行文件
    ├── client              # 控制台客户端
    ├── client_gui          # GUI 客户端
    └── downloads/          # 接收文件保存目录
```

---

## 🔧 技术栈

| 组件 | 技术 |
|------|------|
| **编程语言** | C++17 |
| **网络通信** | POSIX Socket (TCP) |
| **GUI 框架** | Dear ImGui + GLFW + OpenGL |
| **原生 API** | Cocoa (NSOpenPanel, Finder) |
| **并发模型** | C++ Thread + Mutex |
| **构建工具** | CMake |

---

## 🎯 功能演示

### 聊天界面
```
┌────────────────────────────────────────┐
│ Online Users (2)  │  Chat Window       │
│ ─────────────────── │ ──────────────────│
│ • Alice (You)      │ [System] Connected │
│ • Bob              │ Bob: Hello!        │
│                    │          Me: Hi!   │
│                    │ ───────────────────│
│                    │ File Transfers:    │
│                    │ [Received] pic.png │
│                    │   📁 Open  Remove  │
│                    │ ───────────────────│
│                    │ [Input] Send • Send File │
└────────────────────────────────────────┘
```

---

## 🛠️ 开发计划

- [x] 基础聊天功能
- [x] GUI 界面（ImGui）
- [x] 文件传输（分块）
- [x] 进度条显示
- [x] 原生文件选择器
- [x] Finder 集成
- [ ] 文件传输加密
- [ ] 断点续传
- [ ] 群组聊天
- [ ] 历史记录保存

---

## 🤝 贡献

欢迎提交 Issue 和 Pull Request！

---

## 📄 许可证

本项目采用 MIT 许可证 - 详见 [LICENSE](LICENSE) 文件

---

## 👨‍💻 作者

Created by [Liang Zikang](https://github.com/liangzikang1)

---

<div align="center">

**如果这个项目对你有帮助，请给个 ⭐️ Star 支持一下！**

Made with ❤️ using C++

</div>
