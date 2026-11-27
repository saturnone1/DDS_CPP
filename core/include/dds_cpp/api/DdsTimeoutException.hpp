#pragma once
#include "dds_cpp/api/DdsOperationException.hpp"
namespace dds_cpp {
/// DDS의 명시적 대기 시간 초과다. 기존 DdsOperationException 처리와 호환된다.
class DDSCPP_API DdsTimeoutException final : public DdsOperationException {
public:
    explicit DdsTimeoutException(const std::string& message) : DdsOperationException(message) {}
};
}
