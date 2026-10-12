#pragma once
#include "dds_cpp/transport/RtiTopic.hpp"
#include "dds_cpp/api/DdsMessageModule.hpp"
#include "dds_cpp/configuration/DdsClientOptions.hpp"
#include "dds_cpp/transport/RtiSubscriptionSource.hpp"
#include "dds_cpp/diagnostics/DdsClientLog.hpp"
#include "dds_cpp/transport/BackgroundCleanupTask.hpp"
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace dds_cpp::detail {
class ISubscriptionDispatcher {
public:
    virtual ~ISubscriptionDispatcher() = default;
    // 종료 요청은 비동기이며 전체 대기 시간은 호출자가 정한다.
    virtual void RequestStop() noexcept = 0;
    virtual void WaitForShutdown(std::chrono::steady_clock::time_point deadline) noexcept = 0;
};

// 모든 Reader에 먼저 종료를 요청한 뒤 공통 마감 시각으로 기다린다.
// Reader 수만큼 각각의 전체 타임아웃을 반복하여 종료 시간이 배수로 늘어나는 것을 피한다.
inline void StopSubscriptions(const std::vector<std::weak_ptr<ISubscriptionDispatcher>>& readers,
    std::chrono::milliseconds timeout) noexcept
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (const auto& weak : readers)
        if (auto reader = weak.lock()) reader->RequestStop();
    for (const auto& weak : readers)
        if (auto reader = weak.lock()) reader->WaitForShutdown(deadline);
}
template <class T> class SubscriptionDispatcher;

template <class T>
class TypedReaderListener final : public dds::sub::NoOpDataReaderListener<T> {
public:
    void SetOwner(std::weak_ptr<SubscriptionDispatcher<T>> owner)
    {
        owner_ = std::move(owner);
    }

    void on_data_available(dds::sub::DataReader<T>& reader) override
    {
        if (const auto owner = owner_.lock()) {
            owner->NotifyDataAvailable(reader);
        }
    }

private:
    std::weak_ptr<SubscriptionDispatcher<T>> owner_;
};

