#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <dds/dds.hpp>
#include <rti/core/policy/CorePolicy.hpp>
#include <sstream>
#include "DdsClientOptionResolver.hpp"
#include "dds_cpp/api/DdsConfigurationException.hpp"
#include "dds_cpp/api/DdsOperationException.hpp"

namespace dds_cpp::detail {
inline void ValidateMulticastAddress(const std::string& value)
{
    std::array<int, 4> octets{};
    char separator1 = 0, separator2 = 0, separator3 = 0;
    std::istringstream input(value);
    if (!(input >> octets[0] >> separator1 >> octets[1] >> separator2 >> octets[2] >> separator3 >> octets[3]) ||
        separator1 != '.' || separator2 != '.' || separator3 != '.' ||
        octets[0] < 224 || octets[0] > 239 ||
        octets[1] < 0 || octets[1] > 255 || octets[2] < 0 || octets[2] > 255 ||
        octets[3] < 0 || octets[3] > 255) {
        throw DdsConfigurationException(
            "'" + value + "'은(는) 224.0.0.0/4 범위의 IPv4 멀티캐스트 주소여야 합니다.");
    }
    input >> std::ws;
    if (!input.eof()) {
        throw DdsConfigurationException(
            "'" + value + "'은(는) 224.0.0.0/4 범위의 IPv4 멀티캐스트 주소여야 합니다.");
    }
}

class RtiReaderQosOverride final {
public:
    explicit RtiReaderQosOverride(const std::string& configured_path, std::string multicast_address = {})
        : path_(DdsClientOptionResolver::ResolveRequiredFile(
              configured_path,
              "definitions/qos_profiles.xml",
              "qos_profiles_xml_path"))
    {
        multicast_address_ = std::move(multicast_address);
        if (!multicast_address_.empty()) ValidateMulticastAddress(multicast_address_);
    }

    dds::sub::qos::DataReaderQos ReaderQos(dds::core::QosProvider& provider,
        const std::string& profile) const
    {
        auto qos = provider.datareader_qos(profile);
        if (multicast_address_.empty()) return qos;
        auto policy = qos.policy<rti::core::policy::TransportMulticast>();
        auto settings = policy.settings();
        if (settings.size() != 1)
            throw DdsConfigurationException("Profile '" + profile + "' 의 멀티캐스트 수신 설정이 하나여야 덮어쓸 수 있습니다. 여러 주소는 QoS XML에 직접 설정하세요.");
        for (auto& setting : settings) setting.receive_address(multicast_address_);
        policy.settings(settings);
        qos << policy;
        return qos;
    }

    [[nodiscard]] const std::string& path() const noexcept { return path_; }

private:
    std::string path_;
    std::string multicast_address_;
};

} // namespace dds_cpp::detail
