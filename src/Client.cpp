#include <iostream>
#include <cstring>
#include <string>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <vector>
#include "Protocol.h"

void send_package(int sock, int type, const std::string& data) {
    Header header;
    header.length = data.length();
    header.type = type;
    std::vector<char> buffer(sizeof(header) + data.length());
    std::memcpy(buffer.data(), &header, sizeof(header));
    std::memcpy(buffer.data() + sizeof(header), data.c_str(), data.length());
    
    ssize_t sent_bytes = send(sock, buffer.data(), buffer.size(), 0);
    if(sent_bytes < 0) {
        std::cerr << "Failed to send package" << std::endl;
        return;
    }else{
        // 不要输出type值，根据枚举类型输出不同的信息
        std::cout << "[Client]: Sent packet Type=" << (type == MSG_LOGIN ? "LOGIN" : type == MSG_CHAT ? "CHAT" : type == MSG_FILE ? "FILE" : type == MSG_PROGRESS ? "PROGRESS" : "UNKNOWN")
                  << ", Length=" << header.length 
                  << ", Body='" << data << "'" << std::endl;

    }
}

int main() {
    int sock = 0;
    struct sockaddr_in server_addr;
    const int PORT = 8080;
    const std::string SERVER_IP = "127.0.0.1";  

    if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return -1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    
    if(inet_pton(AF_INET, SERVER_IP.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address/ Address not supported" << std::endl;
        return -1;
    }

    if(connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Connection failed" << std::endl;
        return -1;
    }

    std::cout << "Connected to server" << std::endl;
    
    send_package(sock, MSG_LOGIN, "Liang");
    sleep(1);
    send_package(sock, MSG_CHAT, "Hello, server!");
    sleep(1);
    close(sock);
    return 0;
}