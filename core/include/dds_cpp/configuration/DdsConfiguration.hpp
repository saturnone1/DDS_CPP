#pragma once

#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "dds_cpp/api/TopicDefinition.hpp"
#include "dds_cpp/api/Visibility.hpp"

namespace dds_cpp {

class DDSCPP_API DdsConfiguration final {
public:
    explicit DdsConfiguration(std::vector<TopicDefinition> topics);


    [[nodiscard]] const TopicDefinition& GetTopic(const std::string& topic_name) const;
    // 타입이 없거나 여러 토픽에 연결된 경우 DdsConfigurationException을 발생시킨다.
    [[nodiscard]] const TopicDefinition& GetTopic(const std::type_info& sample_type) const;
    [[nodiscard]] bool TryGetTopic(const std::string& topic_name, const TopicDefinition*& topic) const noexcept
    {
        const auto found = topic_index_.find(topic_name);
        topic = found == topic_index_.end() ? nullptr : &topics_[found->second];
        return topic != nullptr;
    }
    [[nodiscard]] const std::vector<TopicDefinition>& Topics() const noexcept { return topics_; }

private:
    std::vector<TopicDefinition> topics_;
    std::unordered_map<std::string, std::size_t> topic_index_;
    // 토픽 목록의 크기를 모호한 타입의 표식으로 사용한다. 포인터 대신 위치를 저장해 복사·이동 후에도 인덱스를 유지한다.
    std::unordered_map<std::type_index, std::size_t> type_index_;
};

} // namespace dds_cpp
