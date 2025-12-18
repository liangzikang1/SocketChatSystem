#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <vector>
#include <string>
#include <iostream>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <sys/stat.h>
#include "Protocol.h"
#include "SafeQueue.h"
#include "file_dialog.h"

#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>

const size_t CHUNK_SIZE = 4096; // 每次发送 4KB

struct ChatMessage {
    std::string sender;
    std::string content;
    bool is_me;
};

struct FileTransferStatus {
    std::string filename;
    uint64_t total_size;
    uint64_t sent_size;
    float progress; // 0.0 ~ 1.0
    bool is_sending; // true=发送中, false=接收中
};

struct AppContext {
    int sock = -1;
    bool is_connected = false;
    std::string username;
    SafeQueue<std::string> recv_queue;
    std::vector<ChatMessage> chat_history;
    std::vector<std::string> online_users;
    std::vector<FileTransferStatus> file_transfers;
};

AppContext g_ctx;
std::mutex g_ctx_mutex; // 保护 file_transfers

// 接收文件的状态
static std::ofstream g_current_file;
static uint64_t g_expected_size = 0;
static uint64_t g_received_size = 0;
static std::string g_receiving_filename;

bool send_package(int sock, int type, const void* data, size_t data_size) {
    Header header;
    header.length = data_size;
    header.type = type;
    std::vector<char> package(sizeof(header) + data_size);
    std::memcpy(package.data(), &header, sizeof(header));
    std::memcpy(package.data() + sizeof(header), data, data_size);
    ssize_t sent = send(sock, package.data(), package.size(), 0);
    return sent > 0;
}

void send_file(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "Failed to open file: " << filepath << std::endl;
        g_ctx.recv_queue.push("SYSTEM:Failed to open file: " + filepath);
        return;
    }
    
    uint64_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    // 提取文件名
    std::string filename = filepath.substr(filepath.find_last_of("/\\") + 1);
    
    // 添加到传输列表
    {
        std::lock_guard<std::mutex> lock(g_ctx_mutex);
        FileTransferStatus status;
        status.filename = filename;
        status.total_size = file_size;
        status.sent_size = 0;
        status.progress = 0.0f;
        status.is_sending = true;
        g_ctx.file_transfers.push_back(status);
    }
    
    // 1. 发送文件元信息
    FileMsg file_msg = {};
    strncpy(file_msg.sender, g_ctx.username.c_str(), sizeof(file_msg.sender) - 1);
    file_msg.sender_len = g_ctx.username.length();
    strncpy(file_msg.filename, filename.c_str(), sizeof(file_msg.filename) - 1);
    file_msg.filename_len = filename.length();
    file_msg.file_size = file_size;
    
    if (!send_package(g_ctx.sock, MSG_FILE, &file_msg, sizeof(file_msg))) {
        g_ctx.recv_queue.push("SYSTEM:Failed to send file metadata");
        return;
    }
    
    // 2. 分块发送文件数据
    std::vector<char> buffer(CHUNK_SIZE);
    uint64_t sent = 0;
    
    while (sent < file_size && g_ctx.is_connected) {
        size_t to_read = std::min((uint64_t)CHUNK_SIZE, file_size - sent);
        file.read(buffer.data(), to_read);
        
        // 构造 FileDataMsg
        FileDataMsg data_msg = {};
        strncpy(data_msg.sender, g_ctx.username.c_str(), sizeof(data_msg.sender) - 1);
        data_msg.sender_len = g_ctx.username.length();
        strncpy(data_msg.filename, filename.c_str(), sizeof(data_msg.filename) - 1);
        data_msg.filename_len = filename.length();
        data_msg.offset = sent;
        data_msg.data_len = to_read;
        
        // 发送包含数据的完整包
        std::vector<char> package(sizeof(data_msg) + to_read);
        std::memcpy(package.data(), &data_msg, sizeof(data_msg));
        std::memcpy(package.data() + sizeof(data_msg), buffer.data(), to_read);
        
        if (!send_package(g_ctx.sock, MSG_FILE_DATA, package.data(), package.size())) {
            g_ctx.recv_queue.push("SYSTEM:Failed to send file data");
            break;
        }
        
        sent += to_read;
        
        // 更新进度
        {
            std::lock_guard<std::mutex> lock(g_ctx_mutex);
            for (auto& transfer : g_ctx.file_transfers) {
                if (transfer.filename == filename && transfer.is_sending) {
                    transfer.sent_size = sent;
                    transfer.progress = (float)sent / file_size;
                    break;
                }
            }
        }
        
        // 发送进度更新
        if (sent % (CHUNK_SIZE * 10) == 0 || sent == file_size) { // 每10个块发送一次进度
            ProgressMsg prog = {};
            strncpy(prog.sender, g_ctx.username.c_str(), sizeof(prog.sender) - 1);
            prog.sender_len = g_ctx.username.length();
            prog.total_size = file_size;
            prog.received_size = sent;
            send_package(g_ctx.sock, MSG_PROGRESS, &prog, sizeof(prog));
        }
    }
    
    file.close();
    
    // 完成后从传输列表移除
    if (sent == file_size) {
        g_ctx.recv_queue.push("SYSTEM:File sent successfully: " + filename);
        std::lock_guard<std::mutex> lock(g_ctx_mutex);
        g_ctx.file_transfers.erase(
            std::remove_if(g_ctx.file_transfers.begin(), g_ctx.file_transfers.end(),
                [&filename](const FileTransferStatus& t) { 
                    return t.filename == filename && t.is_sending; 
                }),
            g_ctx.file_transfers.end()
        );
    }
}

