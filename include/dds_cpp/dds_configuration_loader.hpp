#pragma once

#include <optional>
#include <string>

#include "dds_cpp/dds_client_options.hpp"
#include "dds_cpp/dds_configuration.hpp"
#include "dds_cpp/visibility.hpp"

namespace dds_cpp {

class DDSCPP_API DdsConfigurationLoader final {
public:
    static DdsConfiguration load(const DdsClientOptions& options);
    static DdsConfiguration load(
        const std::string& topics_xml_path,
        const std::string& qos_profiles_xml_path,
        const std::optional<std::string>& dds_sim_xml_path = std::nullopt);
};

} // namespace dds_cpp
