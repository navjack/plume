//
// plume
//
// Copyright (c) 2024 renderbag and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file for details.
//

#include "plume_apple.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <IOKit/IOKitLib.h>

static uint32_t plumeGetEntryProperty(io_registry_entry_t entry, CFStringRef propertyName) {
    uint32_t value = 0;
    CFTypeRef cfProp = IORegistryEntrySearchCFProperty(entry, kIOServicePlane, propertyName, kCFAllocatorDefault, kIORegistryIterateRecursively | kIORegistryIterateParents);

    if (cfProp) {
        if (CFGetTypeID(cfProp) == CFDataGetTypeID()) {
            const uint32_t* pValue = reinterpret_cast<const uint32_t*>(CFDataGetBytePtr((CFDataRef)cfProp));
            if (pValue) {
                value = *pValue;
            }
        }
        CFRelease(cfProp);
    }

    return value;
}

namespace plume {
    RenderDeviceVendor getRenderDeviceVendor(uint64_t registryID) {
        io_service_t entry = IOServiceGetMatchingService(MACH_PORT_NULL, IORegistryEntryIDMatching(registryID));

        if (entry) {
            io_registry_entry_t parent;
            if (IORegistryEntryGetParentEntry(entry, kIOServicePlane, &parent) == kIOReturnSuccess) {
                uint32_t vendorId = plumeGetEntryProperty(parent, CFSTR("vendor-id"));
                IOObjectRelease(parent); // Release the parent
                IOObjectRelease(entry); // Release the entry
                return RenderDeviceVendor(vendorId);
            }
            IOObjectRelease(entry); // Release the entry if we couldn't get parent
        }

        return RenderDeviceVendor::UNKNOWN;
    }

    CGFloat getScaleFactor(NSWindow *nsWindow) {
#ifdef PLUME_APPLE_RETINA_ENABLED
        return [nsWindow backingScaleFactor];
#else
        return 1.0f;
#endif
    }

    // MARK: - CocoaWindow

    // Main thread only. Asynchronous callers pass shared state and an NSWindow
    // captured by the block, never `this`: a swapchain can destroy its wrapper
    // before a queued update runs.
    static void updateAttributes(CocoaWindow::SharedState &state, NSWindow *nsWindow) {
        NSRect contentFrame = [[nsWindow contentView] frame];
        CGFloat scaleFactor = getScaleFactor(nsWindow);

        std::lock_guard<std::mutex> lock(state.attributesMutex);
        state.cachedAttributes.x = (int)round(contentFrame.origin.x);
        state.cachedAttributes.y = (int)round(contentFrame.origin.y);
        state.cachedAttributes.width = (int)round(contentFrame.size.width * scaleFactor);
        state.cachedAttributes.height = (int)round(contentFrame.size.height * scaleFactor);
    }

    static void updateRefreshRate(CocoaWindow::SharedState &state, NSWindow *nsWindow) {
        NSScreen *screen = [nsWindow screen];
        if (@available(macOS 12.0, *)) {
            state.cachedRefreshRate.store((int)[screen maximumFramesPerSecond]);
        }
    }

    CocoaWindow::CocoaWindow(void* window)
        : state(std::make_shared<SharedState>()) {
        state->windowHandle = window;

        if ([NSThread isMainThread]) {
            NSWindow *nsWindow = (__bridge NSWindow *)window;
            updateAttributes(*state, nsWindow);
            updateRefreshRate(*state, nsWindow);
        } else {
            updateWindowAttributesInternal(true);
            updateRefreshRateInternal(true);
        }
    }

    CocoaWindow::~CocoaWindow() {}

    void CocoaWindow::updateWindowAttributesInternal(bool forceSync) const {
        std::shared_ptr<SharedState> sharedState = state;
        NSWindow *nsWindow = (__bridge NSWindow *)sharedState->windowHandle;
        auto updateBlock = ^{
            updateAttributes(*sharedState, nsWindow);
        };

        if (forceSync) {
            dispatch_sync(dispatch_get_main_queue(), updateBlock);
        } else {
            dispatch_async(dispatch_get_main_queue(), updateBlock);
        }
    }

    void CocoaWindow::updateRefreshRateInternal(bool forceSync) const {
        std::shared_ptr<SharedState> sharedState = state;
        NSWindow *nsWindow = (__bridge NSWindow *)sharedState->windowHandle;
        auto updateBlock = ^{
            updateRefreshRate(*sharedState, nsWindow);
        };

        if (forceSync) {
            dispatch_sync(dispatch_get_main_queue(), updateBlock);
        } else {
            dispatch_async(dispatch_get_main_queue(), updateBlock);
        }
    }

    void CocoaWindow::getWindowAttributes(CocoaWindowAttributes* attributes) const {
        if ([NSThread isMainThread]) {
            updateAttributes(*state, (__bridge NSWindow *)state->windowHandle);

            std::lock_guard<std::mutex> lock(state->attributesMutex);
            *attributes = state->cachedAttributes;
        } else {
            {
                std::lock_guard<std::mutex> lock(state->attributesMutex);
                *attributes = state->cachedAttributes;
            }

            updateWindowAttributesInternal(false);
        }
    }

    int CocoaWindow::getRefreshRate() const {
        if ([NSThread isMainThread]) {
            NSWindow *nsWindow = (__bridge NSWindow *)state->windowHandle;
            NSScreen *screen = [nsWindow screen];

            if (@available(macOS 12.0, *)) {
                int freshRate = (int)[screen maximumFramesPerSecond];
                state->cachedRefreshRate.store(freshRate);
                return freshRate;
            }

            return state->cachedRefreshRate.load();
        } else {
            int rate = state->cachedRefreshRate.load();

            updateRefreshRateInternal(false);

            return rate;
        }
    }

    void CocoaWindow::toggleFullscreen() {
        NSWindow *nsWindow = (__bridge NSWindow *)state->windowHandle;
        if ([NSThread isMainThread]) {
            [nsWindow toggleFullScreen:NULL];
        } else {
            dispatch_async(dispatch_get_main_queue(), ^{
                [nsWindow toggleFullScreen:NULL];
            });
        }
    }
}
