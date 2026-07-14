# DDSClient XML 설정 안내서

이 문서는 DDSClient를 처음 사용하는 개발자가 `definitions` 폴더의 XML 파일을
보고도 각 파일의 역할과 선택 방법을 이해할 수 있도록 설명합니다. C# NuGet과
C++ 라이브러리는 같은 XML 구조와 같은 우선순위를 사용합니다.

## 먼저 알아둘 핵심

`dds_client.xml`, `dds_client.asap.xml`,
`dds_client.rti-multicast.xml`은 서로 다른 메시지 형식이 아닙니다. 모두 같은
`<dds_client>` 형식으로 작성된 **실행 환경별 연결 설정 프리셋**입니다.

프로그램은 이 세 파일을 합치거나 동시에 읽지 않습니다. 실행할 때 하나만
선택합니다. 별도 파일을 선택하지 않으면 `dds_client.xml`을 찾습니다.

> 현재 제공되는 `dds_client.xml`의 기본 transport는 `InMemory`입니다.
> 따라서 인자를 주지 않고 `Connect()`만 호출하면 같은 프로세스 안에서만
> 동작하며 다른 PC나 컨테이너의 RTI DDS participant와 통신하지 않습니다.
> 실제 DDS 통신에는 `dds_client.rti-multicast.xml` 또는
> `dds_client.asap.xml`을 명시적으로 선택하세요.

## definitions 폴더의 파일 구분

처음에는 모든 XML이 비슷해 보이지만 담당하는 범위가 다릅니다.

| 파일 | 정하는 내용 | 단순 편집 후 재실행 | 코드 재생성/재빌드 |
|---|---|---:|---:|
| `dds_client*.xml` | domain, transport, participant, discovery peer, 다른 XML 경로 | 가능 | 불필요 |
| `topics.xml` | 사용할 topic, publish/subscribe 방향, QoS profile 연결 | 가능 | 새 메시지 타입이 아니면 불필요 |
| `qos_profiles.xml` | reliability, history, multicast receive address 등 RTI QoS | 가능 | 불필요 |
| `DDSSim.xml` | 메시지 struct, field, enum과 자료형 | 불가능 | C#과 C++ 모두 재생성·재빌드 필요 |

즉 `dds_client*.xml`의 이름이 여러 개인 이유는 네트워크 환경을 쉽게 바꾸기
위해서입니다. 메시지 구조를 여러 벌 관리하기 위한 파일이 아닙니다.

## 어떤 client 설정을 선택해야 하나요?

| 상황 | 선택할 파일 | 실제 네트워크 DDS | discovery 방식 |
|---|---|---:|---|
| 단위 테스트 또는 한 프로세스 안의 기능 확인 | `dds_client.xml` | 아니요 | 해당 없음 |
| 같은 LAN에서 일반 RTI participant끼리 통신 | `dds_client.rti-multicast.xml` | 예 | RTI 기본 discovery, 일반적으로 multicast |
| Kubernetes에서 ASAP discovery relay 사용 | `dds_client.asap.xml` | 예 | 지정된 ASAP peer |

판단 방법은 간단합니다.

1. 다른 프로세스나 다른 장비와 통신해야 하지 않으면 `dds_client.xml`을 사용합니다.
2. 실제 DDS 통신이고 Kubernetes/ASAP 환경이 아니면
   `dds_client.rti-multicast.xml`을 사용합니다.
3. Kubernetes에서 discovery relay 서비스 주소를 받았다면
   `dds_client.asap.xml`을 사용합니다.

## 세 설정 파일의 정확한 차이

### dds_client.xml

개발과 단위 테스트를 위한 기본 파일입니다.

```xml
<transport>InMemory</transport>
```

`InMemory` transport는 RTI participant를 만들지 않습니다. 같은
`DdsClient` transport 안에서 publish/subscribe API 흐름을 검사할 때 유용하지만
실제 DDS 연결 시험에는 사용할 수 없습니다.

