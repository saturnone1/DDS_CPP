# DDSCPP 라이브러리 사용법

DDSCPP는 C++20 애플리케이션에서 RTI Connext DDS를 간단한 typed API로
사용하기 위한 라이브러리입니다. C# `DdsAmbassador.DDSClient`와 같은 설정,
topic 이름, 메시지 정의, QoS 및 API 흐름을 사용합니다.

처음 사용하는 개발자는 [XML 설정 안내서](configuration.md)도 함께 읽으세요.
세 가지 `dds_client` 파일의 차이, 선택 우선순위, 편집 가능한 항목과 재빌드가
필요한 항목을 예제와 함께 설명합니다.

## 1. 준비 사항

- C++20 컴파일러와 CMake 3.20 이상
- RTI Connext DDS 7.3.1 설치 및 유효한 라이선스
- Windows는 Visual Studio, Linux는 GCC와 RTI Linux target package
- 공통 정의 파일이 있는 `DDS_Nuget/definitions` 디렉터리

RTI 위치는 다음 순서로 지정할 수 있습니다.

```powershell
$env:NDDSHOME = "C:\Program Files\rti_connext_dds-7.3.1"
```

```bash
export NDDSHOME=/opt/rti_connext_dds-7.3.1
```

또는 CMake에 `-DDDSCPP_RTI_HOME=<path>`를 전달합니다.

## 2. 프로젝트에 연결

소스 트리를 같이 빌드하는 방식이 가장 간단합니다.

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyDdsApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(DDSCPP_ENABLE_RTI ON CACHE BOOL "" FORCE)
set(DDSCPP_DEFINITIONS_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../DDSClient/DDS_Nuget/definitions"
    CACHE PATH "" FORCE)

add_subdirectory(../DDSClient/DDSCPP DDSCPP-build)

add_executable(my_dds_app main.cpp)
target_link_libraries(my_dds_app PRIVATE dds_cpp_shared)
```

정적 라이브러리를 원하면 `dds_cpp_shared` 대신 `dds_cpp_static`을 연결합니다.
공개 헤더는 target을 통해 자동으로 전달됩니다.

이미 설치된 Rocky 9.7 배포 묶음을 직접 사용할 때는 다음 파일이 필요합니다.

- `include/`: DDSCPP 및 생성된 RTI 타입 헤더
- `lib64/libdds_cpp.so`: DDSCPP 공유 라이브러리
- `lib64/libnddscpp2.so`, `libnddsc.so`, `libnddscore.so`: RTI runtime
- `share/dds_cpp/definitions/`: 공통 XML 설정

실행 시 라이브러리를 찾지 못하면 설치 prefix의 `lib64`를 등록합니다.

```bash
export LD_LIBRARY_PATH=/opt/ddscpp/lib64:$LD_LIBRARY_PATH
```

## 3. 가장 간단한 송수신

```cpp
#include <dds_cpp/dds_cpp.hpp>

#include <iostream>

int main()
{
    auto client = dds_cpp::DdsClient::Connect(
        "definitions/dds_client.rti-multicast.xml");

    auto subscription = client.Subscribe<MSG::TimeTickInformation>(
        [](const MSG::TimeTickInformation& sample) {
            std::cout << "received tick="
                      << sample.TimeTickMessage().TimeTick() << '\n';
        });

    MSG::TimeTickInformation sample;
    sample.Header().MsgID(ENUM::MessageID::TimeTickInformation);
    sample.Header().SrcSimID(ENUM::SimulatorID::TCC);
    sample.Header().DstSimID(ENUM::SimulatorID::TDS);
    sample.TimeTickMessage().TimeTick(1);
    sample.TimeTickMessage().SyncCycle(0);

    client.Publish(sample);
}
```

`Publish(sample)`과 `Subscribe<T>()`는 타입 이름 `T`에서 topic 이름을
결정합니다. 따라서 `MSG::TimeTickInformation`은 `topics.xml`의
`TimeTickInformation` 항목과 연결됩니다.

라이브러리는 사용자 메시지의 `Header` 값을 임의로 변경하지 않습니다.
필요한 `MsgID`, `SrcSimID`, `DstSimID`, timestamp는 애플리케이션에서
설정해야 합니다. 기본 샘플을 만드는 CLI만 편의를 위해 공통 헤더를 채웁니다.

## 4. 연결 방법

### 기본 설정 사용

```cpp
auto client = dds_cpp::DdsClient::Connect();
```

실행 파일 주변, 현재 디렉터리와 상위 디렉터리, 설치된
`share/dds_cpp/definitions`에서 `definitions/dds_client.xml`을 찾습니다.
현재 기본 파일의 transport는 `InMemory`이므로 다른 프로세스나 장비와 실제
DDS 통신을 하지 않습니다. 실제 통신에는 다음 절의 RTI 설정 파일을 명시하세요.

### 설정 파일 지정

```cpp
auto client = dds_cpp::DdsClient::Connect(
    "/app/definitions/dds_client.asap.xml");
