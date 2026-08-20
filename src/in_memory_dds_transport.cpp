#include "dds_cpp/in_memory_dds_transport.hpp"

#include <algorithm>
#include <stdexcept>
#include <typeindex>
#include <utility>

#include "dds_cpp/dds_exception.hpp"

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

struct TopicType final {
    SubscriberKind kind{SubscriberKind::Bytes};
    std::string byte_type_name;
    std::type_index native_type{typeid(void)};
};

} // namespace

struct InMemoryDdsTransport::State final {
    std::mutex mutex;
    std::uint64_t next_id{1};
    std::unordered_map<std::string, std::vector<Subscriber>> subscribers;
    std::unordered_map<std::string, TopicType> topic_types;

    void bind_bytes(const TopicDefinition& topic, const std::string& sample_type_name)
    {
        const auto [it, inserted] = topic_types.try_emplace(
            topic.name, TopicType{SubscriberKind::Bytes, sample_type_name, typeid(void)});
        if (!inserted &&
            (it->second.kind != SubscriberKind::Bytes || it->second.byte_type_name != sample_type_name)) {
            throw DdsOperationException("Topic '" + topic.name + "' is already bound to a different sample type.");
        }
    }

    void bind_typed(const TopicDefinition& topic, const std::type_info& sample_type)
    {
        const auto native_type = std::type_index(sample_type);
        const auto [it, inserted] = topic_types.try_emplace(
            topic.name, TopicType{SubscriberKind::Typed, {}, native_type});
        if (!inserted &&
            (it->second.kind != SubscriberKind::Typed || it->second.native_type != native_type)) {
            throw DdsOperationException("Topic '" + topic.name + "' is already bound to a different sample type.");
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

InMemoryDdsTransport::~InMemoryDdsTransport() = default;

void InMemoryDdsTransport::create_publisher(const TopicDefinition& topic, std::string sample_type_name)
{
    std::lock_guard lock(state_->mutex);
    state_->bind_bytes(topic, sample_type_name);
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
    state_->bind_bytes(topic, sample_type_name);
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
        state_->bind_bytes(topic, sample_type_name);
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

void InMemoryDdsTransport::create_typed_publisher(
    const TopicDefinition& topic,
    const std::type_info& sample_type)
{
    std::lock_guard lock(state_->mutex);
    state_->bind_typed(topic, sample_type);
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
    state_->bind_typed(topic, sample_type);
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
        state_->bind_typed(topic, sample_type);
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