인자 없는 `Connect()`가 자동으로 찾는 파일이므로, 통신 시험에서 아무 메시지도
오지 않을 때는 가장 먼저 이 파일의 `<transport>` 값을 확인하세요.

### dds_client.rti-multicast.xml

일반 RTI DDS 통신용 파일입니다.

```xml
<transport>Rti</transport>
```

`<initial_peers>`가 없으므로 RTI 기본 discovery 설정을 사용합니다. 네트워크에서
multicast discovery가 허용되어 있으면 같은 domain의 participant들이 서로를
자동으로 찾습니다.

이 파일 이름의 `multicast`는 discovery 환경을 알아보기 쉽게 표시한 프리셋
이름입니다. 실제 user data multicast 주소는 `qos_profiles.xml`의
`multicast/receive_address`가 정합니다.

### dds_client.asap.xml

Kubernetes에서 ASAP discovery relay를 사용할 때 선택합니다.

```xml
<transport>Rti</transport>
<initial_peers>
  <peer>[0]@builtin.udpv4://dds-discovery.default.svc.cluster.local</peer>
</initial_peers>
```

Pod가 multicast discovery를 직접 사용하기 어려운 환경에서 지정된 peer를 통해
상대 participant를 찾습니다. 예제의 service DNS, namespace와 domain ID는 실제
배포 환경에 맞게 수정해야 합니다.

이 프리셋은 문제 확인을 쉽게 하도록 `Debug` 로그가 설정되어 있습니다. 운영에서
로그가 너무 많으면 `<log_level>Info</log_level>` 또는 `None`으로 변경할 수
있습니다.

## 설정 파일을 선택하는 방법

### C# 코드에서 명시

```csharp
using var client = DdsClient.Connect(
    "definitions/dds_client.rti-multicast.xml");
```

### C++ 코드에서 명시

```cpp
auto client = dds_cpp::DdsClient::Connect(
    "definitions/dds_client.rti-multicast.xml");
```

### 환경변수로 선택

애플리케이션 코드를 환경별로 바꾸고 싶지 않을 때 사용합니다.

Windows PowerShell:

```powershell
$env:DDS_CLIENT_CONFIG_PATH = "C:\app\definitions\dds_client.rti-multicast.xml"
```

Linux 또는 컨테이너:

```bash
export DDS_CLIENT_CONFIG_PATH=/app/definitions/dds_client.asap.xml
```

### 아무것도 지정하지 않는 경우

C#과 C++ 모두 다음과 같이 호출하면 기본 파일을 탐색합니다.

```text
definitions/dds_client.xml
share/dds_cpp/definitions/dds_client.xml  # C++ 설치 배치
```

배포 위치에 따라 탐색 후보가 조금 다르지만 파일 이름은 항상
`dds_client.xml`입니다. 현재 이 파일은 `InMemory`이므로 실제 DDS 통신이
필요하면 프리셋을 명시하는 것이 안전합니다.

## 설정 적용 우선순위

설정 파일 선택 우선순위는 다음과 같습니다.

1. `Connect("파일 경로")`에 직접 전달한 경로
2. `DDS_CLIENT_CONFIG_PATH` 환경변수
3. 자동 탐색한 `definitions/dds_client.xml`
4. 파일을 찾지 못하면 코드 기본값과 환경변수로 옵션 구성

파일을 읽은 뒤에는 개별 환경변수가 XML 값을 덮어씁니다. 예를 들어 XML의
`domain_id`가 `0`이어도 `DDS_DOMAIN_ID=20`이면 최종 domain은 `20`입니다.

단, `Connect(options)`처럼 코드에서 `DdsClientOptions`를 직접 만들어 전달하면
그 객체가 최종 설정입니다. 이 경로에서는 환경변수를 다시 읽지 않습니다.

## dds_client XML 항목 설명

