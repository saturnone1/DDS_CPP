#include "dds_cpp/in_memory_dds_transport.hpp"

#include <algorithm>
#include <stdexcept>
#include <typeindex>
#include <utility>

namespace dds_cpp {

namespace {

enum class SubscriberKind { Bytes, Typed };

struct Subscriber final {
    std::uint64_t id{};
    SubscriberKind kind{SubscriberKind::Bytes};
    std::string byte_type_name;
    std::type_index native_type{typeid(void)};
    SampleHandler byte_handler;
    TypedSampleHandler typed_handler;
};

} // namespace

struct InMemoryDdsTransport::State final {
    std::mutex mutex;
    std::uint64_t next_id{1};
    std::unordered_map<std::string, std::vector<Subscriber>> subscribers;

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

InMemoryDdsTransport::~InMemoryDdsTransport() = default;

void InMemoryDdsTransport::create_publisher(const TopicDefinition&, std::string)
{
}

std::unique_ptr<Subscription> InMemoryDdsTransport::subscribe(
    const TopicDefinition& topic,
    std::string sample_type_name,
    SampleHandler handler)
{
    if (!handler) {
        throw std::invalid_argument("In-memory subscription requires a handler.");
    }

    std::lock_guard lock(state_->mutex);
    const auto id = state_->next_id++;
    state_->subscribers[topic.name].push_back(Subscriber{
        id,
        SubscriberKind::Bytes,
        std::move(sample_type_name),
        typeid(void),
        std::move(handler),
        {}});
    return std::make_unique<InMemorySubscription>(state_, id, topic.name);
}

void InMemoryDdsTransport::publish(const TopicDefinition& topic, std::string sample_type_name, ByteSpan sample)
{
    std::vector<Subscriber> snapshot;
    {
        std::lock_guard lock(state_->mutex);
        const auto it = state_->subscribers.find(topic.name);
        if (it == state_->subscribers.end()) {
            return;
        }
        snapshot = it->second;
    }

    for (const auto& subscriber : snapshot) {
        if (subscriber.kind == SubscriberKind::Bytes && subscriber.byte_type_name == sample_type_name) {
            subscriber.byte_handler(sample);
        }
    }
}

void InMemoryDdsTransport::create_typed_publisher(const TopicDefinition&, const std::type_info&)
{
}

std::unique_ptr<Subscription> InMemoryDdsTransport::subscribe_typed(
    const TopicDefinition& topic,
    const std::type_info& sample_type,
    TypedSampleHandler handler)
{
    if (!handler) {
        throw std::invalid_argument("In-memory typed subscription requires a handler.");
    }

    std::lock_guard lock(state_->mutex);
    const auto id = state_->next_id++;
    state_->subscribers[topic.name].push_back(Subscriber{
        id,
        SubscriberKind::Typed,
        {},
        std::type_index(sample_type),
        {},
        std::move(handler)});
    return std::make_unique<InMemorySubscription>(state_, id, topic.name);
}

void InMemoryDdsTransport::publish_typed(
    const TopicDefinition& topic,
    const std::type_info& sample_type,
    const void* sample)
{
    if (sample == nullptr) {
        throw std::invalid_argument("In-memory typed publish requires a sample.");
    }

    std::vector<Subscriber> snapshot;
    {
        std::lock_guard lock(state_->mutex);
        const auto it = state_->subscribers.find(topic.name);
        if (it == state_->subscribers.end()) {
            return;
        }
        snapshot = it->second;
    }

    const auto native_type = std::type_index(sample_type);
    for (const auto& subscriber : snapshot) {
        if (subscriber.kind == SubscriberKind::Typed && subscriber.native_type == native_type) {
            subscriber.typed_handler(sample);
        }
    }
}

} // namespace dds_cpp
