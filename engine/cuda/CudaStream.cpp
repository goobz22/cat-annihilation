#include "CudaStream.hpp"

namespace CatEngine {
namespace CUDA {

CudaStream::CudaStream()
    : m_stream(nullptr)
    , m_isDefault(true)
    , m_ownsStream(false)
{
}

CudaStream::CudaStream(Flags flags)
    : m_stream(nullptr)
    , m_isDefault(false)
    , m_ownsStream(true)
{
    CUDA_CHECK(cudaStreamCreateWithFlags(&m_stream, static_cast<unsigned int>(flags)));
}

CudaStream::CudaStream(Priority priority, Flags flags)
    : m_stream(nullptr)
    , m_isDefault(false)
    , m_ownsStream(true)
{
    int leastPriority, greatestPriority;
    getPriorityRange(leastPriority, greatestPriority);

    int priorityValue;
    switch (priority) {
        case Priority::High:
            priorityValue = greatestPriority;
            break;
        case Priority::Low:
            priorityValue = leastPriority;
            break;
        case Priority::Normal:
        default:
            priorityValue = (leastPriority + greatestPriority) / 2;
            break;
    }

    CUDA_CHECK(cudaStreamCreateWithPriority(&m_stream, static_cast<unsigned int>(flags), priorityValue));
}

CudaStream::~CudaStream() {
    destroy();
}

CudaStream::CudaStream(CudaStream&& other) noexcept
    : m_stream(other.m_stream)
    , m_isDefault(other.m_isDefault)
    , m_ownsStream(other.m_ownsStream)
{
    other.m_stream = nullptr;
    other.m_ownsStream = false;
}

CudaStream& CudaStream::operator=(CudaStream&& other) noexcept {
    if (this != &other) {
        destroy();
        m_stream = other.m_stream;
        m_isDefault = other.m_isDefault;
        m_ownsStream = other.m_ownsStream;
        other.m_stream = nullptr;
        other.m_ownsStream = false;
    }
    return *this;
}

void CudaStream::destroy() {
    if (m_ownsStream && m_stream != nullptr) {
        // Don't throw in destructor path
        cudaStreamDestroy(m_stream);
        m_stream = nullptr;
        m_ownsStream = false;
    }
}

void CudaStream::synchronize() const {
    CUDA_CHECK(cudaStreamSynchronize(m_stream));
}

bool CudaStream::query() const {
    cudaError_t error = cudaStreamQuery(m_stream);
    if (error == cudaSuccess) {
        return true;
    } else if (error == cudaErrorNotReady) {
        return false;
    } else {
        CUDA_CHECK(error);
        return false; // Unreachable
    }
}

void CudaStream::waitEvent(cudaEvent_t event) const {
    CUDA_CHECK(cudaStreamWaitEvent(m_stream, event, 0));
}

int CudaStream::getPriority() const {
    int priority;
    CUDA_CHECK(cudaStreamGetPriority(m_stream, &priority));
    return priority;
}

unsigned int CudaStream::getFlags() const {
    unsigned int flags;
    CUDA_CHECK(cudaStreamGetFlags(m_stream, &flags));
    return flags;
}

CudaStream CudaStream::getDefaultStream() {
    return CudaStream();
}

void CudaStream::getPriorityRange(int& leastPriority, int& greatestPriority) {
    CUDA_CHECK(cudaDeviceGetStreamPriorityRange(&leastPriority, &greatestPriority));
}

} // namespace CUDA
} // namespace CatEngine
