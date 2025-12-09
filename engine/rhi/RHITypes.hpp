#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace CatEngine::RHI {

// ============================================================================
// Enumerations
// ============================================================================

/**
 * Buffer usage flags - can be combined with bitwise OR
 */
enum class BufferUsage : uint32_t {
    None = 0,
    Vertex = 1 << 0,
    Index = 1 << 1,
    Uniform = 1 << 2,
    Storage = 1 << 3,
    Staging = 1 << 4,
    TransferSrc = 1 << 5,
    TransferDst = 1 << 6,
    Indirect = 1 << 7
};

inline BufferUsage operator|(BufferUsage a, BufferUsage b) {
    return static_cast<BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline BufferUsage operator&(BufferUsage a, BufferUsage b) {
    return static_cast<BufferUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

/**
 * Memory property flags for buffers
 */
enum class MemoryProperty : uint32_t {
    DeviceLocal = 1 << 0,     // GPU memory
    HostVisible = 1 << 1,     // CPU accessible
    HostCoherent = 1 << 2,    // No manual flush needed
    HostCached = 1 << 3,      // Cached on CPU
    LazilyAllocated = 1 << 4  // Lazily allocated
};

inline MemoryProperty operator|(MemoryProperty a, MemoryProperty b) {
    return static_cast<MemoryProperty>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline MemoryProperty operator&(MemoryProperty a, MemoryProperty b) {
    return static_cast<MemoryProperty>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool operator!=(MemoryProperty a, uint32_t b) {
    return static_cast<uint32_t>(a) != b;
}

/**
 * Texture formats
 */
enum class TextureFormat {
    Undefined,

    // 8-bit formats
    R8_UNORM,
    R8_SNORM,
    R8_UINT,
    R8_SINT,

    // 16-bit formats
    RG8_UNORM,
    RG8_SNORM,
    RG8_UINT,
    RG8_SINT,

    R16_UNORM,
    R16_SNORM,
    R16_UINT,
    R16_SINT,
    R16_SFLOAT,

    // 32-bit formats
    RGBA8_UNORM,
    RGBA8_SRGB,
    RGBA8_SNORM,
    RGBA8_UINT,
    RGBA8_SINT,

    BGRA8_UNORM,
    BGRA8_SRGB,

    RG16_UNORM,
    RG16_SNORM,
    RG16_UINT,
    RG16_SINT,
    RG16_SFLOAT,

    R32_UINT,
    R32_SINT,
    R32_SFLOAT,

    // 64-bit formats
    RGBA16_UNORM,
    RGBA16_SNORM,
    RGBA16_UINT,
    RGBA16_SINT,
    RGBA16_SFLOAT,

    RG32_UINT,
    RG32_SINT,
    RG32_SFLOAT,

    // 96-bit formats (3 components)
    RGB32_UINT,
    RGB32_SINT,
    RGB32_SFLOAT,

    // 128-bit formats
    RGBA32_UINT,
    RGBA32_SINT,
    RGBA32_SFLOAT,

    // Depth/Stencil formats
    D16_UNORM,
    D32_SFLOAT,
    D24_UNORM_S8_UINT,
    D32_SFLOAT_S8_UINT,
    S8_UINT,

    // Compressed formats
    BC1_RGB_UNORM,
    BC1_RGB_SRGB,
    BC1_RGBA_UNORM,
    BC1_RGBA_SRGB,
    BC3_UNORM,
    BC3_SRGB,
    BC4_UNORM,
    BC4_SNORM,
    BC5_UNORM,
    BC5_SNORM,
    BC6H_UFLOAT,
    BC6H_SFLOAT,
    BC7_UNORM,
    BC7_SRGB
};

/**
 * Texture usage flags - can be combined
 */
enum class TextureUsage : uint32_t {
    None = 0,
    Sampled = 1 << 0,          // Can be sampled in shaders
    Storage = 1 << 1,          // Can be used as storage image
    RenderTarget = 1 << 2,     // Can be used as color attachment
    DepthStencil = 1 << 3,     // Can be used as depth/stencil attachment
    TransferSrc = 1 << 4,      // Can be source of transfer
    TransferDst = 1 << 5,      // Can be destination of transfer
    Transient = 1 << 6         // Transient attachment
};

inline TextureUsage operator|(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline TextureUsage operator&(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

/**
 * Texture types
 */
enum class TextureType {
    Texture1D,
    Texture2D,
    Texture3D,
    TextureCube,
    Texture1DArray,
    Texture2DArray,
    TextureCubeArray
};

/**
 * Shader stage flags
 */
enum class ShaderStage : uint32_t {
    None = 0,
    Vertex = 1 << 0,
    TessControl = 1 << 1,
    TessEval = 1 << 2,
    Geometry = 1 << 3,
    Fragment = 1 << 4,
    Compute = 1 << 5,
    AllGraphics = Vertex | TessControl | TessEval | Geometry | Fragment,
    All = AllGraphics | Compute
};

inline ShaderStage operator|(ShaderStage a, ShaderStage b) {
    return static_cast<ShaderStage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline ShaderStage operator&(ShaderStage a, ShaderStage b) {
    return static_cast<ShaderStage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

/**
 * Primitive topology
 */
enum class PrimitiveType {
    Points,
    Lines,
    LineStrip,
    Triangles,
    TriangleStrip,
    TriangleFan,
    LinesWithAdjacency,
    LineStripWithAdjacency,
    TrianglesWithAdjacency,
    TriangleStripWithAdjacency,
    PatchList
};

/**
 * Polygon cull mode
 */
enum class CullMode {
    None,
    Front,
    Back,
    FrontAndBack
};

/**
 * Front face winding order
 */
enum class FrontFace {
    CounterClockwise,
    Clockwise
};

/**
 * Blend factors
 */
enum class BlendFactor {
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    DstColor,
    OneMinusDstColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha,
    ConstantColor,
    OneMinusConstantColor,
    ConstantAlpha,
    OneMinusConstantAlpha,
    SrcAlphaSaturate
};

/**
 * Blend operations
 */
enum class BlendOp {
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max
};

/**
 * Comparison operations
 */
enum class CompareOp {
    Never,
    Less,
    Equal,
    LessOrEqual,
    Greater,
    NotEqual,
    GreaterOrEqual,
    Always
};

/**
 * Texture filtering modes
 */
enum class Filter {
    Nearest,
    Linear
};

/**
 * Mipmap filtering modes
 */
enum class MipmapMode {
    Nearest,
    Linear
};

/**
 * Texture address modes
 */
enum class AddressMode {
    Repeat,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder,
    MirrorClampToEdge
};

/**
 * Border color for textures
 */
enum class BorderColor {
    TransparentBlack,
    OpaqueBlack,
    OpaqueWhite
};

/**
 * Load operation for render pass attachments
 */
enum class LoadOp {
    Load,      // Preserve existing contents
    Clear,     // Clear to a value
    DontCare   // Don't care about existing contents
};

/**
 * Store operation for render pass attachments
 */
enum class StoreOp {
    Store,     // Store the results
    DontCare   // Don't care about storing
};

/**
 * Pipeline bind point
 */
enum class PipelineBindPoint {
    Graphics,
    Compute
};

/**
 * Index type
 */
enum class IndexType {
    UInt16,
    UInt32
};

/**
 * Vertex input rate
 */
enum class VertexInputRate {
    Vertex,    // Per-vertex
    Instance   // Per-instance
};

/**
 * Descriptor types
 */
enum class DescriptorType {
    Sampler,
    CombinedImageSampler,
    SampledImage,
    StorageImage,
    UniformTexelBuffer,
    StorageTexelBuffer,
    UniformBuffer,
    StorageBuffer,
    UniformBufferDynamic,
    StorageBufferDynamic,
    InputAttachment
};

// ============================================================================
// Structures
// ============================================================================

/**
 * Buffer creation descriptor
 */
struct BufferDesc {
    uint64_t size = 0;
    BufferUsage usage = BufferUsage::None;
    MemoryProperty memoryProperties = MemoryProperty::DeviceLocal;
    const char* debugName = nullptr;
};

/**
 * Texture creation descriptor
 */
struct TextureDesc {
    TextureType type = TextureType::Texture2D;
    TextureFormat format = TextureFormat::RGBA8_UNORM;
    TextureUsage usage = TextureUsage::Sampled;
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depth = 1;
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    uint32_t sampleCount = 1;
    const char* debugName = nullptr;
};

/**
 * Sampler creation descriptor
 */
struct SamplerDesc {
    Filter magFilter = Filter::Linear;
    Filter minFilter = Filter::Linear;
    MipmapMode mipmapMode = MipmapMode::Linear;
    AddressMode addressModeU = AddressMode::Repeat;
    AddressMode addressModeV = AddressMode::Repeat;
    AddressMode addressModeW = AddressMode::Repeat;
    float mipLodBias = 0.0f;
    bool anisotropyEnable = false;
    float maxAnisotropy = 1.0f;
    bool compareEnable = false;
    CompareOp compareOp = CompareOp::Never;
    float minLod = 0.0f;
    float maxLod = 1000.0f;
    BorderColor borderColor = BorderColor::OpaqueBlack;
    bool unnormalizedCoordinates = false;
};

/**
 * Shader creation descriptor
 */
struct ShaderDesc {
    ShaderStage stage = ShaderStage::Vertex;
    const uint8_t* code = nullptr;  // SPIR-V bytecode
    uint64_t codeSize = 0;
    const char* entryPoint = "main";
    const char* debugName = nullptr;
};

/**
 * Vertex input attribute description
 */
struct VertexAttribute {
    uint32_t location = 0;
    uint32_t binding = 0;
    TextureFormat format = TextureFormat::RGBA32_SFLOAT;
    uint32_t offset = 0;
};

/**
 * Vertex input binding description
 */
struct VertexBinding {
    uint32_t binding = 0;
    uint32_t stride = 0;
    VertexInputRate inputRate = VertexInputRate::Vertex;
};

/**
 * Vertex input state
 */
struct VertexInputState {
    std::vector<VertexBinding> bindings;
    std::vector<VertexAttribute> attributes;
};

/**
 * Rasterization state
 */
struct RasterizationState {
    bool depthClampEnable = false;
    bool rasterizerDiscardEnable = false;
    CullMode cullMode = CullMode::Back;
    FrontFace frontFace = FrontFace::CounterClockwise;
    bool depthBiasEnable = false;
    float depthBiasConstantFactor = 0.0f;
    float depthBiasClamp = 0.0f;
    float depthBiasSlopeFactor = 0.0f;
    float lineWidth = 1.0f;
};

/**
 * Blend attachment state
 */
struct BlendAttachmentState {
    bool blendEnable = false;
    BlendFactor srcColorBlendFactor = BlendFactor::One;
    BlendFactor dstColorBlendFactor = BlendFactor::Zero;
    BlendOp colorBlendOp = BlendOp::Add;
    BlendFactor srcAlphaBlendFactor = BlendFactor::One;
    BlendFactor dstAlphaBlendFactor = BlendFactor::Zero;
    BlendOp alphaBlendOp = BlendOp::Add;
    uint8_t colorWriteMask = 0xF; // RGBA
};

/**
 * Depth stencil state
 */
struct DepthStencilState {
    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    CompareOp depthCompareOp = CompareOp::Less;
    bool depthBoundsTestEnable = false;
    bool stencilTestEnable = false;
    float minDepthBounds = 0.0f;
    float maxDepthBounds = 1.0f;
};

/**
 * Viewport
 */
struct Viewport {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

/**
 * Scissor rectangle
 */
struct Rect2D {
    int32_t x = 0;
    int32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

/**
 * Pipeline creation descriptor
 */
struct PipelineDesc {
    // Shader stages (interfaces will be defined in RHIShader.hpp)
    std::vector<class IRHIShader*> shaders;

    // Vertex input
    VertexInputState vertexInput;

    // Input assembly
    PrimitiveType primitiveType = PrimitiveType::Triangles;
    bool primitiveRestartEnable = false;

    // Rasterization
    RasterizationState rasterization;

    // Depth/Stencil
    DepthStencilState depthStencil;

    // Blending
    std::vector<BlendAttachmentState> blendAttachments;

    // Render pass (interface will be defined in RHIRenderPass.hpp)
    class IRHIRenderPass* renderPass = nullptr;
    uint32_t subpass = 0;

    const char* debugName = nullptr;
};

/**
 * Compute pipeline descriptor
 */
struct ComputePipelineDesc {
    class IRHIShader* shader = nullptr;
    const char* debugName = nullptr;
};

/**
 * Render pass attachment description
 */
struct AttachmentDesc {
    TextureFormat format = TextureFormat::Undefined;
    uint32_t sampleCount = 1;
    LoadOp loadOp = LoadOp::Clear;
    StoreOp storeOp = StoreOp::Store;
    LoadOp stencilLoadOp = LoadOp::DontCare;
    StoreOp stencilStoreOp = StoreOp::DontCare;
};

/**
 * Attachment reference
 */
struct AttachmentReference {
    uint32_t attachmentIndex = 0;
};

/**
 * Subpass description
 */
struct SubpassDesc {
    PipelineBindPoint bindPoint = PipelineBindPoint::Graphics;
    std::vector<AttachmentReference> inputAttachments;
    std::vector<AttachmentReference> colorAttachments;
    AttachmentReference* depthStencilAttachment = nullptr;
};

/**
 * Render pass creation descriptor
 */
struct RenderPassDesc {
    std::vector<AttachmentDesc> attachments;
    std::vector<SubpassDesc> subpasses;
    const char* debugName = nullptr;
};

/**
 * Clear color value
 */
struct ClearColorValue {
    union {
        float float32[4];
        int32_t int32[4];
        uint32_t uint32[4];
    };

    ClearColorValue() : float32{0.0f, 0.0f, 0.0f, 1.0f} {}
    ClearColorValue(float r, float g, float b, float a) : float32{r, g, b, a} {}
};

/**
 * Clear depth stencil value
 */
struct ClearDepthStencilValue {
    float depth = 1.0f;
    uint32_t stencil = 0;
};

/**
 * Clear value
 */
struct ClearValue {
    union {
        ClearColorValue color;
        ClearDepthStencilValue depthStencil;
    };

    ClearValue() : color() {}
    ClearValue(float r, float g, float b, float a) : color(r, g, b, a) {}
};

/**
 * Descriptor set layout binding
 */
struct DescriptorBinding {
    uint32_t binding = 0;
    DescriptorType descriptorType = DescriptorType::UniformBuffer;
    uint32_t descriptorCount = 1;
    ShaderStage stageFlags = ShaderStage::All;
};

/**
 * Descriptor set layout descriptor
 */
struct DescriptorSetLayoutDesc {
    std::vector<DescriptorBinding> bindings;
    const char* debugName = nullptr;
};

/**
 * Swapchain creation descriptor
 */
struct SwapchainDesc {
    void* windowHandle = nullptr;      // Platform-specific window handle
    uint32_t width = 0;
    uint32_t height = 0;
    TextureFormat format = TextureFormat::BGRA8_SRGB;
    uint32_t imageCount = 3;           // Triple buffering by default
    bool vsync = true;
    const char* debugName = nullptr;
};

} // namespace CatEngine::RHI
