//
// plume
//
// Copyright (c) 2024 renderbag and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file for details.
//

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include "plume_render_interface_types.h"

namespace plume {
    RenderDeviceVendor getRenderDeviceVendor(uint64_t registryID);

    struct CocoaWindowAttributes {
        int x, y;
        int width, height;
    };

    class CocoaWindow {
    public:
        // Updates queued to the main thread capture this state instead of `this`,
        // so they stay valid if the wrapper is destroyed before they run.
        struct SharedState {
            void* windowHandle = nullptr;
            CocoaWindowAttributes cachedAttributes = {0, 0, 0, 0};
            std::atomic<int> cachedRefreshRate{0};
            std::mutex attributesMutex;
        };

    private:
        std::shared_ptr<SharedState> state;

        void updateWindowAttributesInternal(bool forceSync = false) const;
        void updateRefreshRateInternal(bool forceSync = false) const;
    public:
        CocoaWindow(void* window);
        ~CocoaWindow();

        // Get cached window attributes, may trigger async update
        void getWindowAttributes(CocoaWindowAttributes* attributes) const;

        // Get cached refresh rate, may trigger async update
        int getRefreshRate() const;

        // Toggle fullscreen
        void toggleFullscreen();
    };
}
