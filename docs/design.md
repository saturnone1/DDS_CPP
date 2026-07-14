# DDSCPP Design

The runtime flow is:

```text
dds_client.xml + environment
             -> DdsClientOptions
DDSSim.xml   -> rtiddsgen C++11 types
topics.xml   -> generated MSG type registry
QoS XML      -> RTI QosProvider
             -> DdsClient -> RtiDdsTransport -> DataWriter/DataReader
```

The public typed API passes the native C++ type through a type-erased transport
boundary. The RTI registry dispatches it to a `DataWriter<T>` or `DataReader<T>`
for the matching generated `MSG::X` type. This supports generated messages that
contain strings and sequences; it does not reinterpret arbitrary byte buffers
as C++ objects.

`InMemoryDdsTransport` uses the same typed boundary for deterministic tests and
also supports the explicit raw-byte API. Subscription handles retain shared
transport state so they can safely outlive a `DdsClient`.

On Windows, the generated IDL module `MSG` conflicts with the Win32 `MSG` type.
The generation step scopes a preprocessor rename to Windows/RTI header inclusion
so the public generated namespace remains `MSG`.
