#pragma once
#include "dds_cpp/api/DdsException.hpp"
namespace dds_cpp {
/// 초기화·발행·구독 등 DDS 작업 중의 오류다.
class DDSCPP_API DdsOperationException : public DdsException {
public:
    explicit DdsOperationException(const std::string& message) : DdsException(message) {}
};
}
