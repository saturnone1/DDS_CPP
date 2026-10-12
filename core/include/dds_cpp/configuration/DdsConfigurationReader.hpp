#pragma once

#include <optional>
#include <string>

#include "dds_cpp/configuration/DdsClientOptions.hpp"
#include "dds_cpp/configuration/DdsConfiguration.hpp"
#include "dds_cpp/api/Visibility.hpp"

namespace dds_cpp {

class DDSCPP_API DdsConfigurationReader final {
public:
    static DdsConfiguration Load(const DdsClientOptions& options);
    static DdsConfiguration Load(
        const std::optional<std::vector<TopicDefinition>>& topics,
        const std::string& qos_profiles_xml_path,
        std::vector<std::shared_ptr<const DdsMessageModule>> modules = {});
};

} // namespace dds_cpp