// Reader·리스너·수신 스레드가 공유하는 상태다. 공개 구독 핸들이 먼저 사라져도 수신 스레드가 상태를 보존한다.
// owner_는 연결 자원을 붙잡아 핸들러가 끝나기 전에 Participant가 파괴되는 것을 막는다.
template <class T>
class SubscriptionDispatcher final
    : public ISubscriptionDispatcher, public std::enable_shared_from_this<SubscriptionDispatcher<T>> {
public:
    static std::shared_ptr<SubscriptionDispatcher> Create(
        std::shared_ptr<void> transport_owner,
        const dds::domain::DomainParticipant& participant,
        const dds::sub::Subscriber& subscriber,
        const dds::sub::qos::DataReaderQos& reader_qos,
        const TopicDefinition& definition,
        std::function<void(const void*)> handler,
        DdsLogLevel log_level,
        std::mutex& topic_mutex, std::shared_ptr<ClientStatus> status, std::uint64_t subscription_id)
    {
        auto state = std::shared_ptr<SubscriptionDispatcher>(new SubscriptionDispatcher(
            std::move(transport_owner), participant, definition, std::move(handler), log_level, topic_mutex, std::move(status), subscription_id));
        state->listener_.SetOwner(state);
        state->sample_source_.CreateReader(subscriber, reader_qos, &state->listener_);
        // 전용 수신 스레드에서 사용자 코드를 실행해 RTI 리스너의 배타 구간을 벗어난다. Reader 생성이 끝난 뒤에만 수신을 시작한다.
        bool started = false;
        try {
            BackgroundCleanupTask::Start([state] { state->RunDispatchLoop(); }, &started);
        } catch (...) {
            state->RequestStop();
            // 분리 실패 시 join으로 호출자를 묶지 않는다. 실행 중인 스레드가 상태의 소유권을 유지한다.
            // 생성 자체가 실패하면 네이티브 객체를 호출자에서 동기 파괴하지 않고 실패 상태로 보존한다.
            if (!started) {
                state->failed_cleanup_retainer_ = state;
                state->status_->fault(std::current_exception());
            }
            throw;
        }
        return state;
    }

    void NotifyDataAvailable(dds::sub::DataReader<T>& reader)
    {
        (void)reader;
        // RTI 리스너 콜백에서는 샘플 처리·사용자 코드 실행·정리 대기를 하지 않는다.
        { std::lock_guard lock(dispatch_mutex_); pending_ = true; }
        dispatch_changed_.notify_one();
    }

    void RequestStop() noexcept override
    {
        { std::lock_guard lock(dispatch_mutex_); closing_ = true; }
        dispatch_changed_.notify_one();
    }

    void WaitForShutdown(std::chrono::steady_clock::time_point deadline) noexcept override
    {
        // 이 콜백이 반환되어야 close가 실행되므로 자신의 완료 신호를 기다려서는 안 된다.
        if (active_callback_ == this) return;
        std::unique_lock lock(cleanup_mutex_);
        cleanup_changed_.wait_until(lock, deadline, [this] { return cleanup_done_; });
    }

private:
    void LogError(const char* message, const char* detail = "") const noexcept
    {
        DdsClientLog::Error(log_level_, message, detail);
    }

    SubscriptionDispatcher(
        std::shared_ptr<void> owner,
        const dds::domain::DomainParticipant& participant,
        const TopicDefinition& definition,
        std::function<void(const void*)> handler,
        DdsLogLevel log_level,
        std::mutex& topic_mutex, std::shared_ptr<ClientStatus> status, std::uint64_t subscription_id)
        : owner_(std::move(owner))
        , sample_source_(participant, definition, topic_mutex)
        , handler_(std::move(handler))
        , log_level_(log_level)
        , status_(std::move(status)), topic_name_(definition.name)
        , subscription_id_(subscription_id)
    {
    }

    void RunDispatchLoop() noexcept
    {
        CallbackScope callback_scope(status_.get());
        // 현재 수신 콜백에서 자기 자신의 종료를 기다리지 않도록 스레드 식별자를 유지한다.
        active_callback_ = this;
        try {
            while (true) {
                {
                    std::unique_lock lock(dispatch_mutex_);
                    dispatch_changed_.wait(lock, [this] { return closing_ || pending_; });
                    if (closing_) break;
                    pending_ = false;
                }
                // 알림 하나가 메시지 하나를 뜻하지 않는다. 현재 배치의 유효 샘플을 모두 처리한다.
                // 대여 샘플은 이 스코프 끝에서 반환되므로 사용자 핸들러는 참조를 외부에 저장하면 안 된다.
                sample_source_.Dispatch(closing_, [this](const void* sample) {
                    try { handler_(sample); }
                    catch (const std::exception& ex) {
                        LogError("DDS 구독 핸들러 오류: ", ex.what());
                    } catch (...) {
                        LogError("DDS 구독 핸들러에서 알 수 없는 예외가 발생했습니다.");
                    }
                });
            }
        } catch (const std::exception& ex) {
            status_->report({topic_name_, std::current_exception(), true, subscription_id_});
            LogError("DDS 수신기 오류: ", ex.what());
        } catch (...) {
            status_->report({topic_name_, std::current_exception(), true, subscription_id_});
            LogError("DDS 수신기에서 알 수 없는 예외가 발생했습니다.");
        }
        closing_ = true;
        active_callback_ = nullptr;
        CompleteDispose();
    }

    void CompleteDispose() noexcept
    {
        // 네이티브 삭제가 끝날 때까지 수신 스레드가 상태를 소유한다. 이 시점에는 사용자 콜백과 대여 샘플이 없으며 리스너는 수신 신호만 보낸다.
        try {
            sample_source_.Dispose();
            owner_.reset();
        } catch (...) {
            status_->fault(std::current_exception());
            // 정리에 실패한 네이티브 객체를 무조건 소멸시키지 않는다. 상태를 보존하여 남은 참조의 조기 파괴를 피한다.
            failed_cleanup_retainer_ = this->shared_from_this();
        }
        {
            std::lock_guard lock(cleanup_mutex_);
            cleanup_done_ = true;
        }
        cleanup_changed_.notify_all();
    }

private:
    std::shared_ptr<void> owner_;
    TypedReaderListener<T> listener_;
    // 소스가 리스너보다 먼저 파괴되도록 선언한다. 생성·스레드 시작 실패에도 리스너 포인터가 먼저 무효화되면 안 된다.
    RtiSubscriptionSource<T> sample_source_;
    std::function<void(const void*)> handler_;
    DdsLogLevel log_level_;
    std::shared_ptr<ClientStatus> status_;
    std::string topic_name_;
    const std::uint64_t subscription_id_;
    std::mutex dispatch_mutex_;
    std::condition_variable dispatch_changed_;
    // 여러 RTI 알림을 한 번의 읽기 요청으로 합친다. 최초 true는 Reader 생성 직후 도착한 데이터도 읽게 한다.
    bool pending_{true};
    std::mutex cleanup_mutex_;
    std::condition_variable cleanup_changed_;
    // 정리 시도가 끝났다는 신호다. 성공 여부는 공유 ClientStatus의 종료 오류와 함께 판단한다.
    bool cleanup_done_{false};
    std::atomic_bool closing_{false};
    std::shared_ptr<SubscriptionDispatcher> failed_cleanup_retainer_;
    inline static thread_local const SubscriptionDispatcher* active_callback_ = nullptr;
};

template <class T>
class SubscriptionHandle final : public Subscription {
public:
    SubscriptionHandle(
        std::shared_ptr<void> owner,
        const dds::domain::DomainParticipant& participant,
        const dds::sub::Subscriber& subscriber,
        const dds::sub::qos::DataReaderQos& reader_qos,
        const TopicDefinition& definition,
        std::function<void(const void*)> handler,
        DdsLogLevel log_level,
        std::mutex& topic_mutex, std::chrono::milliseconds shutdown_timeout, std::shared_ptr<ClientStatus> status,
        std::uint64_t subscription_id)
        : shutdown_timeout_(shutdown_timeout), state_(SubscriptionDispatcher<T>::Create(
              std::move(owner), participant, subscriber, reader_qos, definition,
              std::move(handler), log_level, topic_mutex, std::move(status), subscription_id))
    {
    }

    std::weak_ptr<ISubscriptionDispatcher> state() const { return state_; }

    ~SubscriptionHandle() override
    {
        const auto deadline = std::chrono::steady_clock::now() + shutdown_timeout_;
        state_->RequestStop();
        state_->WaitForShutdown(deadline);
    }

private:
    std::chrono::milliseconds shutdown_timeout_;
    std::shared_ptr<SubscriptionDispatcher<T>> state_;
};

} // namespace dds_cpp::detail
