#include "MacUtils.h"
#include <QFile>
#include <QTextStream>

#import <AppKit/AppKit.h>

void setIconForFile(const std::string &iconImageName, const std::string &path)
{
    NSString *imagePath = [NSString stringWithUTF8String:iconImageName.c_str()];
    NSImage *image = [[NSImage alloc] initWithContentsOfFile:imagePath];
    if(image)
    {
        [[NSWorkspace sharedWorkspace] setIcon:image forFile:[NSString stringWithUTF8String:path.c_str()] options:0];
        [image release];
    }
}