```

XML을 읽은 뒤 환경 변수가 XML 값을 덮어씁니다.

### 옵션 직접 지정

```cpp
dds_cpp::DdsClientOptions options;
options.DomainId() = 20;
options.UseRtiTransport() = true;
options.ParticipantName() = "MySimulator";
options.TopicsXmlPath() = "/app/definitions/topics.xml";
options.QosProfilesXmlPath() = "/app/definitions/qos_profiles.xml";
options.DdsSimXmlPath() = "/app/definitions/DDSSim.xml";
options.InitialPeers() = {
    "[0]@builtin.udpv4://dds-discovery.default.svc.cluster.local"
};

auto client = dds_cpp::DdsClient::Connect(options);
```

`Connect(options)`는 전달된 옵션을 최종값으로 취급하며 환경 변수를 다시
읽지 않습니다. 빈 정의 경로만 기본 정의 탐색을 사용합니다.

## 공개 API 요약

| API | 용도 |
|---|---|
| `DdsClient::Connect()` | 기본 XML과 환경 변수를 읽어 연결 |
| `DdsClient::Connect(path)` | 지정한 client XML을 읽어 연결 |
| `DdsClient::Connect(options)` | 직접 구성한 최종 옵션으로 연결 |
| `CreatePublisher<T>()` | 재사용 가능한 typed publisher 생성 |
| `CreateSubscriber<T>(handler)` | typed subscription 생성 |
| `Publish<T>(sample)` | 타입 이름으로 topic을 찾아 한 샘플 송신 |
| `Publish<T>(topic, sample)` | 명시한 topic으로 한 샘플 송신 |
| `Subscribe<T>(handler)` | 타입 이름으로 topic을 찾아 구독 |
| `Subscribe<T>(topic, handler)` | 명시한 topic을 구독 |
| `Options()` | 연결에 사용된 최종 옵션 조회 |
| `Configuration()` | topic 목록과 direction/QoS 조회 |
| `Dispose()` / `Close()` / `close()` | participant와 transport 종료 |

`DdsClientOptions`는 C# 속성과 같은 이름의 getter/setter를 제공합니다.

| 옵션 | C++ 접근자 | 기본값/의미 |
|---|---|---|
| Domain | `DomainId()` | `0` |
| Transport | `UseRtiTransport()` | `false`; 실제 DDS는 `true` |
| Participant | `ParticipantName()` | 빈 이름 |
| Topics | `TopicsXmlPath()` | 빈 값이면 기본 파일 탐색 |
| QoS | `QosProfilesXmlPath()` | 빈 값이면 기본 파일 탐색 |
| 메시지 정의 | `DdsSimXmlPath()` | 빈 값이면 기본 파일 탐색 |
| Discovery | `InitialPeers()` | 빈 목록이면 RTI 기본 discovery |
| Log | `LogLevel()` | `DdsLogLevel::None` |

## 5. Publish API

한두 번 송신할 때는 client에서 직접 호출합니다.

```cpp
MSG::SimulatorStatus sample;
client.Publish(sample);
```

반복 송신할 때는 publisher handle을 재사용할 수 있습니다.

```cpp
auto publisher = client.CreatePublisher<MSG::TimeTickInformation>();

MSG::TimeTickInformation sample;
for (std::uint64_t tick = 0; tick < 100; ++tick) {
    sample.TimeTickMessage().TimeTick(tick);
    publisher.Publish(sample);
}
```

명시적 topic overload도 제공합니다.

```cpp
client.Publish("TimeTickInformation", sample);
client.create_publisher<MSG::TimeTickInformation>("TimeTickInformation");
```

publisher handle은 자신을 만든 client가 열려 있는 동안만 유효합니다.

## 6. Subscribe API

```cpp
auto subscription = client.Subscribe<MSG::AirThreatInformation>(
    [](const MSG::AirThreatInformation& sample) {
        // 수신 처리
    });
```

반환된 `std::unique_ptr<Subscription>`이 살아 있는 동안 구독이 유지됩니다.
구독을 중지하려면 다음처럼 파괴합니다.

```cpp
subscription.reset();
```

명시적 topic 이름을 사용할 수도 있습니다.

```cpp
auto subscription = client.Subscribe<MSG::TimeTickInformation>(
    "TimeTickInformation",
    [](const MSG::TimeTickInformation& sample) {
        // 수신 처리
    });
