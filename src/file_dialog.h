#ifndef FILE_DIALOG_H
#define FILE_DIALOG_H

#include <string>

// 打开文件选择对话框，返回选中的文件路径
// 如果用户取消，返回空字符串
std::string open_file_dialog();

#endif // FILE_DIALOG_H
