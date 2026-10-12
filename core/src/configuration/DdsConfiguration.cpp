#include "dds_cpp/configuration/DdsConfiguration.hpp"

#include <utility>

#include "dds_cpp/api/DdsConfigurationException.hpp"
#include "dds_cpp/api/DdsOperationException.hpp"

namespace dds_cpp {

DdsConfiguration::DdsConfiguration(std::vector<TopicDefinition> topics)
    : topics_(std::move(topics))
{
    topic_index_.reserve(topics_.size());
    type_index_.reserve(topics_.size());
    for (std::size_t i = 0; i < topics_.size(); ++i) {
        const auto [_, inserted] = topic_index_.emplace(topics_[i].name, i);
        if (!inserted) {
            throw DdsConfigurationException("Topic '" + topics_[i].name + "' 이(가) 코드에 중복 등록되었습니다.");
        }
        const auto [binding, unique] = type_index_.emplace(topics_[i].sample_type, i);
        if (!unique) binding->second = topics_.size();
    }
}

const TopicDefinition& DdsConfiguration::GetTopic(const std::type_info& sample_type) const
{
    const auto binding = type_index_.find(std::type_index(sample_type));
    if (binding == type_index_.end())
        throw DdsConfigurationException("이 타입에 등록된 토픽이 없습니다.");
    if (binding->second == topics_.size())
        throw DdsConfigurationException("이 타입에 여러 바인딩이 있습니다. 토픽 이름을 지정하세요.");
    return topics_[binding->second];
}

const TopicDefinition& DdsConfiguration::GetTopic(const std::string& topic_name) const
{
    const TopicDefinition* topic = nullptr;
    if (!TryGetTopic(topic_name, topic)) {
        throw DdsConfigurationException("정의되지 않은 토픽: '" + topic_name + "'.");
    }

    return *topic;
}

} // namespace dds_cpp
