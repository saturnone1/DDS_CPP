#pragma once
// 내부 접근 지원 코드이며 라이브러리 설치본에는 포함하지 않는다.
#include "dds_cpp/api/DdsClient.hpp"
#include "IDdsTransport.hpp"
namespace dds_cpp::detail {
void FailBackgroundStartForTest();
class ClientAccess {
public:
    static DdsClient Create(DdsClientOptions options, std::unique_ptr<IDdsTransport> transport)
    { return DdsClient(std::move(options), std::move(transport)); }
    static std::shared_ptr<IDdsTransport> RetainTransport(DdsClient& client) { return client.RequireTransport(); }
    static void FailNextBackgroundStart() { FailBackgroundStartForTest(); }
};
}
