//
// plume
//
// Copyright (c) 2024 renderbag and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file for details.
//

#pragma once

#include <cstdint>

namespace plume {
    struct RenderDevice;

    // Data layout of RenderShaderFormat::METAL_IR shaders: DXIL converted by Apple's Metal Shader
    // Converter with its automatic linear resource layout.
    //   MetalIRShaderHeader
    //   MetalIRResource[resourceCount]   (from IRShaderReflectionGetResourceLocations)
    //   entry point name                 (entryPointLength bytes, no terminator)
    //   metallib                         (metallibSize bytes)
    // Every resource is one IRDescriptorTableEntry at topLevelOffset in its stage's top-level
    // argument buffer. Register spaces map to descriptor set indices and slots to range bindings,
    // like the D3D12 backend; root descriptors and push constants resolve constant buffers first.
    enum class MetalIRResourceType : uint32_t {
        SRV = 0,
        UAV = 1,
        CBV = 2,
        SAMPLER = 3
    };

    struct MetalIRResource {
        MetalIRResourceType type = MetalIRResourceType::SRV;
        uint32_t space = 0;
        uint32_t slot = 0;
        uint32_t topLevelOffset = 0;
    };

    struct MetalIRShaderHeader {
        static constexpr uint32_t MAGIC = 0x52494C50; // "PLIR"
        static constexpr uint32_t VERSION = 1;

        uint32_t magic = MAGIC;
        uint32_t version = VERSION;
        uint32_t resourceCount = 0;
        uint32_t entryPointLength = 0;
        uint64_t metallibSize = 0;
    };

    // Descriptor sets created on a Metal device after enabling this keep D3D12-style ranges (register
    // classes may share binding numbers) for METAL_IR pipelines instead of Metal argument buffers, so
    // they cannot be used with METAL (MSL) pipelines.
    void SetMetalShaderConverterDescriptorSets(RenderDevice *device, bool enabled);
}
