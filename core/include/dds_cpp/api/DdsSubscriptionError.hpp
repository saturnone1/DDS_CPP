#pragma once
#include <string>
#include <exception>
#include <cstdint>
namespace dds_cpp {
/// 수신 실패를 로그와 독립적으로 전달하는 정보다. IsFatal은 수신 루프를 계속할 수 없는 오류인지를 나타낸다.
struct DdsSubscriptionError {
    std::string TopicName;
    std::exception_ptr Exception;
    bool IsFatal{};
    // 구독 식별자는 0이 아니며 해당 구독의 수명 동안 유지된다. 라이브러리 인스턴스 내에서 고유하다.
    std::uint64_t SubscriptionId{};
};
}
