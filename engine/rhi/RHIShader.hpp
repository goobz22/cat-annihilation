#pragma once

#include "RHITypes.hpp"
#include <cstdint>

namespace CatEngine::RHI {

/**
 * Abstract interface for shader modules
 * Represents compiled shader code (SPIR-V bytecode)
 */
class IRHIShader {
public:
    virtual ~IRHIShader() = default;

    /**
     * Get shader stage
     */
    virtual ShaderStage GetStage() const = 0;

    /**
     * Get entry point name
     */
    virtual const char* GetEntryPoint() const = 0;

    /**
     * Get shader bytecode
     */
    virtual const uint8_t* GetCode() const = 0;

    /**
     * Get shader bytecode size in bytes
     */
    virtual uint64_t GetCodeSize() const = 0;

    /**
     * Get debug name
     */
    virtual const char* GetDebugName() const = 0;
};

} // namespace CatEngine::RHI
