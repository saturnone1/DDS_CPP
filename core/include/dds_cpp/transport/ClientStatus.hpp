#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <map>
#include <vector>
#include <atomic>
#include "dds_cpp/api/Visibility.hpp"

#include "dds_cpp/api/DdsShutdownState.hpp"
#include "dds_cpp/api/DdsSubscriptionError.hpp"
namespace dds_cpp {
namespace detail {
class ClientStatus;
DDSCPP_API const ClientStatus*& ActiveClientCallback();
// 공개 클라이언트와 백그라운드 정리·수신 스레드가 공유하는 상태다. 연결 핸들이 사라져도 조회·완료 통지가 가능하다.
class ClientStatus {
public:
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::atomic<DdsShutdownState> state{DdsShutdownState::Open};
    std::exception_ptr failure;
    std::optional<DdsSubscriptionError> last_error;
    using ErrorHandler = std::function<void(const DdsSubscriptionError&)>;
    using Observer = std::shared_ptr<const ErrorHandler>;
    Observer observer;
    // 이전 단일 관찰자 API와 독립적으로 추가 관찰자·구독별 관찰자를 보관한다.
    std::map<std::uint64_t, Observer> observers;
    std::map<std::uint64_t, Observer> subscription_observers;
    std::uint64_t next_observer_id{1};

    void closing() { std::lock_guard lock(mutex); if (state == DdsShutdownState::Open) state = DdsShutdownState::Closing; }
    void complete() {
        { std::lock_guard lock(mutex); if (state != DdsShutdownState::Faulted) state = DdsShutdownState::Closed; }
        changed.notify_all();
    }
    void fault(std::exception_ptr error) {
        { std::lock_guard lock(mutex); failure = error; state = DdsShutdownState::Faulted; }
        changed.notify_all();
    }
    bool wait(std::chrono::milliseconds timeout) {
        // 아직 종료하지 않은 연결과 현재 콜백의 자기 대기는 완료 조건이 아니므로 즉시 false로 반환한다.
        std::unique_lock lock(mutex);
        if (state == DdsShutdownState::Open || ActiveClientCallback() == this) return false;
        if (!changed.wait_for(lock, timeout, [&] { return state == DdsShutdownState::Closed || state == DdsShutdownState::Faulted; })) return false;
        if (failure) std::rethrow_exception(failure);
        return state == DdsShutdownState::Closed;
    }
    void report(DdsSubscriptionError error) noexcept {
        try {
            // 사용자 함수 객체의 복사 생성자도 재진입할 수 있다. 잠금 안에서는 공유 포인터만 복사한다.
            std::vector<Observer> handlers;
            {
                std::lock_guard lock(mutex);
                last_error = error;
                if (const auto local = subscription_observers.find(error.SubscriptionId); local != subscription_observers.end())
                    handlers.push_back(local->second);
                if (observer) handlers.push_back(observer);
                for (const auto& [id, handler] : observers) { (void)id; handlers.push_back(handler); }
            }
            // 관찰자는 상태 잠금 밖에서 호출한다. 관찰자가 상태 조회나 종료 API를 호출해도 재진입 교착을 피한다.
            for (const auto& handler : handlers) { try { (*handler)(error); } catch (...) {} }
        } catch (...) { /* 오류 관찰자 때문에 수신 스레드가 종료되지 않도록 한다. */ }
    }
};
// 수신 중인 연결을 스레드별로 식별하고 스코프 종료 시 이전 값을 복원한다. 중첩 콜백도 바깥 정보를 잃지 않는다.
struct CallbackScope {
    const ClientStatus* previous;
    explicit CallbackScope(const ClientStatus* current) : previous(ActiveClientCallback()) { ActiveClientCallback() = current; }
    ~CallbackScope() { ActiveClientCallback() = previous; }
};
// 네이티브 자원보다 먼저 선언해 모든 자원 파괴가 끝난 후에 완료 상태를 기록한다.
struct CleanupWitness {
    std::shared_ptr<ClientStatus> status;
    ~CleanupWitness() { status->complete(); }
};
}
}
