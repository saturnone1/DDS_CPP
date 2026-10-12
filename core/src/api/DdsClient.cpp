#include "dds_cpp/api/DdsClient.hpp"
#include "DdsConfigurationReader.hpp"
#include "IDdsTransport.hpp"
#include "RtiDdsTransport.hpp"
#include "BackgroundCleanupTask.hpp"
#include <thread>
#include <atomic>
#include <limits>
#if defined(_WIN32)
#include <Windows.h>
#else
#include <pthread.h>
#endif

namespace dds_cpp {
namespace detail {
thread_local bool fail_next_background_start = false;
void FailBackgroundStartForTest() { fail_next_background_start = true; }
bool ConsumeBackgroundStartFailure() noexcept
{
    const auto fail = fail_next_background_start;
    fail_next_background_start = false;
    return fail;
}

namespace {
std::mutex retired_mutex;
RetiredThread* retired_threads = nullptr;
bool reaper_running = false;
void ReapThreads() noexcept
{
    for (;;) {
        RetiredThread* ready = nullptr;
        {
            std::lock_guard lock(retired_mutex);
            auto** link = &retired_threads;
            while (*link) {
                auto* record = *link;
                if (record->Finished->load(std::memory_order_acquire)) {
                    *link = record->Next; record->Next = ready; ready = record;
                } else link = &record->Next;
            }
            if (!ready && !retired_threads) { reaper_running = false; return; }
        }
        while (ready) {
            auto* record = ready; ready = ready->Next;
            try { record->Worker.join(); delete record; }
            catch (...) {
                std::lock_guard lock(retired_mutex);
                record->Next = retired_threads; retired_threads = record;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
void LaunchThreadReaper() noexcept
{
    if (reaper_running || !retired_threads) return;
    // 제어 작업자는 처음부터 운영체제가 회수하는 방식으로 시작하여 detach 실패를 재귀적으로 만들지 않는다.
#if defined(_WIN32)
    reaper_running = TrySubmitThreadpoolCallback([](PTP_CALLBACK_INSTANCE instance, PVOID) {
        CallbackMayRunLong(instance);
        ReapThreads();
    }, nullptr, nullptr) != FALSE;
#else
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0) return;
    if (pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED) == 0) {
        pthread_t thread;
        reaper_running = pthread_create(&thread, &attributes, [](void*) -> void* { ReapThreads(); return nullptr; }, nullptr) == 0;
    }
    pthread_attr_destroy(&attributes);
#endif
}
}
void RetainThreadForReaping(RetiredThread* record) noexcept
{
    std::lock_guard lock(retired_mutex);
    record->Next = retired_threads; retired_threads = record;
    LaunchThreadReaper();
}
void KickThreadReaper() noexcept
{
    std::lock_guard lock(retired_mutex);
    LaunchThreadReaper();
}

void DestroyTransportAsync(IDdsTransport* transport) noexcept
{
    const auto status = transport->status();
    try { BackgroundCleanupTask::Start([transport] { delete transport; }); }
    catch (...) {
        // 스레드를 시작하지 못하면 프로세스가 종료될 때까지 네이티브 소유권을 보존한다. 호출자 스레드에서 무제한 파괴 작업을 실행하지 않는다.
        status->fault(std::current_exception());
    }
}
// 메시지 모듈의 헤더 인스턴스가 아닌 공통 Core에서 식별자를 할당한다.
std::uint64_t NextSubscriptionId()
{
    static std::atomic<std::uint64_t> next{1};
    auto id = next.load(std::memory_order_relaxed);
    do {
        if (id == (std::numeric_limits<std::uint64_t>::max)())
            throw DdsOperationException("DDS 구독 식별자가 소진되었습니다.");
    } while (!next.compare_exchange_weak(id, id + 1, std::memory_order_relaxed));
    return id;
}

const ClientStatus*& ActiveClientCallback()
{
    static thread_local const ClientStatus* current = nullptr;
    return current;
}
const DdsSampleInfo*& ActiveSampleInfo()
{
    static thread_local const DdsSampleInfo* current = nullptr;
    return current;
}

void RetireTransport(std::shared_ptr<IDdsTransport> transport, std::chrono::milliseconds timeout) noexcept
{
    // 먼저 공개 상태를 Closing으로 바꿔 신규 송수신을 막는다. 실제 close와 파괴는 백그라운드 소유자에게 넘긴다.
    if (!transport) return;
    auto status = transport->status();
    status->closing();
    try {
        BackgroundCleanupTask::Start([transport] { transport->Dispose(); });
    } catch (...) {
        status->fault(std::current_exception());
    }
    transport.reset();
    try { (void)status->wait(timeout); } catch (...) { /* 정리 실패 내용은 ShutdownError에서 확인한다. */ }
}

void EnsureOpen(const std::shared_ptr<IDdsTransport>& transport)
{
    if (!transport) throw DdsOperationException("DDS 클라이언트가 닫혔습니다.");
    // 작업 중 전송 계층 소유권은 이미 확보했다. 상태만 원자적으로 읽어 메시지마다 상태 잠금을 잡지 않는다.
    if (transport->status()->state.load(std::memory_order_acquire) != DdsShutdownState::Open)
        throw DdsOperationException("DDS 클라이언트가 닫혔습니다.");
}
void ValidateWaitTimeout(std::chrono::milliseconds timeout)
{
    if (timeout.count() < 0 || timeout.count() > 2147483647LL)
        throw std::invalid_argument("시간 제한은 0부터 Int32.MaxValue 밀리초까지의 유한한 값이어야 합니다.");
}
void WaitForSubscribers(const std::weak_ptr<IDdsTransport>& weak, const TopicDefinition& topic,
    const std::type_info& type, int count, std::chrono::milliseconds timeout)
{
    if (count < 1) throw std::invalid_argument("구독자 수는 양수여야 합니다.");
    ValidateWaitTimeout(timeout);
    const auto transport = weak.lock();
    EnsureOpen(transport);
    transport->WaitForSubscribers(topic, type, count, timeout);
}
void WaitForAcknowledgments(const std::weak_ptr<IDdsTransport>& weak, const TopicDefinition& topic,
    const std::type_info& type, std::chrono::milliseconds timeout)
{
    ValidateWaitTimeout(timeout);
    const auto transport = weak.lock();
    EnsureOpen(transport);
    transport->WaitForAcknowledgments(topic, type, timeout);
}
void PublishSample(const std::weak_ptr<IDdsTransport>& weak, const TopicDefinition& topic,
    const std::type_info& type, const void* sample)
{
    // 작업 동안 강한 참조를 유지한다. 동시 Close가 공개 핸들을 놓아도 Write가 끝나기 전 전송 계층을 파괴하지 않는다.
    const auto transport = weak.lock();
    EnsureOpen(transport);
    transport->Publish(topic, type, sample);
}
std::function<void(const void*)> BindPublisher(const std::shared_ptr<IDdsTransport>& transport,
    const TopicDefinition& topic, const std::type_info& type)
{
    EnsureOpen(transport);
    auto write = transport->BindPublisher(topic, type);
    return [weak = std::weak_ptr<IDdsTransport>(transport), write = std::move(write)](const void* sample) {
        const auto owner = weak.lock();
        EnsureOpen(owner);
        write(sample);
    };
}
}

DdsClient DdsClient::Connect(DdsClientOptions options) {
    // const shared_ptr만으로는 다른 mutable 소유자의 수정을 막을 수 없어 모듈 등록 정보도 복사한다.
    for (auto& module : options.MessageModules())
        if (module) module = std::make_shared<const DdsMessageModule>(*module);
    return DdsClient(std::move(options));
}
DdsClient::DdsClient(DdsClientOptions options)
    : DdsClient(options, DdsConfigurationReader::Load(options)) {}
DdsClient::DdsClient(DdsClientOptions options, DdsConfiguration configuration)
    : DdsClient(options, std::move(configuration), std::make_unique<RtiDdsTransport>(options)) {}
DdsClient::DdsClient(DdsClientOptions options, std::unique_ptr<IDdsTransport> transport)
    : DdsClient(options, DdsConfigurationReader::Load(options), std::move(transport)) {}
DdsClient::DdsClient(DdsClientOptions options, DdsConfiguration configuration, std::unique_ptr<IDdsTransport> transport)
    : options_(std::move(options)), configuration_(std::move(configuration)),
      transport_(transport ? std::shared_ptr<IDdsTransport>(transport.release(), detail::DestroyTransportAsync) : nullptr)
{
    if (!transport_) throw DdsOperationException("DdsClient에는 전송 계층이 필요합니다.");
    status_ = transport_->status();
}
DdsClient::DdsClient(DdsClient&& other) noexcept
    : options_(std::move(other.options_)), configuration_(std::move(other.configuration_))
{
    std::lock_guard lock(other.transport_mutex_);
    transport_ = std::move(other.transport_);
    status_ = std::move(other.status_);
}
DdsClient& DdsClient::operator=(DdsClient&& other) noexcept
{
    if (this == &other) return *this;
    std::shared_ptr<IDdsTransport> retired;
    auto timeout = options_.ShutdownTimeout();
    {
        std::scoped_lock lock(transport_mutex_, other.transport_mutex_);
        retired = std::move(transport_);
        options_ = std::move(other.options_);
        configuration_ = std::move(other.configuration_);
        transport_ = std::move(other.transport_);
        status_ = std::move(other.status_);
    }
    detail::RetireTransport(std::move(retired), timeout);
    return *this;
}
DdsClient::~DdsClient() { Close(); }
void DdsClient::Close() noexcept
{
    std::shared_ptr<IDdsTransport> retired;
    {
        std::lock_guard lock(transport_mutex_);
        retired = std::move(transport_);
    }
    detail::RetireTransport(std::move(retired), options_.ShutdownTimeout());
}
DdsShutdownState DdsClient::ShutdownState() const
{
    if (!status_) return DdsShutdownState::Closed;
    return status_->state.load(std::memory_order_acquire);
}
std::exception_ptr DdsClient::ShutdownError() const
{
    if (!status_) return {};
    std::lock_guard lock(status_->mutex); return status_->failure;
}
bool DdsClient::WaitForShutdown(std::chrono::milliseconds timeout) const
{
    detail::ValidateWaitTimeout(timeout);
    return !status_ || status_->wait(timeout);
}
std::optional<DdsSubscriptionError> DdsClient::LastSubscriptionError() const
{
    if (!status_) return {};
    std::lock_guard lock(status_->mutex); return status_->last_error;
}
void DdsClient::SetSubscriptionErrorHandler(std::function<void(const DdsSubscriptionError&)> handler)
{
    if (!status_) throw DdsOperationException("DDS 클라이언트의 소유권이 이동되었습니다.");
    // 이전 관찰자가 소유한 객체의 소멸자는 잠금 밖에서 실행한다.
    auto replacement = handler ? std::make_shared<const detail::ClientStatus::ErrorHandler>(std::move(handler)) : nullptr;
    std::lock_guard lock(status_->mutex); status_->observer.swap(replacement);
}
namespace {
void RemoveErrorObserver(const std::shared_ptr<detail::ClientStatus>& status, std::uint64_t id, bool local)
{
    // 관찰자 클로저가 연결·다른 관찰자 핸들을 소유할 수 있다. 제거 대상의 파괴는 상태 잠금 밖에서 수행한다.
    decltype(status->observers)::node_type removed;
    {
        std::lock_guard lock(status->mutex);
        removed = local ? status->subscription_observers.extract(id) : status->observers.extract(id);
    }
}
// 핸들 해제 중 사용자 정리 코드를 상태 잠금 안에서 실행하지 않는다.
class ErrorObserverHandle final : public Subscription {
public:
    ErrorObserverHandle(std::shared_ptr<detail::ClientStatus> status, std::uint64_t id,
        std::unique_ptr<Subscription> inner = {}, bool local = false)
        : status_(std::move(status)), id_(id), inner_(std::move(inner)), local_(local) {}
    ~ErrorObserverHandle() override {
        inner_.reset();
        RemoveErrorObserver(status_, id_, local_);
    }
private:
    std::shared_ptr<detail::ClientStatus> status_;
    std::uint64_t id_;
    std::unique_ptr<Subscription> inner_;
    bool local_;
};
}
std::unique_ptr<Subscription> DdsClient::ObserveSubscriptionErrors(std::function<void(const DdsSubscriptionError&)> handler)
{
    if (!handler) throw std::invalid_argument("오류 관찰자가 필요합니다.");
    if (!status_) throw DdsOperationException("DDS 클라이언트의 소유권이 이동되었습니다.");
    std::unique_ptr<Subscription> handle;
    auto observer = std::make_shared<const detail::ClientStatus::ErrorHandler>(std::move(handler));
    {
        std::lock_guard lock(status_->mutex);
        if (status_->next_observer_id == (std::numeric_limits<std::uint64_t>::max)())
            throw DdsOperationException("오류 관찰자 식별자가 소진되었습니다.");
        const auto id = status_->next_observer_id++;
        handle = std::make_unique<ErrorObserverHandle>(status_, id);
        status_->observers.emplace(id, std::move(observer));
    }
    return handle;
}
bool DdsClient::IsOpen() const noexcept
{
    std::lock_guard lock(transport_mutex_);
    if (!transport_) return false;
    return status_->state.load(std::memory_order_acquire) == DdsShutdownState::Open;
}
const DdsClientOptions& DdsClient::Options() const noexcept { return options_; }
const DdsConfiguration& DdsClient::Configuration() const noexcept { return configuration_; }
std::shared_ptr<IDdsTransport> DdsClient::RequireTransport() const
{
    std::lock_guard lock(transport_mutex_);
    detail::EnsureOpen(transport_);
    return transport_;
}
void DdsClient::PublishThroughTransport(const std::shared_ptr<IDdsTransport>& transport, const TopicDefinition& topic,
    const std::type_info& type, const void* sample)
{
    // RequireTransport가 이미 상태를 확인하고 작업 수명을 확보했다. 약한 참조로 바꾸었다가 다시 잠그지 않는다.
    transport->Publish(topic, type, sample);
}
const TopicDefinition& DdsClient::ResolveTopic(const std::type_info& type, const std::string& name, bool check_open) const
{
    if (check_open) (void)RequireTransport();
    if (!name.empty()) {
        const auto& topic = configuration_.GetTopic(name);
        if (topic.sample_type != std::type_index(type)) throw DdsOperationException("토픽의 샘플 타입이 일치하지 않습니다.");
        return topic;
    }
    return configuration_.GetTopic(type);
}
std::shared_ptr<IDdsTransport> DdsClient::CreatePublisher(const TopicDefinition& topic, const std::type_info& type)
{
    const auto transport = RequireTransport();
    EnsureCanPublish(topic);
    (void)type; // 실제 생성은 발행 핸들의 BindPublisher에서 한 번만 수행한다.
    return transport;
}
std::unique_ptr<Subscription> DdsClient::CreateSubscriber(const TopicDefinition& topic, const std::type_info& type,
    std::function<void(const void*)> handler, std::function<void(const DdsSubscriptionError&)> on_error)
{
    const auto transport = RequireTransport();
    EnsureCanSubscribe(topic);
    auto status = status_;
    const auto subscription_id = detail::NextSubscriptionId();
    if (on_error) {
        auto observer = std::make_shared<const detail::ClientStatus::ErrorHandler>(std::move(on_error));
        std::lock_guard lock(status->mutex);
        status->subscription_observers.emplace(subscription_id, std::move(observer));
    }
    try {
        auto inner = transport->Subscribe(topic, type, [status, topic, subscription_id, handler = std::move(handler)](const void* sample) {
            detail::CallbackScope scope(status.get());
            try { handler(sample); }
            catch (...) { status->report({topic.name, std::current_exception(), false, subscription_id}); }
        }, subscription_id);
        return std::make_unique<ErrorObserverHandle>(status, subscription_id, std::move(inner), true);
    } catch (...) {
        RemoveErrorObserver(status, subscription_id, true);
        throw;
    }
}
void DdsClient::EnsureCanPublish(const TopicDefinition& topic)
{
    if (topic.Direction() == TopicDirection::Subscribe)
        throw DdsOperationException("Topic '" + topic.Name() + "' 은(는) Subscribe로 설정되어 발행할 수 없습니다.");
}
void DdsClient::EnsureCanSubscribe(const TopicDefinition& topic)
{
    if (topic.Direction() == TopicDirection::Publish)
        throw DdsOperationException("Topic '" + topic.Name() + "' 은(는) Publish로 설정되어 구독할 수 없습니다.");
}
}
