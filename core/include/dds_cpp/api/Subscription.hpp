#pragma once
#include "dds_cpp/api/Visibility.hpp"
namespace dds_cpp {
/// 구독 하나의 수명 핸들이다. 반환된 unique_ptr를 유지하는 동안 수신하고 파괴 시 종료를 요청한다.
/// RTI 구현은 종료 대기 시간이 지나도 진행 중인 콜백이 사용하는 자원을 강제로 삭제하지 않는다.
class DDSCPP_API Subscription {
public:
    virtual ~Subscription() = default;
};
}
