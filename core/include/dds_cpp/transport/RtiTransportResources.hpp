#pragma once
#include "dds_cpp/transport/RtiMessageType.hpp"
#include "RtiReaderQosOverride.hpp"
#include "DdsClientOptionResolver.hpp"
#include "DdsUnicode.hpp"
#include "DdsConfigurationReader.hpp"

namespace dds_cpp::detail {
/// 연결 단위 네이티브 자원을 소유한다. 수신·발행 작업의 종료는 전송 계층이 먼저 보장한다.
/// 선언의 역순으로 자원을 파괴하며, 마지막 witness가 실제 정리 완료를 기록한다.
class RtiTransportResources final {
private:
    CleanupWitness cleanup_witness_;
public:
    std::shared_ptr<ClientStatus> Status;
    RtiReaderQosOverride ReaderQosOverride;
    dds::core::QosProvider Provider;
    dds::domain::DomainParticipant Participant;
    dds::pub::Publisher Publisher;
    dds::sub::Subscriber Subscriber;

    RtiTransportResources(const DdsClientOptions& options, std::shared_ptr<ClientStatus> status)
        : cleanup_witness_{status}, Status(std::move(status)),
          ReaderQosOverride(options.QosProfilesXmlPath(), options.MulticastReceiveAddress()),
          Provider(RtiFilePath(ReaderQosOverride.path())),
          Participant(CreateParticipant(options, Provider, ReaderQosOverride)),
          Publisher(Participant), Subscriber(Participant) {}
private:
    static dds::domain::DomainParticipant CreateParticipant(
        const DdsClientOptions& options,
        dds::core::QosProvider& qos_provider, const RtiReaderQosOverride& effective_qos)
    {
        if (!options.MulticastReceiveAddress().empty()) {
            const auto configuration = DdsConfigurationReader::Load(options);
            for (const auto& topic : configuration.Topics())
                if (topic.direction != TopicDirection::Publish)
                    (void)effective_qos.ReaderQos(qos_provider, topic.QualifiedQosProfile());
        }
        auto qos = qos_provider.participant_qos(options.ParticipantQosProfile());
        if (!options.ParticipantName().empty()) {
            qos->participant_name.name(options.ParticipantName());
        }
        // C#과 같은 UUID 기반 정책을 사용한다. 컨테이너의 같은 IP·PID 조합을 식별자로 재사용하지 않는다.
        auto wire_protocol = qos.policy<rti::core::policy::WireProtocol>();
        wire_protocol.rtps_auto_id_kind(rti::core::policy::WireProtocolAutoKind::RTPS_AUTO_ID_FROM_UUID);
        qos << wire_protocol;
        return dds::domain::DomainParticipant(options.DomainId(), qos);
    }


};
}
