
# Portfolio Sample Code (UE 5.5 / C++)

실프로젝트 기반 코드를 포트폴리오 제출용으로 정리/마스킹한 UE5 클라이언트 샘플입니다.  
핵심은 MVVM 기반 UI, 비디오 연출 파이프라인, 가챠 보상/확률/인벤토리 정합성 구현 역량입니다.

---

## Tech Stack
- Unreal Engine 5.5
- C++

---

## Included Systems

### 1) Gacha System
- MVVM UI 아키텍처
- 동적 비디오 연출 시퀀스 생성
- Enhanced Input 통합
- 클라-서버 보상 동기화
- 역할: 설계/구현 100%, 비디오 통합, UI 로직 최적화

### 2) Video Player System
- 멀티 비디오 시퀀스 자동 재생(Queue)
- SRT 자막 파싱 + 실시간 동기화
- 오디오 포커스 제어(BGM 자동 조절)
- False End 이벤트 필터링
- 역할: 아키텍처 설계 100%, SRT 파서, 큐 관리 로직

### 3) Reward System
- 가중치 기반 확률 알고리즘
- Pity(피티) 시스템
- 스마트 인벤토리(스택 병합)
- 트랜잭션 기반 일관성 보장
- 역할: 설계/구현 100%, 인벤토리 최적화, 서버 보안/정합성 로직

---

## Folder Structure
Portfolio_Samples/

├── GachaSystem/

│ ├── GachaU.h

│ ├── GachaUI.cpp

├── VideoPlayerSystem/

│ ├── VideoPlayer.h

│ ├── VideoPlayer_Core.cpp

│ ├── VideoPlayer_SubtitleSystem.cpp

├── RewardSystem/

│ ├── ServerRewardSystem_Gacha.cpp

│ ├── ServerRewardSystem_Inventory.cpp

└── README.md

---

## Notes

- 포트폴리오용으로 리소스/서버 의존성은 제거 또는 더미 처리되어 있을 수 있습니다.