void network_thread_func() {
    Header header;
    while (g_ctx.is_connected) {
        // 1. 读取头部 (阻塞)
        ssize_t len = recv(g_ctx.sock, &header, sizeof(header), 0);
        if (len <= 0) {
            // 连接断开
            g_ctx.recv_queue.push("SYSTEM:Disconnected from server.");
            g_ctx.is_connected = false;
            close(g_ctx.sock);
            g_ctx.sock = -1;
            break;
        }

        // 2. 读取包体
        std::vector<char> body(header.length + 1, 0);
        ssize_t body_len = recv(g_ctx.sock, body.data(), header.length, 0);
        if (body_len <= 0) {
            g_ctx.is_connected = false;
            break;
        }

        // 3. 处理消息
        std::string msg_content(body.data(), header.length);
        if (header.type == MSG_LOGIN) {
            // 格式: "username connected" - 提取用户名并添加到在线列表
            g_ctx.recv_queue.push("LOGIN:" + msg_content);
        } else if (header.type == MSG_CHAT) {
            // 格式: "sender: message"
            g_ctx.recv_queue.push("CHAT:" + msg_content);
        } else if (header.type == MSG_FILE) {
            // 接收文件元信息
            FileMsg* file_msg = (FileMsg*)body.data();
            g_receiving_filename = std::string(file_msg->filename, file_msg->filename_len);
            g_expected_size = file_msg->file_size;
            g_received_size = 0;
            
            // 创建 downloads 目录
            mkdir("./downloads", 0755);
            
            // 打开文件准备写入
            std::string save_path = "./downloads/" + g_receiving_filename;
            g_current_file.open(save_path, std::ios::binary);
            
            if (!g_current_file) {
                g_ctx.recv_queue.push("SYSTEM:Failed to create file: " + g_receiving_filename);
            } else {
                // 添加到传输列表
                std::lock_guard<std::mutex> lock(g_ctx_mutex);
                FileTransferStatus status;
                status.filename = g_receiving_filename;
                status.total_size = g_expected_size;
                status.sent_size = 0;
                status.progress = 0.0f;
                status.is_sending = false;
                g_ctx.file_transfers.push_back(status);
                
                std::string sender(file_msg->sender, file_msg->sender_len);
                g_ctx.recv_queue.push("SYSTEM:Receiving file from " + sender + ": " + g_receiving_filename);
            }
        } else if (header.type == MSG_FILE_DATA) {
            // 接收文件数据块
            const char* file_data = body.data() + sizeof(FileDataMsg);
            size_t data_len = header.length - sizeof(FileDataMsg);
            
            if (g_current_file.is_open()) {
                g_current_file.write(file_data, data_len);
                g_received_size += data_len;
                
                // 更新进度
                {
                    std::lock_guard<std::mutex> lock(g_ctx_mutex);
                    for (auto& transfer : g_ctx.file_transfers) {
                        if (transfer.filename == g_receiving_filename && !transfer.is_sending) {
                            transfer.sent_size = g_received_size;
                            transfer.progress = (float)g_received_size / g_expected_size;
                            break;
                        }
                    }
                }
                
                // 检查是否接收完成
                if (g_received_size >= g_expected_size) {
                    g_current_file.close();
                    g_ctx.recv_queue.push("SYSTEM:File received successfully: " + g_receiving_filename);
                    
                    // 从传输列表移除
                    std::lock_guard<std::mutex> lock(g_ctx_mutex);
                    g_ctx.file_transfers.erase(
                        std::remove_if(g_ctx.file_transfers.begin(), g_ctx.file_transfers.end(),
                            [](const FileTransferStatus& t) { 
                                return t.filename == g_receiving_filename && !t.is_sending; 
                            }),
                        g_ctx.file_transfers.end()
                    );
                }
            }
        } else if (header.type == MSG_PROGRESS) {
            g_ctx.recv_queue.push("PROGRESS:" + msg_content);
        }
    }
}

