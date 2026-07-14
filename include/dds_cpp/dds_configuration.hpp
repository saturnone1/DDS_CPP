#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "dds_cpp/topic_definition.hpp"
#include "dds_cpp/visibility.hpp"

namespace dds_cpp {

class DDSCPP_API DdsConfiguration final {
public:
    explicit DdsConfiguration(std::vector<TopicDefinition> topics);

    [[nodiscard]] const TopicDefinition& get_topic(const std::string& topic_name) const;
    [[nodiscard]] const TopicDefinition* try_get_topic(const std::string& topic_name) const noexcept;
    [[nodiscard]] const std::vector<TopicDefinition>& topics() const noexcept;

    [[nodiscard]] const TopicDefinition& GetTopic(const std::string& topic_name) const { return get_topic(topic_name); }
    [[nodiscard]] bool TryGetTopic(const std::string& topic_name, const TopicDefinition*& topic) const noexcept
    {
        topic = try_get_topic(topic_name);
        return topic != nullptr;
    }
    [[nodiscard]] const std::vector<TopicDefinition>& Topics() const noexcept { return topics(); }

private:
    std::vector<TopicDefinition> topics_;
    std::unordered_map<std::string, std::size_t> topic_index_;
};

} // namespace dds_cpp
