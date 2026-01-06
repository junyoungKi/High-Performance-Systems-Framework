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







제안하신 항목들은 서로 충돌하기보다는 **매우 긴밀하게 연결되어 시너지를 내는 작업들**입니다. 다만, 구현 순서나 설계 방식에 있어 **'겹치는 영역'**이 존재하므로, 이를 어떻게 조율하면 효율적일지 정리해 드립니다.

---

### 1. 겹치거나 시너지가 발생하는 부분 (Synergy & Overlap)

#### ① 메모리 풀(5) vs 스택 할당자(7)

- **관계:** 둘 다 Job System의 성능을 높이기 위한 장치이지만, **관리 대상**이 다릅니다.

- **겹치는 점:** 구현할 때 **Sub-allocation(1GB 미리 빌려오기)** 로직을 공유할 수 있습니다. 하나의 거대한 메모리 블록을 할당받은 뒤, 절반은 'Job 객체용 Pool'로 쓰고, 나머지 절반은 '임시 데이터용 Stack'으로 나누어 쓰면 매우 깔끔합니다.

- **구분:** * **Pool:** `Job` 구조체 자체(고정 크기)를 담는 곳.
  
  - **Stack:** Job 내부에서 연산할 때 잠깐 쓰는 가변 크기 배열 등을 담는 곳.

#### ② 에러 커스텀 로거(4) vs 유닛 테스팅(3)

- **관계:** **로거는 테스팅의 기반 도구**가 됩니다.

- **겹치는 점:** 유닛 테스트 결과(Pass/Fail)를 출력할 때 본인이 만든 커스텀 로거를 사용하게 됩니다.

- **팁:** 로거에 `LogType::Test` 같은 카테고리를 추가하면 테스트 결과만 따로 파일로 저장하거나 색깔별로 콘솔에 찍을 수 있어 전문성이 더 높아집니다.

#### ③ 메모리 트래킹(6) vs 모든 할당자(5, 7)

- **관계:** **트래킹은 모든 메모리 작업의 감시자**입니다.

- **겹치는 점:** (5)번과 (7)번을 구현할 때, 할당/해제 로직에 (6)번에서 만든 '현재 사용량 카운트' 코드를 한 줄씩 넣어줘야 합니다.

- **팁:** ImGui 탭을 하나 만들어서 `[System Memory]`, `[Job Pool Usage]`, `[Frame Stack Usage]`를 나란히 보여주면 포트폴리오의 가시성이 폭발합니다.

---

### 2. 추천하는 작업 순서 (효율적 로드맵)

겹치는 부분을 최소화하고 코드를 여러 번 고치지 않으려면 다음 순서를 추천합니다.

**[Step 1: 기반 다지기]**

1. **커스텀 로거(4):** 앞으로의 모든 작업에서 에러를 잡아야 하므로 가장 먼저 만듭니다.

2. **헤더 분리 및 주석(1, 2):** 현재 코드를 정리하면서 어디에 어떤 할당자를 끼워 넣을지 설계도를 그립니다.

**[Step 2: 메모리 아키텍처 구축]** 3. **메모리 트래킹 기본(6):** `

#### 방법 : 전역 `new` / `delete` 오버로딩 (가장 강력함)

C++의 전역 `operator new`와 `delete`를 오버로딩해버리는 것입니다.

C++

```
void* operator new(size_t size) {
    g_MemoryCounter.Add(size); // 모든 new를 여기서 가로챔
    return malloc(size);
}
```

이 방식을 쓰면 잡 시스템뿐만 아니라 프로젝트 전체에서 일어나는 모든 할당을 단 한 곳에서 완벽하게 추적할 수 있습니다. 이것이 **"진짜 메모리 트래킹"**의 끝판왕입니다.

- **기초:** `AlignedAllocator`에 카운터를 넣어 대용량 삼각형 데이터의 사용량을 ImGui에 띄웁니다.

- **심화 (잡 시스템):** 아까 이야기한 **"잡 전용 메모리 풀(Pool)"**을 만들 때, 그 풀 안에서 메모리를 가져갈 때마다 올라가는 카운터를 따로 만듭니다.

- **시각화:** ImGui에서 다음과 같이 구분해서 보여줍니다.
  
  - `[Static Data]: 400MB` (삼각형 데이터)
  
  - `[Job System Pool]: 2MB / 10MB` (미리 할당한 잡 풀 사용량)
  
  - `[Frame Allocations]: 0 times` (프레임당 할당 횟수 - **0이 목표!**)
    
    4. **Job Pool(5) & Stack(7):** 두 할당자를 구현하면서 (6)번의 트래킹 기능과 연결합니다. 이제 ImGui에서 Job 관련 메모리 사용량이 실시간으로 움직이는 것을 볼 수 있습니다.

**[Step 3: 검증 및 완성]** 5. **유닛 테스팅(3):** 새로 만든 할당자들이 메모리를 정확하게 주고받는지, 정렬(Alignment)은 잘 지켜지는지 테스트 코드를 짭니다. 이때 로거(4)를 사용합니다.

---

### 3. 한 줄 어필 포인트 (이력서용)

이렇게 연결된 작업을 마치고 나면 이력서에 다음과 같이 쓸 수 있습니다.

> "자체 제작한 **커스텀 로거 및 메모리 트래킹 시스템**을 기반으로, Job System의 오버헤드를 줄이기 위한 **Pool/Stack Allocator 아키텍처**를 설계했습니다. 이를 통해 프레임당 수천 개의 작업을 동적 할당 없이 처리하며 성능과 안정성을 동시에 확보했습니다."

**결론:** 겹치는 부분은 오히려 **'시스템 간의 결합도'**를 보여줄 좋은 기회입니다. 특히 **메모리 풀과 스택 할당자를 하나의 거대한 메모리 덩어리에서 쪼개 쓰는 방식**으로 구현해 보시는 것을 강력 추천합니다.

가장 먼저 시작하고 싶은 **'커스텀 로거'**나 **'메모리 트래킹'** 중 구체적인 구현 코드가 필요한 부분이 있으신가요? 바로 가이드를 드릴 수 있습니다.
