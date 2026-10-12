#pragma once
namespace dds_cpp {
/// Closed는 종료 요청 수락이 아니라 네이티브 자원 파괴 완료를 뜻한다. 정리 실패는 Faulted로 구분한다.
enum class DdsShutdownState { Open, Closing, Closed, Faulted };
}
