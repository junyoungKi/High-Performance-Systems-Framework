### **Character Encoding Conflict (Unicode vs ANSI)**

- **Problem:** `OutputDebugString` 및 `sprintf_s` 호출 시 `char*`와 `LPCWSTR` 간의 형 변환 에러(`C2664`) 발생.

- **Cause:** 현대적인 윈도우 환경(Unicode)과 튜토리얼 코드(ANSI) 간의 인코딩 불일치.

- **Solution:** * 1바이트 기반 `sprintf_s`를 와이드 문자용 **`swprintf_s`**로 교체.
  
  - 문자열 리터럴 앞에 **`L`** 매크로를 사용하여 2바이트 문자열임을 명시.
  
  - `OutputDebugStringW`와의 호환성을 확보하여 디버그 로그 시스템 안정화.

- **Takeaway:** 시스템 프로그래밍에서 문자열 인코딩 체계가 메모리 레이아웃 및 API 호환성에 미치는 영향 파악.
