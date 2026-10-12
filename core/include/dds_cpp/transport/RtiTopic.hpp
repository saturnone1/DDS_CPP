#pragma once
#include "dds_cpp/transport/RtiHeaders.hpp"
#include <mutex>
#include <string>
#include <typeinfo>
#include <typeindex>
#include <string_view>
#include <functional>
#include "dds_cpp/api/TopicDefinition.hpp"

namespace dds_cpp::detail {
struct WriterKeyView {
    std::string_view name;
    std::type_index type;
};
struct WriterKey {
    std::string name;
    std::type_index type;
    operator WriterKeyView() const noexcept { return {name, type}; }
};
struct WriterKeyHash {
    using is_transparent = void;
    std::size_t operator()(WriterKeyView key) const noexcept {
        const auto name_hash = std::hash<std::string_view>{}(key.name);
        return name_hash ^ (key.type.hash_code() + 0x9e3779b9u + (name_hash << 6) + (name_hash >> 2));
    }
};
struct WriterKeyEqual {
    using is_transparent = void;
    bool operator()(WriterKeyView a, WriterKeyView b) const noexcept { return a.name == b.name && a.type == b.type; }
};
inline WriterKeyView CreateTopicKey(const TopicDefinition& topic, const std::type_info& type)
{
    return {topic.name, type};
}

inline std::string ResolveQosProfile(const TopicDefinition& topic)
{
    return topic.QualifiedQosProfile();
}

// 토픽 조회·생성만 직렬화한다. 사용자 콜백이나 write는 이 잠금으로 보호하지 않는다.
template<class T>
dds::topic::Topic<T> GetOrCreateTopic(
    const dds::domain::DomainParticipant& participant, const std::string& name,
    std::mutex& topic_mutex)
{
    std::lock_guard lock(topic_mutex);
    auto topic = dds::topic::find<dds::topic::Topic<T>>(participant, name);
    if (topic == dds::core::null) topic = dds::topic::Topic<T>(participant, name);
    return topic;
}

} // namespace dds_cpp::detail
