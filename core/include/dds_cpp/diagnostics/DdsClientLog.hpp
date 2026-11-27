#pragma once
#include "dds_cpp/api/TopicDefinition.hpp"
#include "dds_cpp/api/DdsLogLevel.hpp"
#include <iostream>
namespace dds_cpp::detail {
/// 출력·문자열 변환 실패를 통신과 분리한다. 수준을 확인한 뒤에만 샘플 문자열을 만든다.
class DdsClientLog final {
public:
    template<class T> static void Sample(const char* direction, DdsLogLevel level, const TopicDefinition& topic, const T& sample) noexcept
    {
        if (level != DdsLogLevel::Debug) return;
        try {
            std::clog << direction << " 토픽=" << topic.Name();
            if constexpr (requires(std::ostream& stream, const T& value) { stream << value; }) std::clog << '\n' << sample;
            std::clog << '\n';
        } catch (...) { /* 로그 오류는 송수신을 중단하지 않는다. */ }
    }
    static void Error(DdsLogLevel level, const char* message, const char* detail = "") noexcept
    {
        if (level == DdsLogLevel::None) return;
        try { std::cerr << message << detail << '\n'; }
        catch (...) { /* 수신 스레드에서 출력 실패로 프로세스를 종료하지 않는다. */ }
    }
};
}
