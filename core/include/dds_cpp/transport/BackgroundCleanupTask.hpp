#pragma once
#include <thread>
#include <utility>
#include <atomic>
#include <memory>
#include <optional>
#include "dds_cpp/api/DdsConfigurationException.hpp"
#include "dds_cpp/api/DdsOperationException.hpp"

namespace dds_cpp::detail {
// 실패 주입용 내부 플래그다. 테스트가 아닌 일반 호출에서는 항상 false다.
DDSCPP_API bool ConsumeBackgroundStartFailure() noexcept;
struct RetiredThread {
    std::thread Worker;
    std::shared_ptr<std::atomic_bool> Finished = std::make_shared<std::atomic_bool>(false);
    RetiredThread* Next{};
};
// 실패한 분리의 핸들은 Core의 비동기 회수 소유자에게 넘긴다.
DDSCPP_API void RetainThreadForReaping(RetiredThread* thread) noexcept;
DDSCPP_API void KickThreadReaper() noexcept;
/// 네이티브 정리를 호출자 스레드에서 실행하지 않도록 전용 작업자를 시작한다.
/// 실제 완료·실패 상태는 연결의 ClientStatus에 보존한다. C#과 달리 소유권은 shared_ptr 캡처로 유지한다.
class BackgroundCleanupTask final {
public:
    template<class Action> static void Start(Action action, bool* started = nullptr)
    {
        KickThreadReaper();
        if (started) *started = false;
        if (ConsumeBackgroundStartFailure()) {
            throw DdsOperationException("테스트용 백그라운드 스레드 시작 실패입니다.");
        }
        auto record = std::make_unique<RetiredThread>();
        const auto finished = record->Finished;
        record->Worker = std::thread([task = std::optional<Action>(std::move(action)), finished]() mutable {
            try { (*task)(); }
            catch (...) { /* 호출 경로의 상태 소유자가 실패를 기록한다. 스레드 경계를 넘어 던지지 않는다. */ }
            task.reset();
            finished->store(true, std::memory_order_release);
        });
        if (started) *started = true;
        try { record->Worker.detach(); }
        catch (...) { RetainThreadForReaping(record.release()); throw; }
    }
};
}
