#pragma once
#include "dds_cpp/configuration/DdsClientOptions.hpp"
#include "dds_cpp/api/DdsConfigurationException.hpp"
#include "dds_cpp/api/DdsOperationException.hpp"
#include <filesystem>

namespace dds_cpp {
/// 독립 망의 메시지 모듈과 도메인·QoS를 묶는 설정 도우미다. 이 객체 자체는 연결을 열지 않는다.
class DdsNetwork final {
public:
    /// 모듈 정보를 복사하고 QoS 경로를 절대 경로로 고정한다. Participant와 토픽 프로파일은 같은 라이브러리에 속해야 한다.
    DdsNetwork(std::string name, std::shared_ptr<const DdsMessageModule> module, int domain,
        std::string qos_path, std::string participant_profile, std::string topic_profile)
        : name_(std::move(name)), module_(module ? std::make_shared<const DdsMessageModule>(*module) : nullptr),
          domain_(domain), participant_profile_(std::move(participant_profile)), topic_profile_(std::move(topic_profile))
    {
        if (name_.find_first_not_of(" \t\r\n") == std::string::npos || !module_ ||
            qos_path.find_first_not_of(" \t\r\n") == std::string::npos)
            throw DdsConfigurationException("망 설정에는 이름, 메시지 모듈과 QoS 경로가 필요합니다.");
        const auto absolute_path = std::filesystem::absolute(
            std::filesystem::path(std::u8string(qos_path.begin(), qos_path.end()))).u8string();
        qos_path_.assign(reinterpret_cast<const char*>(absolute_path.data()), absolute_path.size());
        if (library(participant_profile_) != library(topic_profile_))
            throw DdsConfigurationException("Participant와 토픽 프로파일은 해당 망의 QoS 라이브러리에 속해야 합니다.");
    }
    const std::string& Name() const noexcept { return name_; }
    const std::shared_ptr<const DdsMessageModule>& Module() const noexcept { return module_; }
    int DomainId() const noexcept { return domain_; }
    const std::string& QosPath() const noexcept { return qos_path_; }
    const std::string& ParticipantProfile() const noexcept { return participant_profile_; }
    const std::string& TopicProfile() const noexcept { return topic_profile_; }
    const std::string& MulticastReceiveAddress() const noexcept { return multicast_address_; }
    std::string& MulticastReceiveAddress() noexcept { return multicast_address_; }
    auto ShutdownTimeout() const noexcept { return shutdown_timeout_; }
    auto& ShutdownTimeout() noexcept { return shutdown_timeout_; }
    /// nullopt이면 전체 타입을 양방향 등록한다. 빈 vector를 명시하면 토픽을 등록하지 않는다.
    /// 짧은 프로파일 이름은 이 망의 라이브러리로 한정하고 다른 라이브러리의 프로파일은 거부한다.
    DdsClientOptions CreateClientOptions(std::optional<std::vector<TopicDefinition>> topics = std::nullopt) const
    {
        return CreateClientOptions(std::move(topics), multicast_address_);
    }

    // 이전 호출 방식은 유지하되 기본 주소는 망 객체에서도 지정할 수 있다.
    DdsClientOptions CreateClientOptions(std::optional<std::vector<TopicDefinition>> topics,
        std::string multicast_address) const
    {
        auto registrations = topics.value_or(std::vector<TopicDefinition>{});
        if (!topics) for (const auto& type : module_->types)
            registrations.push_back({type.name, topic_profile_, TopicDirection::Both, type.type});
        for (auto& topic : registrations) {
            if (!topic.qos_profile) topic.qos_profile = topic_profile_;
            else if (topic.qos_profile->find("::") == std::string::npos)
                topic.qos_profile = library(participant_profile_) + "::" + *topic.qos_profile;
            if (library(*topic.qos_profile) != library(participant_profile_))
                throw DdsConfigurationException("토픽에 다른 망의 QoS 라이브러리를 지정할 수 없습니다.");
        }
        DdsClientOptions options;
        options.MessageModules() = {module_}; options.DomainId() = domain_; options.ParticipantName() = name_;
        options.QosProfilesXmlPath() = qos_path_; options.ParticipantQosProfile() = participant_profile_;
        options.Topics() = std::move(registrations); options.MulticastReceiveAddress() = std::move(multicast_address);
        options.ShutdownTimeout() = shutdown_timeout_;
        return options;
    }
private:
    static std::string library(const std::string& profile) {
        const auto separator = profile.find("::");
        if (separator == std::string::npos || separator == 0 || separator + 2 == profile.size() ||
            profile.substr(0, separator).find_first_not_of(" \t\r\n") == std::string::npos ||
            profile.substr(separator + 2).find_first_not_of(" \t\r\n") == std::string::npos ||
            profile.find("::", separator + 2) != std::string::npos)
            throw DdsConfigurationException("망의 QoS 프로파일은 Library::Profile 형식으로 지정해야 합니다.");
        return profile.substr(0, separator);
    }
    std::string name_;
    std::shared_ptr<const DdsMessageModule> module_;
    int domain_;
    std::string qos_path_, participant_profile_, topic_profile_;
    std::string multicast_address_;
    std::chrono::milliseconds shutdown_timeout_{10000};
};
}
