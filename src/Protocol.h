#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>

#pragma pack(push, 1)

struct Header {
    uint32_t length;
    uint8_t type;
};

enum MessageType {
    MSG_LOGIN = 1,
    MSG_CHAT = 2,
    MSG_FILE = 3,        // 文件元信息（文件名、大小）
    MSG_FILE_DATA = 4,   // 文件数据块
    MSG_PROGRESS = 5,    // 进度更新
};

struct LoginMsg {
    uint32_t username_len;
    char username[20];
};

struct ChatMsg {
    uint32_t sender_len;
    char sender[20];
    uint32_t content_len;
    char content[1024];
};

struct FileMsg {
    uint32_t sender_len;
    char sender[20];
    uint32_t filename_len;
    char filename[100];
    uint64_t file_size;
};

// 文件数据块消息
struct FileDataMsg {
    uint32_t sender_len;
    char sender[20];
    uint32_t filename_len;
    char filename[100];
    uint64_t offset;      // 当前数据块在文件中的偏移量
    uint32_t data_len;    // 本次传输的数据长度
    // 后面跟随实际的数据（不在结构体中，动态分配）
};

// 文件传输进度后加的，方便查看文件传输进度
struct ProgressMsg { 
    uint32_t sender_len;
    char sender[20];
    uint64_t total_size;
    uint64_t received_size;
};

#pragma pack(pop)

#endif // PROTOCOL_H