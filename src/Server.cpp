#include <iostream>
#include <thread>
#include <vector>
#include <map>
#include <mutex>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "Protocol.h"
#include "SafeQueue.h"

// fd->username 展示当前在线用户
std::map<int, std::string> clients;
std::mutex clients_mutex; // 保护 clients 的互斥锁

void log(const std::string& msg) {
    std::cout << "[Server]: " << msg << std::endl; // 日志
}

bool is_valid_username(const std::string& username) {
    return username.length() > 0 && username.length() <= 20;
}

bool recv_exact(int sock, void* buffer, size_t length) {
    size_t received = 0;
    char* ptr = (char*)buffer;
    while (received < length) {
        int result = recv(sock, ptr + received, length - received, 0);
        if (result == 0) { // 连接关闭
            return false;
        } else if (result < 0) { // 错误
            log("recv failed: " + std::string(strerror(errno)));
            return false;
        }
        received += result;
    }
    return true;
}

bool broadcast(int client_fd, const std::string& message) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (const auto& client : clients) {
        if (client.first != client_fd) {
            if (send(client.first, message.c_str(), message.length(), 0) < 0) {
                log("Failed to broadcast message");
                return false;
            }
        }
    }
    return true;
}

// 重构 broadcast 函数，支持包的转发
bool broadcast(int client_fd, const std::vector<char>& package) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (const auto& client : clients) {
        if (client.first != client_fd) {
            if (send(client.first, package.data(), package.size(), 0) < 0) {
                log("Failed to broadcast package");
                return false;
            }
        }
    }
    return true;
}

void handle_client(int client_fd) {
    Header header;
    std::string username = "Unknown";
    bool is_running = true;

    while(is_running) {
        // 依据规则先接收header的头部
        if(!recv_exact(client_fd, &header, sizeof(header))) {
            // log("Failed to receive header (Client disconnected or error)");
            is_running = false;
            break; // 必须 break，否则会继续执行后续逻辑
        }
        // 接受body数据
        std::vector<char> body(header.length);
        if(!recv_exact(client_fd, body.data(), header.length)) {
            log("Failed to receive body");
            is_running = false;
        }

        switch (header.type) {
            case MSG_LOGIN: {
                username = std::string(body.data(), header.length);
                
                // 先发送当前所有在线用户给新登录的客户端
                {
                    std::lock_guard<std::mutex> lock(clients_mutex);
                    for (const auto& client : clients) {
                        if (client.first != client_fd) {
                            // 发送已在线用户的信息
                            std::string user_login = client.second + " connected";
                            Header user_header;
                            user_header.type = MSG_LOGIN;
                            user_header.length = user_login.length();
                            
                            std::vector<char> user_package(sizeof(user_header) + user_login.length());
                            std::memcpy(user_package.data(), &user_header, sizeof(user_header));
                            std::memcpy(user_package.data() + sizeof(user_header), user_login.c_str(), user_login.length());
                            send(client_fd, user_package.data(), user_package.size(), 0);
                        }
                    }
                    // 将新用户添加到在线列表
                    clients[client_fd] = username;
                }
                
                log(username + " connected");
                // 向其他人广播新用户上线了（使用完整的协议格式）
                std::string login_broadcast = username + " connected";
                Header login_header;
                login_header.type = MSG_LOGIN;
                login_header.length = login_broadcast.length();
                
                std::vector<char> login_package(sizeof(login_header) + login_broadcast.length());
                std::memcpy(login_package.data(), &login_header, sizeof(login_header));
                std::memcpy(login_package.data() + sizeof(login_header), login_broadcast.c_str(), login_broadcast.length());
                broadcast(client_fd, login_package);
                break;
            }
            case MSG_CHAT: {
                std::string message(body.data(), header.length);
                log("Msg from " + username + ": " + message);
                
                // 构造带发送者信息的消息：格式为 "sender: message"
                std::string formatted_msg = username + ": " + message;
                Header new_header;
                new_header.type = MSG_CHAT;
                new_header.length = formatted_msg.length();
                
                std::vector<char> package(sizeof(new_header) + formatted_msg.length());
                std::memcpy(package.data(), &new_header, sizeof(new_header));
                std::memcpy(package.data() + sizeof(new_header), formatted_msg.c_str(), formatted_msg.length());
                broadcast(client_fd, package);
                break;
            }
            case MSG_FILE: {
                FileMsg* file_msg = (FileMsg*)body.data();
                log(username + " is sending file: " + file_msg->filename + " (" + std::to_string(file_msg->file_size) + " bytes)");
                
                // 转发文件头信息包
                std::vector<char> package(sizeof(header) + body.size());
                std::memcpy(package.data(), &header, sizeof(header));
                std::memcpy(package.data() + sizeof(header), body.data(), body.size());
                broadcast(client_fd, package);
                break;
            }
            case MSG_FILE_DATA: {
                // 转发文件数据块
                std::vector<char> package(sizeof(header) + body.size());
                std::memcpy(package.data(), &header, sizeof(header));
                std::memcpy(package.data() + sizeof(header), body.data(), body.size());
                broadcast(client_fd, package);
                break;
            }
            case MSG_PROGRESS: {
                ProgressMsg* prog_msg = (ProgressMsg*)body.data();
                // 计算百分比
                double percent = (prog_msg->total_size > 0) ? 
                                 (double)prog_msg->received_size / prog_msg->total_size * 100.0 : 0;
                log("File transfer progress from " + username + ": " + std::to_string((int)percent) + "%");
                
                // 转发进度包
                std::vector<char> package(sizeof(header) + body.size());
                std::memcpy(package.data(), &header, sizeof(header));
                std::memcpy(package.data() + sizeof(header), body.data(), body.size());
                broadcast(client_fd, package);
                break;
            }
            default:
                log("Invalid message type");
                is_running = false;
                break;
        }


    }

    close(client_fd);
    {
        // 加锁消除数据
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.erase(client_fd);
    }
    log(username + " disconnected");
} 

int main() {
    int server_fd, new_socket;
    struct sockaddr_in server_addr;
    int opt = 1;
    int addrlen = sizeof(server_addr);
    const int PORT = 8080;


    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        log("socket failed");
        return -1;  
    }

    // 设置端口复用
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        log("setsockopt SO_REUSEADDR failed: " + std::string(strerror(errno)));
        return -1;
    }
    // macos 需要单独设置 SO_REUSEPORT
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt))) {
        log("setsockopt SO_REUSEPORT failed: " + std::string(strerror(errno)));
        return -1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        log("bind failed");
        return -1;
    }

    if (listen(server_fd, 10) < 0) {
        log("listen failed");
        return -1;
    }

    log("Server started on port " + std::to_string(PORT));

    while (true) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&server_addr, (socklen_t*)&addrlen)) < 0) {
            log("accept failed");
            continue;
        }
        // 获取一下 IP 地址
        char* ip = inet_ntoa(server_addr.sin_addr);
        log("New connection from " + std::string(ip));

        std::thread client_thread(handle_client, new_socket);
        client_thread.detach();
    }
}

