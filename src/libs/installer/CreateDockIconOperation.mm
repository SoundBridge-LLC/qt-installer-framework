/**************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the Qt Installer Framework.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file. Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** As a special exception, The Qt Company gives you certain additional
** rights. These rights are described in The Qt Company LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
**
** $QT_END_LICENSE$
**
**************************************************************************/

#include "CreateDockIconOperation.h"
#include "packagemanagercore.h"
#include "utils.h"

#include <QFile>
#include <QDir>
#include <QProcess>
#include <QDebug>

#import <Cocoa/Cocoa.h>
#import <AppKit/AppKit.h>

using namespace QInstaller;

CreateDockIconOperation::CreateDockIconOperation()
{
    setName(QLatin1String("CreateDockIcon"));
}

void CreateDockIconOperation::backup()
{
}

bool CreateDockIconOperation::performOperation()
{
    const CFStringRef bundleId = arguments().at(0).toCFString();
    const CFStringRef appPath = arguments().at(1).toCFString();

    const CFStringRef persistentAppsKey = CFSTR("persistent-apps");
    const CFStringRef dockAppKey = CFSTR("com.apple.dock");
    const CFStringRef tileDataKey = CFSTR("tile-data");
    const CFStringRef bundleIdKey = CFSTR("bundle-identifier");

    BOOL existing = NO;
    CFArrayRef persistentApps = (CFArrayRef)CFPreferencesCopyAppValue(persistentAppsKey, dockAppKey);
    CFIndex count = CFArrayGetCount(persistentApps);
    for(CFIndex i = 0; i < count; i++)
    {
        CFDictionaryRef dict = (CFDictionaryRef)CFArrayGetValueAtIndex(persistentApps, i);
        if(!dict)
            continue;

        CFDictionaryRef tileData = (CFDictionaryRef)CFDictionaryGetValue(dict, tileDataKey);
        if(!tileData)
            continue;

        CFStringRef existingBundleId = (CFStringRef)CFDictionaryGetValue(tileData, bundleIdKey);
        if(!existingBundleId)
            continue;

        if(CFStringCompare(bundleId, existingBundleId, 0) == kCFCompareEqualTo)
        {
            existing = YES;
            break;
        }
    }

    if(!existing)
    {
        CFMutableDictionaryRef fileDataDict = CFDictionaryCreateMutable(NULL, 2, NULL, NULL);
        CFDictionarySetValue(fileDataDict, CFSTR("_CFURLString"), appPath);
        int type = 15; // TODO: will this value be changed by Apple?
        CFNumberRef stringType = CFNumberCreate(NULL, kCFNumberIntType, &type);
        CFDictionarySetValue(fileDataDict, CFSTR("_CFURLStringType"), stringType);

        CFMutableDictionaryRef tileDataDict = CFDictionaryCreateMutable(NULL, 2, NULL, NULL);
        CFDictionarySetValue(tileDataDict, CFSTR("file-data"), fileDataDict);
        CFDictionarySetValue(tileDataDict, bundleIdKey, bundleId);

        CFMutableDictionaryRef rootDict = CFDictionaryCreateMutable(NULL, 1, NULL, NULL);
        CFDictionarySetValue(rootDict, tileDataKey, tileDataDict);

        CFMutableArrayRef newApps = CFArrayCreateMutableCopy(NULL, 0, persistentApps);
        CFArrayAppendValue(newApps, rootDict);
        CFPreferencesSetAppValue(persistentAppsKey, newApps, dockAppKey);
        CFPreferencesAppSynchronize(dockAppKey);

        CFRelease(newApps);
        CFRelease(rootDict);
        CFRelease(tileDataDict);
        CFRelease(stringType);
        CFRelease(fileDataDict);

        //
        restartDock();
    }

    CFRelease(persistentApps);

    return true;
}

bool CreateDockIconOperation::undoOperation()
{
    const CFStringRef bundleId = arguments().at(0).toCFString();

    const CFStringRef persistentAppsKey = CFSTR("persistent-apps");
    const CFStringRef dockAppKey = CFSTR("com.apple.dock");
    const CFStringRef tileDataKey = CFSTR("tile-data");
    const CFStringRef bundleIdKey = CFSTR("bundle-identifier");

    CFIndex existingIndex = -1;
    CFArrayRef persistentApps = (CFArrayRef)CFPreferencesCopyAppValue(persistentAppsKey, dockAppKey);
    CFIndex count = CFArrayGetCount(persistentApps);
    for(CFIndex i = 0; i < count; i++)
    {
        CFDictionaryRef dict = (CFDictionaryRef)CFArrayGetValueAtIndex(persistentApps, i);
        if(!dict)
            continue;

        CFDictionaryRef tileData = (CFDictionaryRef)CFDictionaryGetValue(dict, tileDataKey);
        if(!tileData)
            continue;

        CFStringRef existingBundleId = (CFStringRef)CFDictionaryGetValue(tileData, bundleIdKey);
        if(!existingBundleId)
            continue;

        if(CFStringCompare(bundleId, existingBundleId, 0) == kCFCompareEqualTo)
        {
            existingIndex = i;
            break;
        }
    }

    if(existingIndex >= 0)
    {
        CFMutableArrayRef newApps = CFArrayCreateMutableCopy(NULL, 0, persistentApps);
        CFArrayRemoveValueAtIndex(newApps, existingIndex);
        CFPreferencesSetAppValue(persistentAppsKey, newApps, dockAppKey);
        CFPreferencesAppSynchronize(dockAppKey);
        CFRelease(newApps);

        //
        restartDock();
    }

    CFRelease(persistentApps);

    return true;
}

bool CreateDockIconOperation::testOperation()
{
    return true;
}

Operation *CreateDockIconOperation::clone() const
{
    return new CreateDockIconOperation();
}

void CreateDockIconOperation::restartDock()
{
    NSArray *runningApps = [NSRunningApplication runningApplicationsWithBundleIdentifier:@"com.apple.dock"];
    if([runningApps count] > 0)
    {
        NSRunningApplication *dock = [runningApps lastObject];
        [dock terminate];
    }
}
