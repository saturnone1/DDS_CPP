#include "InMemoryDdsTransport.hpp"

#include <algorithm>
#include <stdexcept>
#include <typeindex>
#include <utility>
#include <thread>

#include "dds_cpp/api/DdsConfigurationException.hpp"
#include "dds_cpp/api/DdsOperationException.hpp"

namespace dds_cpp {

namespace {

struct Subscriber final {
    std::uint64_t id{};
    std::type_index native_type{typeid(void)};
    TypedSampleHandler typed_handler;
};

struct TopicType final {
    std::type_index native_type{typeid(void)};
};

} // namespace

struct InMemoryDdsTransport::State final {
    std::mutex mutex;
    bool closed{false};
    std::unordered_map<std::string, std::vector<Subscriber>> subscribers;
    std::unordered_map<std::string, TopicType> topic_types;

    void bind_typed(const TopicDefinition& topic, const std::type_info& sample_type)
    {
        if (closed) throw DdsOperationException("DDS 클라이언트가 닫혔습니다.");
        const auto native_type = std::type_index(sample_type);
        const auto [it, inserted] = topic_types.try_emplace(
            topic.name, TopicType{native_type});
        if (!inserted &&
            it->second.native_type != native_type) {
            throw DdsOperationException("Topic '" + topic.name + "' 은(는) 이미 다음 타입에 바인딩되어 있습니다: 다른 샘플 타입.");
        }
    }

    void unsubscribe(std::uint64_t id, const std::string& topic_name)
    {
        std::lock_guard lock(mutex);
        const auto it = subscribers.find(topic_name);
        if (it == subscribers.end()) {
            return;
        }

        auto& entries = it->second;
        entries.erase(
            std::remove_if(entries.begin(), entries.end(), [id](const Subscriber& subscriber) {
                return subscriber.id == id;
            }),
            entries.end());
        if (entries.empty()) {
            subscribers.erase(it);
        }
    }
};

class InMemoryDdsTransport::InMemorySubscription final : public Subscription {
public:
    InMemorySubscription(
        std::weak_ptr<State> state,
        std::uint64_t id,
        std::string topic_name)
        : state_(std::move(state))
        , id_(id)
        , topic_name_(std::move(topic_name))
    {
    }

    ~InMemorySubscription() override
    {
        if (const auto state = state_.lock()) {
            state->unsubscribe(id_, topic_name_);
        }
    }

private:
    std::weak_ptr<State> state_;
    std::uint64_t id_;
    std::string topic_name_;
};

InMemoryDdsTransport::InMemoryDdsTransport()
    : state_(std::make_shared<State>())
{
}

InMemoryDdsTransport::~InMemoryDdsTransport() { status_->complete(); }

void InMemoryDdsTransport::CreatePublisher(
    const TopicDefinition& topic,
    const std::type_info& sample_type)
{
    std::lock_guard lock(state_->mutex);
    state_->bind_typed(topic, sample_type);
}

std::unique_ptr<Subscription> InMemoryDdsTransport::Subscribe(
    const TopicDefinition& topic,
    const std::type_info& sample_type,
    TypedSampleHandler handler, std::uint64_t subscription_id)
{
    if (!handler) {
        throw std::invalid_argument("인메모리 타입 지정 구독에는 핸들러가 필요합니다.");
    }

    std::lock_guard lock(state_->mutex);
    state_->bind_typed(topic, sample_type);
    const auto id = subscription_id;
    state_->subscribers[topic.name].push_back(Subscriber{
        id,
        std::type_index(sample_type),
        std::move(handler)});
    return std::make_unique<InMemorySubscription>(state_, id, topic.name);
}

void InMemoryDdsTransport::Publish(
    const TopicDefinition& topic,
    const std::type_info& sample_type,
    const void* sample)
{
    if (sample == nullptr) {
        throw std::invalid_argument("인메모리 타입 지정 발행에는 샘플이 필요합니다.");
    }

    std::vector<Subscriber> snapshot;
    {
        std::lock_guard lock(state_->mutex);
        state_->bind_typed(topic, sample_type);
        const auto it = state_->subscribers.find(topic.name);
        if (it == state_->subscribers.end()) {
            return;
        }
        snapshot = it->second;
    }

    const auto native_type = std::type_index(sample_type);
    for (const auto& subscriber : snapshot) {
        if (subscriber.native_type == native_type) {
            subscriber.typed_handler(sample);
        }
    }
}

void InMemoryDdsTransport::Dispose() noexcept
{
    std::lock_guard lock(state_->mutex);
    state_->closed = true;
    state_->subscribers.clear();
}

void InMemoryDdsTransport::WaitForSubscribers(const TopicDefinition& topic, const std::type_info& type,
    int count, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        {
            std::lock_guard lock(state_->mutex);
            state_->bind_typed(topic, type);
            const auto it = state_->subscribers.find(topic.name);
            if (it != state_->subscribers.end() && it->second.size() >= static_cast<std::size_t>(count)) return;
        }
        if (std::chrono::steady_clock::now() >= deadline) throw DdsOperationException("구독자 연결 대기 시간이 초과되었습니다.");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
void InMemoryDdsTransport::WaitForAcknowledgments(const TopicDefinition& topic, const std::type_info& type,
    std::chrono::milliseconds)
{
    std::lock_guard lock(state_->mutex);
    state_->bind_typed(topic, type); // Synchronous dispatch has already completed.
}

} // namespace dds_cpp
