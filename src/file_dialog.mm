#include "file_dialog.h"
#import <Cocoa/Cocoa.h>

std::string open_file_dialog() {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:NO];
        [panel setTitle:@"Select File to Send"];
        
        NSModalResponse response = [panel runModal];
        
        if (response == NSModalResponseOK) {
            NSURL* url = [[panel URLs] objectAtIndex:0];
            NSString* path = [url path];
            return std::string([path UTF8String]);
        }
    }
    
    return ""; // 用户取消选择
}
