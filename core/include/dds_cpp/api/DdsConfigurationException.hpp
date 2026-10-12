#pragma once
#include "dds_cpp/api/DdsException.hpp"
namespace dds_cpp {
/// 연결 전에 발견한 모듈·토픽·QoS 설정 오류다.
class DDSCPP_API DdsConfigurationException final : public DdsException {
public:
    explicit DdsConfigurationException(const std::string& message) : DdsException(message) {}
};
}
