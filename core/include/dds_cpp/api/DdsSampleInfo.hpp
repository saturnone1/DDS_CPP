#pragma once
#include <chrono>
#include <cstdint>
#include <optional>
#include "dds_cpp/api/Visibility.hpp"

namespace dds_cpp {
/// 샘플별 시각 정보다. 데이터와 달리 콜백 밖에 복사해 보관할 수 있다.
/// 단방향 지연 계산은 송수신 장비의 시계 동기화를 전제로 한다.
struct DdsSampleInfo {
    std::chrono::system_clock::time_point ReceivedAtUtc;
    std::optional<std::int64_t> SourceTimestampNs;
};
namespace detail {
// 모듈별 TLS가 아니라 Core의 단일 TLS를 사용한다. 중첩 수신에서도 이전 샘플 정보를 복원한다.
DDSCPP_API const DdsSampleInfo*& ActiveSampleInfo();
struct SampleInfoScope {
    const DdsSampleInfo* previous;
    explicit SampleInfoScope(const DdsSampleInfo& info) : previous(ActiveSampleInfo()) { ActiveSampleInfo() = &info; }
    ~SampleInfoScope() { ActiveSampleInfo() = previous; }
};
}
}