```xml
<?xml version="1.0" encoding="UTF-8"?>
<dds_client>
  <domain_id>0</domain_id>
  <transport>Rti</transport>
  <log_level>Info</log_level>
  <participant_name>MySimulator</participant_name>
  <topics_xml_path>topics.xml</topics_xml_path>
  <qos_profiles_xml_path>qos_profiles.xml</qos_profiles_xml_path>
  <dds_sim_xml_path>DDSSim.xml</dds_sim_xml_path>
  <initial_peers>
    <peer>[0]@builtin.udpv4://dds-discovery.default.svc.cluster.local</peer>
  </initial_peers>
</dds_client>
```

| 항목 | 설명 | 예시/주의사항 |
|---|---|---|
| `domain_id` | 서로 통신할 DDS 논리 domain | 송신자와 수신자가 같은 값이어야 함 |
| `transport` | `InMemory` 또는 `Rti` | 실제 네트워크 통신은 반드시 `Rti` |
| `log_level` | `None`, `Error`, `Info`, `Debug` | `Debug`는 메시지 내용까지 많이 출력할 수 있음 |
| `participant_name` | 모니터링 도구에 표시될 participant 이름 | 서비스/시뮬레이터별로 구분 가능한 이름 권장 |
| `topics_xml_path` | topic 목록 파일 | 상대 경로는 선택한 client XML 폴더 기준 |
| `qos_profiles_xml_path` | RTI QoS 파일 | 상대 경로는 선택한 client XML 폴더 기준 |
| `dds_sim_xml_path` | 메시지 원본 정의 | topic 및 생성 타입 검증에 사용 |
| `initial_peers` | discovery를 시작할 상대 주소 목록 | 생략하면 RTI default discovery 사용 |

상대 경로를 사용하면 `definitions` 폴더 전체를 다른 위치로 옮겨도 내부 참조가
유지됩니다. 따라서 세 XML 경로는 특별한 이유가 없다면 파일 이름만 적는 방식을
권장합니다.

## discovery multicast와 user data multicast

두 개념은 서로 다릅니다.

- **Discovery**: participant와 endpoint가 서로 존재를 찾는 과정입니다.
- **User data**: discovery가 끝난 뒤 실제 DDS sample을 보내는 과정입니다.

`dds_client.rti-multicast.xml`에서 `<initial_peers>`를 비우는 것은 RTI 기본
discovery를 선택한다는 뜻입니다. 반면 `qos_profiles.xml`의 multicast
`receive_address`는 user data 수신 주소입니다. discovery가 성공해도 방화벽이나
CNI가 user data multicast를 막으면 sample은 전달되지 않을 수 있습니다.

현재 기본 user data multicast group은 다음과 같습니다.

```text
239.255.5.1
```

환경별로 변경할 때는 XML을 직접 수정하거나 다음 환경변수를 사용합니다.

```bash
DDS_MULTICAST_ADDRESS=239.255.10.20
```

## 자주 쓰는 환경변수

| 환경변수 | 덮어쓰는 값 |
|---|---|
| `DDS_CLIENT_CONFIG_PATH` | 선택할 client 설정 파일 |
| `DDS_DOMAIN_ID` | domain ID |
| `DDS_USE_RTI_TRANSPORT` | `true`이면 실제 RTI transport 사용 |
| `DDS_PARTICIPANT_NAME` | participant 이름 |
| `DDS_LOG_LEVEL` | 로그 수준 |
| `DDS_INITIAL_PEERS` | `;`로 구분한 discovery peer 목록 |
| `DDS_MULTICAST_ADDRESS` | user data multicast receive address |
| `DDS_TOPICS_XML_PATH` | topics XML 경로 |
| `DDS_QOS_PROFILES_XML_PATH` | QoS XML 경로 |
| `DDS_DDSSIM_XML_PATH` | 메시지 정의 XML 경로 |

`DDS_INITIAL_PEERS=none` 또는 `default`는 XML의 peer 목록을 지우고 RTI 기본
discovery를 사용하겠다는 뜻입니다.

## 배포 패키지에 같은 XML이 여러 번 보이는 이유

개발자 배포 패키지에는 Windows, Rocky 9.7, Rocky 10 결과가 각각 독립적으로
들어 있습니다. 어느 플랫폼 폴더 하나만 복사해도 실행할 수 있게 하려고 같은
`definitions`를 각 플랫폼에 복사했습니다.