bool connect_to_server(const char* ip, int port) {
    g_ctx.sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_ctx.sock < 0) {
        return false;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &server_addr.sin_addr);

    if (connect(g_ctx.sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(g_ctx.sock);
        g_ctx.sock = -1;
        return false;
    }

    g_ctx.is_connected = true;
    return true;
}

static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

int main(int, char**) {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    // GL 3.2 + GLSL 150
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    // 创建窗口
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Chat Client", nullptr, nullptr);
    if (window == nullptr)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // 状态变量
    static char server_ip[128] = "127.0.0.1";
    static int server_port = 8080;
    static char username_buf[64] = "";
    static char message_buf[256] = "";
    bool show_connect_window = true;
    std::thread* network_thread = nullptr;

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // 从接收队列中取出消息，解析并添加到聊天历史或更新在线用户
        std::string msg;
        while (g_ctx.recv_queue.try_pop(msg)) {
            // 解析消息类型前缀
            if (msg.find("LOGIN:") == 0) {
                // 格式: "LOGIN:username connected"
                std::string content = msg.substr(6); // 跳过 "LOGIN:"
                size_t pos = content.find(" connected");
                if (pos != std::string::npos) {
                    std::string new_user = content.substr(0, pos);
                    // 添加到在线用户列表
                    auto it = std::find(g_ctx.online_users.begin(), g_ctx.online_users.end(), new_user);
                    if (it == g_ctx.online_users.end()) {
                        g_ctx.online_users.push_back(new_user);
                    }
                    // 添加系统消息
                    ChatMessage sys_msg;
                    sys_msg.sender = "System";
                    sys_msg.content = new_user + " joined the chat";
                    sys_msg.is_me = false;
                    g_ctx.chat_history.push_back(sys_msg);
                }
            } else if (msg.find("CHAT:") == 0) {
                // 格式: "CHAT:sender: message"
                std::string content = msg.substr(5); // 跳过 "CHAT:"
                size_t colon_pos = content.find(": ");
                if (colon_pos != std::string::npos) {
                    std::string sender = content.substr(0, colon_pos);
                    std::string message = content.substr(colon_pos + 2);
                    ChatMessage chat_msg;
                    chat_msg.sender = sender;
                    chat_msg.content = message;
                    chat_msg.is_me = (sender == g_ctx.username);
                    g_ctx.chat_history.push_back(chat_msg);
                }
            } else if (msg.find("SYSTEM:") == 0) {
                // 系统消息
                ChatMessage sys_msg;
                sys_msg.sender = "System";
                sys_msg.content = msg.substr(7);
                sys_msg.is_me = false;
                g_ctx.chat_history.push_back(sys_msg);
            }
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // 连接窗口
        if (show_connect_window) {
            ImGui::SetNextWindowPos(ImVec2(400, 250), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(480, 220), ImGuiCond_FirstUseEver);
            ImGui::Begin("Connect to Server", &show_connect_window);
            
            ImGui::InputText("Server IP", server_ip, IM_ARRAYSIZE(server_ip));
            ImGui::InputInt("Port", &server_port);
            ImGui::InputText("Username", username_buf, IM_ARRAYSIZE(username_buf));
            
            if (ImGui::Button("Connect")) {
                if (connect_to_server(server_ip, server_port)) {
                    g_ctx.username = username_buf;
                    // 添加自己到在线列表
                    g_ctx.online_users.push_back(g_ctx.username);
                    // 发送登录包
                    send_package(g_ctx.sock, MSG_LOGIN, username_buf, strlen(username_buf));
                    
                    // 启动网络线程
                    network_thread = new std::thread(network_thread_func);
                    network_thread->detach();
                    
                    show_connect_window = false;
                    ChatMessage sys_msg;
                    sys_msg.sender = "System";
                    sys_msg.content = "Connected to server as " + g_ctx.username;
                    sys_msg.is_me = false;
                    g_ctx.chat_history.push_back(sys_msg);
                } else {
                    ChatMessage sys_msg;
                    sys_msg.sender = "System";
                    sys_msg.content = "Failed to connect to server";
                    sys_msg.is_me = false;
                    g_ctx.chat_history.push_back(sys_msg);
                }
            }
            
            ImGui::End();
        }

        // 主聊天窗口
        if (!show_connect_window && g_ctx.is_connected) {
            ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y), ImGuiCond_Always);
            ImGui::Begin("Chat", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
            
            // 左侧用户列表 (20% 宽度)
            float sidebar_width = io.DisplaySize.x * 0.2f;
            ImGui::BeginChild("UserList", ImVec2(sidebar_width, -ImGui::GetFrameHeightWithSpacing()), true);
            ImGui::Text("Online Users (%zu)", g_ctx.online_users.size());
            ImGui::Separator();
            for (const auto& user : g_ctx.online_users) {
                if (user == g_ctx.username) {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%s (You)", user.c_str());
                } else {
                    ImGui::Text("%s", user.c_str());
                }
            }
            ImGui::EndChild();
            
            ImGui::SameLine();
            
            // 右侧聊天区域 (80% 宽度)
            ImGui::BeginGroup();
            
            // 聊天历史区域
            ImGui::BeginChild("ChatHistory", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true);
            for (const auto& msg : g_ctx.chat_history) {
                if (msg.sender == "System") {
                    // 系统消息居中，灰色
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[%s] %s", msg.sender.c_str(), msg.content.c_str());
                } else if (msg.is_me) {
                    // 我的消息靠右对齐
                    std::string display = "Me: " + msg.content;
                    float text_width = ImGui::CalcTextSize(display.c_str()).x;
                    float window_width = ImGui::GetWindowWidth();
                    ImGui::SetCursorPosX(window_width - text_width - 20); // 20 为滚动条预留空间
                    ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "%s", display.c_str());
                } else {
                    // 其他人的消息靠左对齐
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s: %s", msg.sender.c_str(), msg.content.c_str());
                }
            }
            // 自动滚动到底部
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();
            
            // 输入框
            if (ImGui::InputText("##MessageInput", message_buf, IM_ARRAYSIZE(message_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                if (strlen(message_buf) > 0) {
                    // 发送消息
                    send_package(g_ctx.sock, MSG_CHAT, message_buf, strlen(message_buf));
                    // 立即添加到聊天历史
                    ChatMessage my_msg;
                    my_msg.sender = g_ctx.username;
                    my_msg.content = message_buf;
                    my_msg.is_me = true;
                    g_ctx.chat_history.push_back(my_msg);
                    message_buf[0] = '\0';
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Send")) {
                if (strlen(message_buf) > 0) {
                    send_package(g_ctx.sock, MSG_CHAT, message_buf, strlen(message_buf));
                    ChatMessage my_msg;
                    my_msg.sender = g_ctx.username;
                    my_msg.content = message_buf;
                    my_msg.is_me = true;
                    g_ctx.chat_history.push_back(my_msg);
                    message_buf[0] = '\0';
                }
            }
            
            // 文件传输按钮
            ImGui::SameLine();
            
            if (ImGui::Button("Send File")) {
                // 打开原生文件选择对话框
                std::string filepath = open_file_dialog();
                if (!filepath.empty()) {
                    // 在单独线程中发送文件
                    std::thread send_thread([filepath]() {
                        send_file(filepath);
                    });
                    send_thread.detach();
                }
            }
            
            // 文件传输进度显示
            {
                std::lock_guard<std::mutex> lock(g_ctx_mutex);
                if (!g_ctx.file_transfers.empty()) {
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "File Transfers:");
                    for (const auto& transfer : g_ctx.file_transfers) {
                        std::string label = transfer.is_sending ? "[Sending] " : "[Receiving] ";
                        label += transfer.filename;
                        ImGui::Text("%s", label.c_str());
                        ImGui::SameLine();
                        char progress_text[64];
                        snprintf(progress_text, sizeof(progress_text), "%.1f%%", transfer.progress * 100.0f);
                        ImGui::ProgressBar(transfer.progress, ImVec2(-1, 0), progress_text);
                    }
                }
            }
            
            ImGui::EndGroup();
            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Cleanup
    if (g_ctx.is_connected) {
        g_ctx.is_connected = false;
        close(g_ctx.sock);
    }
    
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
