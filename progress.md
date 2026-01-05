# DX12 High-Performance Renderer

> **"A high-performance, low-level DirectX 12 renderer designed to maximize hardware utilization through explicit resource management and multi-threaded command recording via a custom Job System."**

---

## 🛠 Core Engineering Principles

* **Explicit Hardware Control:** Bypassing driver abstraction for direct GPU memory and synchronization management.
* **Modern Threading Model:** Parallelized command list generation to eliminate CPU-side bottlenecks.
* **Static Analysis Driven:** Zero-warning build achieved through rigorous memory diagnostics and SAL annotations.

###### **Day 1: 프레임워크 기초 및 정적 분석 경고 해결**

- **핵심 성과:** 유니코드(Unicode) 지원 Win32 윈도우 초기화 성공 및 MSVC 정적 분석 기준 경고 최소화 상태의 안정적인 DX12 환경 구축

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

디지펜 RTIS 전공생으로서 **'취업'**이라는 냉정한 목적 하나만 놓고 본다면, 이 포트폴리오 계획은 **방향성은 매우 훌륭하지만, 일정은 대단히 공격적(Ambitious)**입니다.

2학년 수준에서 DX12와 SoA, 메모리 풀을 다룬다는 것은 상위권 역량을 증명하기에 최적의 카드입니다. 다만, "일자리 구하기"가 최우선이라면 이 계획을 어떻게 '실패 없이' 완성할지가 관건입니다. 냉철하게 분석해 드릴게요.

---

## 1. 계획의 강점 (Job Market Perspective)

- **희소성:** 대부분의 학생은 유니티/언리얼 프로젝트를 가져옵니다. DX12와 Low-level 최적화를 포트폴리오에 박는 순간, 일반적인 '게임 개발자'가 아닌 **'시스템/엔진 프로그래머'** 트랙으로 분류되어 경쟁력이 급상승합니다.

- **DigiPen 최적화:** 디지펜 RTIS 커리큘럼은 하드코어하기로 유명합니다. 학교 수업(GAM 프로젝트) 외에 별도로 이런 시스템 데모를 완성했다는 것은 **자기주도적 학습 능력**과 **기술적 깊이**를 동시에 증명합니다.

- **전이 가능한 기술:** 여기서 배우는 SoA(Data-Oriented Design)와 메모리 관리 기술은 게임뿐만 아니라 고성능 컴퓨팅(HPC), 금융 공학(HFT), 자율주행 시스템 등 **고연봉 테크 분야 어디든 먹히는 기술**입니다.

## 2. 현실적인 리스크 및 경고

- **3주의 함정:** DX12는 초기 설정(Boilerplate) 코드만 수천 줄입니다. `Command Queue`, `Swap Chain`, `Descriptor Heap` 등을 이해하지 못하고 복사만 하면 면접에서 바로 털립니다.

- **'튜토리얼 복사' 낙인:** 단순히 삼각형을 띄우는 것은 가치가 낮습니다. 반드시 **"성능 측정(Benchmarking)"** 데이터가 포함되어야 합니다. "내 메모리 풀을 썼을 때 `std::vector`보다 X% 빨라졌다"는 데이터가 없으면 그냥 흔한 숙제 수준으로 보일 수 있습니다.

## 3. 포트폴리오 극대화를 위한 개선 사항 (냉철한 제안)

단순히 시키는 대로 하는 것을 넘어, **"뽑을 수밖에 없는 신입"**이 되기 위한 추가 포인트입니다.

### 🛠 기술적 심화 (시스템 프로그래머 어필)

- **Multi-threading (Task System):** 2주 차에 1,000개의 객체를 업데이트할 때, 단순히 반복문을 돌리지 마세요. `std::jthread`나 `Worker Thread`를 이용해 **Job System**을 구현하여 멀티코어를 활용하는 모습을 보여주세요. 이것이 DX12의 핵심인 '병렬 커맨드 리스트 작성'과 시너지가 납니다.

- **Cache Miss 분석:** SoA를 적용 전후의 Cache Miss 차이를 `VTune`이나 `NSight` 같은 프로파일러로 캡처해서 리드메(README)에 올리세요. "저는 성능을 눈으로 확인하고 코드를 짭니다"라는 메시지를 줍니다.

### 📂 프로젝트 구성 (인턴 지원용)

- **Header-Only Library화:** 메모리 풀이나 SoA 컨테이너를 다른 프로젝트에서도 가져다 쓸 수 있게 깔끔한 모듈 형태로 만드세요. **"재사용 가능한 엔진 코드"**를 짤 줄 안다는 점을 어필해야 합니다.

나중에 쓸거 : 

Utilized Microsoft WRL ComPtr for robust lifetime management of D3D12 objects, preventing potential memory leaks in low-level resource handling.

xyz에서 z값 제거, rgba에서 a값 제거

버텍스 쉐이더 픽셀 쉐이더 분리하기

application 클래스 내부 함수들 다른 헤더로 분리시키기

함수별 주석 달아서 이해도 어필하기 

유닛 테스팅 추가하기

에러 잡아주는 함수 커스텀하기
