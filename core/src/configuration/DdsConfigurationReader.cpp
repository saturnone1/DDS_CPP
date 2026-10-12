#include "DdsConfigurationReader.hpp"

#include <dds/core/QosProvider.hpp>
#include <algorithm>
#include <unordered_set>

#include "dds_cpp/api/DdsConfigurationException.hpp"
#include "dds_cpp/api/DdsOperationException.hpp"
#include "DdsClientOptionResolver.hpp"
#include "DdsUnicode.hpp"

namespace dds_cpp {
namespace {

std::unordered_set<std::string> LoadQosProfileNames(const std::string& path)
{
    try {
        dds::core::QosProvider provider(detail::RtiFilePath(path));
        std::unordered_set<std::string> profiles;
        for (const auto& library : provider->qos_profile_libraries())
            for (const auto& profile : provider->qos_profiles(library))
                profiles.insert(library + "::" + profile);
        return profiles;
    } catch (const DdsConfigurationException&) {
        throw;
    } catch (const std::exception& error) {
        throw DdsConfigurationException("잘못된 QoS XML '" + path + "': " + error.what());
    }
}

void ValidateTopics(
    const std::vector<TopicDefinition>& topics,
    const std::unordered_set<std::string>& qos_profiles,
    const std::unordered_set<std::type_index>& message_types)
{
    std::unordered_set<std::string> seen;

    for (const auto& topic : topics) {
        const auto& name = topic.name;
        const auto& qos_profile = topic.QosProfile();

        if (name.find_first_not_of(" \t\r\n") == std::string::npos) {
            throw DdsConfigurationException("메시지 등록에는 비어 있지 않은 이름이 필요합니다.");
        }
        if (qos_profile.find_first_not_of(" \t\r\n") == std::string::npos) {
            throw DdsConfigurationException("Topic '" + name + "' 의 qos_profile이 비어 있습니다.");
        }

        if (!seen.insert(name).second) {
            throw DdsConfigurationException("Topic '" + name + "' 이(가) 코드에 중복 등록되었습니다.");
        }

        if (!qos_profiles.contains(topic.QualifiedQosProfile())) {
            throw DdsConfigurationException(
                "Topic '" + name + "' 에서 참조하는 QoS 프로파일이 없습니다: '" + topic.QualifiedQosProfile() + "'.");
        }

        if (!message_types.contains(topic.sample_type)) {
            throw DdsConfigurationException("Topic '" + name + "' 에 지정된 메시지 타입이 등록되지 않았습니다.");
        }

        if (topic.direction != TopicDirection::Both && topic.direction != TopicDirection::Publish &&
            topic.direction != TopicDirection::Subscribe) {
            throw DdsConfigurationException("Topic '" + name + "' 의 송수신 방향이 잘못되었습니다:. Both, Publish 또는 Subscribe를 사용하세요.");
        }
    }
}

} // namespace

DdsConfiguration DdsConfigurationReader::Load(const DdsClientOptions& options)
{
    const auto qos_path = detail::DdsClientOptionResolver::ResolveRequiredFile(
        options.QosProfilesXmlPath(), "definitions/qos_profiles.xml", "qos_profiles_xml_path");
    auto qos_profiles = LoadQosProfileNames(qos_path);
    if (!qos_profiles.contains(options.ParticipantQosProfile()))
        throw DdsConfigurationException("Participant QoS 프로파일이 없습니다: " + options.ParticipantQosProfile());
    if (options.ShutdownTimeout().count() < 0 || options.ShutdownTimeout().count() > 2147483647LL)
        throw DdsConfigurationException("ShutdownTimeout은 0 이상의 유한한 값이어야 합니다.");
    std::unordered_set<std::string> module_names;
    std::unordered_set<std::type_index> message_types;
    std::vector<const MessageType*> entries;
    for (const auto& module : options.MessageModules()) {
        if (!module || module->name.find_first_not_of(" \t\r\n") == std::string::npos ||
            module->version.find_first_not_of(" \t\r\n") == std::string::npos || !module_names.insert(module->name).second)
            throw DdsConfigurationException("메시지 모듈에는 중복되지 않는 이름과 버전이 필요합니다.");
        for (const auto& entry : module->types) {
            if (entry.name.find_first_not_of(" \t\r\n") == std::string::npos ||
                entry.type == std::type_index(typeid(void)) || !entry.create_writer || !entry.create_reader ||
                !message_types.insert(entry.type).second)
                throw DdsConfigurationException("메시지 타입 등록이 잘못되었거나 중복되었습니다.");
            entries.push_back(&entry);
        }
    }
    auto registrations = options.Topics().value_or(std::vector<TopicDefinition>{});
    if (!options.Topics()) {
        for (const auto* entry : entries)
            registrations.push_back({entry->name, std::string(detail::default_topic_profile), TopicDirection::Both, entry->type});
        std::sort(registrations.begin(), registrations.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    }
    for (auto& topic : registrations) {
        if (!topic.qos_profile) topic.qos_profile = detail::default_topic_profile;
        if (topic.sample_type == std::type_index(typeid(void))) {
            const MessageType* match = nullptr;
            for (const auto* entry : entries) {
                if (entry->name != topic.name) continue;
                if (match) throw DdsConfigurationException("토픽 타입이 모호합니다. 타입을 지정하여 등록하세요.");
                match = entry;
            }
            if (!match) throw DdsConfigurationException("토픽에 등록된 타입이 없습니다: " + topic.name);
            topic.sample_type = match->type;
        } else if (topic.name.empty()) {
            for (const auto* entry : entries)
                if (entry->type == topic.sample_type) { topic.name = entry->name; break; }
        }
    }
    ValidateTopics(registrations, qos_profiles, message_types);
    return DdsConfiguration(std::move(registrations));
}

DdsConfiguration DdsConfigurationReader::Load(
    const std::optional<std::vector<TopicDefinition>>& topics, const std::string& path,
    std::vector<std::shared_ptr<const DdsMessageModule>> modules)
{
    DdsClientOptions options;
    options.Topics() = topics;
    options.QosProfilesXmlPath() = path;
    options.MessageModules() = std::move(modules);
    return Load(options);
}
} // namespace dds_cpp
