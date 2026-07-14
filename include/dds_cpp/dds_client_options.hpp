#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "dds_cpp/dds_log_level.hpp"
#include "dds_cpp/visibility.hpp"

namespace dds_cpp {

struct DDSCPP_API DdsClientOptions final {
    std::string config_path{};
    std::string topics_xml_path{};
    std::string qos_profiles_xml_path{};
    std::string dds_sim_xml_path{};
    std::string participant_name{};
    std::vector<std::string> initial_peers;
    std::int32_t domain_id{0};
    bool use_rti_transport{false};
    DdsLogLevel log_level{DdsLogLevel::None};

    static DdsClientOptions from_environment();
    static DdsClientOptions load(const std::optional<std::string>& config_path = std::nullopt);

    // PascalCase aliases mirror the DdsAmbassador.DDSClient NuGet contract.
    static DdsClientOptions Load(const std::optional<std::string>& config_path = std::nullopt)
    {
        return load(config_path);
    }

    [[nodiscard]] std::int32_t DomainId() const noexcept { return domain_id; }
    std::int32_t& DomainId() noexcept { return domain_id; }
    [[nodiscard]] const std::string& TopicsXmlPath() const noexcept { return topics_xml_path; }
    std::string& TopicsXmlPath() noexcept { return topics_xml_path; }
    [[nodiscard]] const std::string& QosProfilesXmlPath() const noexcept { return qos_profiles_xml_path; }
    std::string& QosProfilesXmlPath() noexcept { return qos_profiles_xml_path; }
    [[nodiscard]] const std::string& DdsSimXmlPath() const noexcept { return dds_sim_xml_path; }
    std::string& DdsSimXmlPath() noexcept { return dds_sim_xml_path; }
    [[nodiscard]] const std::string& ParticipantName() const noexcept { return participant_name; }
    std::string& ParticipantName() noexcept { return participant_name; }
    [[nodiscard]] bool UseRtiTransport() const noexcept { return use_rti_transport; }
    bool& UseRtiTransport() noexcept { return use_rti_transport; }
    [[nodiscard]] DdsLogLevel LogLevel() const noexcept { return log_level; }
    DdsLogLevel& LogLevel() noexcept { return log_level; }
    [[nodiscard]] const std::vector<std::string>& InitialPeers() const noexcept { return initial_peers; }
    std::vector<std::string>& InitialPeers() noexcept { return initial_peers; }
};

} // namespace dds_cpp
