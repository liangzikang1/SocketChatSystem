阶段一：核心网络库封装 (Core Networking) - Day 1
目标：构建与 UI 无关的底层通信能力。

协议定义 (protocol.h)：

定义 Header（长度、类型）和 Body。

增加 MSG_PROGRESS 类型的预留，方便后续做进度条。

线程安全队列 (SafeQueue.h)：

这是 GUI 编程的核心。你需要实现一个模板类 SafeQueue<T>，使用 std::mutex 和 std::condition_variable。

UI 线程从中 try_pop（非阻塞取），网络线程往里 push。


服务器端开发 (Server) - Day 2
目标：高并发后台，逻辑不变。

实现 Server.cpp：

维护 ClientSocket 列表。

处理 Login、Chat、File 消息转发。

注意：服务器不需要 GUI，建议保留为命令行程序，方便在云端或 Docker 中部署。

阶段三：客户端 GUI 框架搭建 (Client Skeleton) - Day 3
目标：跑通一个空的 GUI 窗口。

集成 ImGui (或 Qt)：

配置 CMake，引入 glfw 和 opengl 库。

绘制布局：

写死一些假数据（Dummy Data），画出左边栏（用户列表）、中间（聊天框）、底部（输入框）。

确保中文显示正常（需要加载支持中文的 .ttf 字体文件，否则是乱码）。