```

callback은 DDS 수신 처리 흐름에서 호출되므로 오래 block하지 않는 것이
좋습니다. 무거운 처리는 별도 queue나 작업 thread로 넘기고, 공유 데이터는
mutex 등으로 보호합니다.

## 7. 설정 및 topic 조회

```cpp
std::cout << client.Options().DomainId() << '\n';

for (const auto& topic : client.Configuration().Topics()) {
    std::cout << topic.Name() << ' '
              << topic.QualifiedQosProfile() << '\n';
}

const dds_cpp::TopicDefinition* topic = nullptr;
if (client.Configuration().TryGetTopic("TimeTickInformation", topic)) {
    std::cout << topic->Name() << '\n';
}
```

`topics.xml`의 direction 규칙은 API 호출 시 검사됩니다.

- `Both`: publish와 subscribe 허용
- `Publish`: publish만 허용
- `Subscribe`: subscribe만 허용

정책을 위반하면 `DdsOperationException`이 발생합니다.

## 8. 종료와 재연결

일반적으로 scope 종료 시 destructor가 participant를 닫습니다.

```cpp
{
    auto client = dds_cpp::DdsClient::Connect();
    // 사용
}
```

명시적으로 닫을 수도 있습니다.

```cpp
client.Dispose(); // close(), Close()도 동일
```

종료 후 기존 client나 publisher를 사용하면 명확한 예외가 발생합니다.
재연결은 새 client를 만드는 방식입니다.

```cpp
client.Dispose();
auto reconnected = dds_cpp::DdsClient::Connect(options);
```

## 9. 설정 파일 수정

공통 정의는 `DDS_Nuget/definitions`에서 관리합니다.

- `DDSSim.xml`: enum, struct, 메시지 정의
- `topics.xml`: topic, QoS profile, direction
- `qos_profiles.xml`: RTI writer/reader QoS
- `dds_client*.xml`: domain, transport, participant 및 discovery peer

메시지를 추가할 때는 `DDSSim.xml`의 `MSG::X`와 `topics.xml`의 topic `X`를
같은 이름으로 추가한 뒤 CMake를 다시 빌드합니다. CMake가 `rtiddsgen`과
topic registry 생성을 자동 실행합니다.

```powershell
cmake --build DDSCPP/build --config Release
ctest --test-dir DDSCPP/build -C Release --output-on-failure
```

## 10. 환경 변수

`Connect()` 또는 `Connect(path)`에서 사용할 수 있습니다.

| 변수 | 의미 |
|---|---|
| `DDS_CLIENT_CONFIG_PATH` | 선택할 `dds_client.xml` |
| `DDS_DOMAIN_ID` | DDS domain id |
| `DDS_USE_RTI_TRANSPORT` | `true` 또는 `false` |
| `DDS_PARTICIPANT_NAME` | participant 이름 |
| `DDS_INITIAL_PEERS` | 세미콜론으로 구분한 discovery peers |
| `DDS_TOPICS_XML_PATH` | `topics.xml` 경로 |
| `DDS_QOS_PROFILES_XML_PATH` | `qos_profiles.xml` 경로 |
| `DDS_DDSSIM_XML_PATH` | `DDSSim.xml` 경로 |
| `DDS_LOG_LEVEL` | `None`, `Error`, `Info`, `Debug` |

`DDS_INITIAL_PEERS=default` 또는 `none`은 명시적 peer 목록을 비우고 RTI
기본 discovery를 사용합니다.

## 11. 예외 처리

```cpp
try {
    auto client = dds_cpp::DdsClient::Connect();
    client.Publish(sample);
} catch (const dds_cpp::DdsConfigurationException& ex) {
    // XML, 파일 경로, topic/QoS 정의 오류
} catch (const dds_cpp::DdsOperationException& ex) {
    // 종료된 client, direction 위반, DDS 실행 오류
} catch (const dds_cpp::DdsException& ex) {
    // 그 밖의 DDSCPP 오류
}
```

문제 분석 시 `DDS_LOG_LEVEL=Debug`를 사용하면 TX/RX 메시지와 writer match
상태를 확인할 수 있습니다.

## 12. 권장 사용 패턴

- 프로세스 또는 서비스 단위로 `DdsClient` 하나를 오래 유지합니다.
- 반복 송신은 `CreatePublisher<T>()` 결과를 재사용합니다.
- subscription handle을 필요한 기간 동안 보관합니다.
- callback 안에서는 짧게 처리하고 무거운 작업을 queue로 넘깁니다.
- 메시지 정의를 바꾸면 C#과 C++을 모두 재생성·재빌드하고 parity test를 실행합니다.
- 운영 환경에서는 `dds_client.xml`을 기본값으로 두고 domain/peer만 환경 변수로 덮어씁니다.
