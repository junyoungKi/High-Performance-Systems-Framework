# DX12 High-Performance Renderer

> **"A high-performance, low-level DirectX 12 renderer designed to maximize hardware utilization through explicit resource management and multi-threaded command recording via a custom Job System."**

---

## 🛠 Core Engineering Principles

* **Explicit Hardware Control:** Bypassing driver abstraction for direct GPU memory and synchronization management.
* **Modern Threading Model:** Parallelized command list generation to eliminate CPU-side bottlenecks.
* **Static Analysis Driven:** Zero-warning build achieved through rigorous memory diagnostics and SAL annotations.





###### **Day 1: 프레임워크 기초 및 정적 분석 경고 해결**

- **핵심 성과:** 유니코드(Unicode) 지원 Win32 윈도우 초기화 성공 및 MSVC 정적 분석 기준 '경고 제로' 상태의 안정적인 DX12 환경 구축

#### **Technical Challenge 1: 문자열 인코딩 충돌 (Unicode vs ANSI)**

- **문제 (Problem):** `OutputDebugString` 및 `sprintf_s` 호출 시 `char*`와 `LPCWSTR` 간의 형 변환 에러(`C2664`) 발생

- **원인 분석 (Cause):** 현대적인 윈도우 개발 환경(UTF-16 Unicode)과 레거시 튜토리얼 코드(ANSI/char) 간의 데이터 규격 불일치

- **해결 방안 (Resolution):** 1바이트 기반 함수를 와이드 문자용(`swprintf_s`)으로 교체하고, 문자열 리터럴에 `L` 매크로를 적용하여 해결

- **엔지니어링 의사결정 (Decision):** 하위 호환성보다는 글로벌 표준인 유니코드를 프로젝트 전체 표준으로 채택하여 현대적 API와의 호환성 및 확장성 확보

#### **Technical Challenge 2: 외부 라이브러리(`d3dx12.h`)의 정적 분석 경고 관리**

- **문제 (Problem):** MS 헬퍼 라이브러리 포함 시 `HeapAlloc` 및 `Union` 구조체 관련 '초기화되지 않은 메모리 사용' 경고(C6001/C6387) 발생

- **원인 분석 (Cause):** 성능 최적화를 위해 메모리 제로화(Zero-init)를 생략한 라이브러리의 설계 특성과 정적 분석기의 엄격한 검사가 충돌하여 발생하는 **오탐(False Positive)**임을 파악

- **해결 방안: 고지된 수용 (Informed Acceptance).** 해당 경고가 `switch` 문 등에 의해 논리적으로 보호되고 있음을 확인한 후, 라이브러리 원본 수정 없이 유지

- **엔지니어링 의사결정 (Decision):** - **공급자 무결성(Vendor Integrity):** 외부 소스 코드를 임의 수정할 경우 향후 SDK 업데이트나 이식 시 코드 일관성이 깨질 위험이 큼
  
  - **실익 분석(Risk-Benefit):** 경고를 없애기 위해 불필요한 성능 저하(Zero-init 비용)를 발생시키거나 표준 헤더와의 호환성을 깨는 것보다, 원본을 유지하는 것이 유지보수 측면에서 유리하다고 판단함
  
  - **의도적인 선택:** 단순히 경고를 방치한 것이 아니라, 원인을 분석하고 '수정하지 않는 실익'이 더 크다는 결론 하에 내린 전략적 선택임

#### **Technical Challenge 3: `LoadIconW` 함수의 리소스 로딩 모호성**

- **문제 (Problem):** 아이콘 로드 시 인자 값이 `0`일 수 있다는 정적 분석 경고(C6387) 발생

- **원인 분석 (Cause):** 사용자 정의 인스턴스 핸들(`hInst`)로 시스템 아이콘을 요청하여 발생한 논리적 모순. 윈도우의 자동 대체(Fail-safe) 기능 덕분에 작동은 하나 잠재적 위험 내포

- **해결 방안 (Resolution):** 인스턴스 핸들을 `nullptr`로 명시하고 `IDI_APPLICATION` 식별자를 사용하여 해결

- **엔지니어링 통찰 (Insight):** "작동만 하는 코드"에서 "올바른 코드"로의 전환. 로우레벨 개발에서는 운영체제의 자비에 기대지 않고 모든 자원 핸들을 명확하고 예측 가능한 상태로 제어해야 함을 학습

### **📈 Day 1 요약: 화면 출력 기반 완성**

![Rendering Result](./images/2026-01-03-screen_complete.png)

---









나중에 쓸거 : 

Utilized Microsoft WRL ComPtr for robust lifetime management of D3D12 objects, preventing potential memory leaks in low-level resource handling.
