#include "dds_cpp/dds_configuration.hpp"

#include <utility>

#include "dds_cpp/dds_exception.hpp"

namespace dds_cpp {

DdsConfiguration::DdsConfiguration(std::vector<TopicDefinition> topics)
    : topics_(std::move(topics))
{
    if (topics_.empty()) {
        throw DdsConfigurationException("topics.xml must contain at least one <topic> element.");
    }

    topic_index_.reserve(topics_.size());
    for (std::size_t i = 0; i < topics_.size(); ++i) {
        const auto [_, inserted] = topic_index_.emplace(topics_[i].name, i);
        if (!inserted) {
            throw DdsConfigurationException("Topic '" + topics_[i].name + "' is duplicated in topics.xml.");
        }
    }
}

const TopicDefinition& DdsConfiguration::get_topic(const std::string& topic_name) const
{
    const auto* topic = try_get_topic(topic_name);
    if (topic == nullptr) {
        throw DdsConfigurationException("No topic is defined for '" + topic_name + "'.");
    }

    return *topic;
}

const TopicDefinition* DdsConfiguration::try_get_topic(const std::string& topic_name) const noexcept
{
    const auto it = topic_index_.find(topic_name);
    return it == topic_index_.end() ? nullptr : &topics_[it->second];
}

const std::vector<TopicDefinition>& DdsConfiguration::topics() const noexcept
{
    return topics_;
}

} // namespace dds_cpp