```text
Cpp/windows-x64/definitions
Cpp/rocky9.7-x64/definitions
Cpp/rocky10-x64/definitions
```

내용이 다른 세 벌의 메시지 규격이 아니라 배포 편의를 위한 동일한 복사본입니다.
실제로 배포하는 대상 플랫폼 폴더의 파일만 수정하면 됩니다. 원본 프로젝트에서
공통 변경을 관리할 때는 `DDS_Nuget/definitions`를 기준으로 수정하고 패키지를
다시 만드는 것이 안전합니다.

## 변경 후 재빌드가 필요한 경우

다음 변경은 XML을 수정하고 프로그램을 다시 시작하는 것으로 충분합니다.

- domain ID 변경
- participant 이름 변경
- RTI/InMemory transport 선택
- discovery peer 변경
- 로그 수준 변경
- 기존 topic의 방향 또는 QoS profile 연결 변경
- multicast 주소와 QoS 값 변경

다음 변경은 C#과 C++ 타입 코드를 모두 다시 생성하고 라이브러리를 다시 빌드해야
합니다.

- `DDSSim.xml`에 struct 추가 또는 삭제
- field 이름이나 자료형 변경
- enum 값 변경
- 배열/sequence 구조 변경

`DDSSim.xml`만 배포 폴더에서 바꾸면 이미 컴파일된 C# DLL과 C++ 라이브러리의
타입은 바뀌지 않습니다. 이 상태로 통신하면 타입 불일치가 생길 수 있으므로
메시지 구조 변경은 반드시 원본 저장소에서 처리해야 합니다.

## 문제를 확인하는 순서

### 다른 프로그램과 전혀 통신하지 않을 때

1. 선택한 파일의 `<transport>`가 `Rti`인지 확인합니다.
2. 송신자와 수신자의 `domain_id`가 같은지 확인합니다.
3. `topics.xml`에 같은 topic이 있고 direction이 허용되는지 확인합니다.
4. discovery 환경에 맞게 default multicast 또는 ASAP peer를 선택했는지 확인합니다.
5. 방화벽, Kubernetes NetworkPolicy와 CNI의 multicast 지원을 확인합니다.
6. `DDS_LOG_LEVEL=Debug`로 올려 participant와 writer/reader match 로그를 확인합니다.

### 설정 파일을 찾지 못할 때

- 상대 경로의 기준은 현재 터미널이 아니라 선택한 `dds_client*.xml`이 있는
  폴더일 수 있다는 점을 확인합니다.
- 가장 확실한 방법은 `Connect(절대경로)` 또는
  `DDS_CLIENT_CONFIG_PATH=절대경로`를 사용하는 것입니다.
- `topics.xml`, `qos_profiles.xml`, `DDSSim.xml`이 client XML과 같은 폴더에
  있는지 확인합니다.

### 같은 PC에서는 되지만 다른 PC에서는 안 될 때

- 실수로 `dds_client.xml`의 `InMemory` 설정을 사용하지 않았는지 확인합니다.
- 두 PC가 같은 DDS domain인지 확인합니다.
- multicast discovery가 네트워크 장비에서 차단되면 initial peer 또는 discovery
  relay가 필요할 수 있습니다.
- user data multicast 주소와 방화벽 규칙도 별도로 확인합니다.

## 권장 운영 방식

- 단위 테스트에서는 기본 `dds_client.xml`을 사용합니다.
- 일반 RTI 통합시험에서는 `dds_client.rti-multicast.xml`을 코드나 환경변수로
  명시합니다.
- Kubernetes에서는 `dds_client.asap.xml`을 선택하고 service DNS와 namespace를
  배포 환경에 맞춥니다.
- 환경마다 달라지는 domain, peer, participant 이름은 환경변수로 관리합니다.
- 메시지 구조는 `DDSSim.xml` 한 곳을 원본으로 관리하고 C#과 C++을 함께
  재생성합니다.

