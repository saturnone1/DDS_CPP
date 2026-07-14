# DDS NuGet / DDSCPP API parity

`DdsAmbassador.DDSClient` is the authoritative functional contract. DDSCPP
provides the same workflow and names with normal C++ ownership syntax. Existing
snake_case methods remain compatibility aliases.

실제 프로젝트 연결과 전체 예제는 [DDSCPP 라이브러리 사용법](library-usage.md),
C# 상세 API는 [NuGet API 문서](../../DDS_Nuget/docs/api.md)를 참고합니다.

| NuGet | DDSCPP | Contract |
|---|---|---|
| `DdsClient.Connect()` | `DdsClient::Connect()` | Load default config and connect |
| `DdsClient.Connect(path)` | `DdsClient::Connect(path)` | Load the selected client XML |
| `DdsClient.Connect(options)` | `DdsClient::Connect(options)` | Connect with explicit options |
| `CreatePublisher<T>()` | `CreatePublisher<T>()` | Resolve topic from `T`, validate direction, create writer |
| `CreateSubscriber<T>(handler)` | `CreateSubscriber<T>(handler)` | Resolve topic from `T`, validate direction, subscribe |
| `Publish<T>(sample)` | `Publish<T>(sample)` | Resolve topic from `T` and publish |
| `Subscribe<T>(handler)` | `Subscribe<T>(handler)` | Resolve topic from `T` and subscribe |
| `Publish(topic, sample)` | `Publish(topic, sample)` | Publish to an explicit topic |
| `Subscribe(topic, type, handler)` | `Subscribe<T>(topic, handler)` | Subscribe to an explicit topic and type |
| `IDdsPublisher<T>.Topic` | `DdsPublisher<T>::Topic()` | Return the configured topic |
| `IDdsPublisher<T>.Publish` | `DdsPublisher<T>::Publish` | Reuse the created publisher |
| `IDisposable` subscription | `std::unique_ptr<Subscription>` | Unsubscribe on disposal/destruction |
| `DdsClient.Dispose()` | `DdsClient::Dispose()` / destructor | Close the participant; later operations fail clearly |

Configuration names, XML files, environment overrides, direction rules,
exception categories, generated `MSG` types, participant/writer/reader QoS,
initial peers, multicast override, and Debug TX/RX logging follow the same
contract. C# properties map to C++ getter/setter functions such as
`options.DomainId()` and `topic.QualifiedQosProfile()`.

Both implementations use the same predictable configuration rule: no-argument
or path-based `Connect` loads XML and then environment overrides, while
`Connect(options)` treats the supplied options as authoritative and does not
read environment variables a second time.

The companion CLI is also mirrored: `list`, `shell`/`daemon`, `idle`/`wait`,
`publish`/`send`, `subscribe`/`listen`, JSON file/stdin input, persistent shell
subscriptions, and unsubscribe commands are provided by the C++ `ddsclient`
executable. Both standalone CLIs also support `--repeat`, `--interval-ms`,
`--count`, and `--timeout-ms` for deterministic automation.

A publisher handle is valid only while its creating client is open in both
languages. Reconnection is explicit and simple: dispose/destroy the old client,
then call `Connect` again with the desired options.

## Equivalent usage

NuGet:

```csharp
using var client = DdsClient.Connect();
using var subscription = client.Subscribe<TimeTickInformation>(Handle);
client.Publish(new TimeTickInformation());
```

DDSCPP:

```cpp
auto client = dds_cpp::DdsClient::Connect();
auto subscription = client.Subscribe<MSG::TimeTickInformation>(Handle);
client.Publish(MSG::TimeTickInformation{});
```

Both resolve the topic as `TimeTickInformation` from the unqualified message
type name and apply the matching `topics.xml` entry.
