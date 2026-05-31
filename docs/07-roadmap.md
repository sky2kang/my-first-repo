# 07. 구현 로드맵

각 단계는 호스트(시뮬레이션)에서 먼저 검증 → 하드웨어로 이전하는 것을
권장합니다. 체크박스를 따라가며 채워나가세요.

## Phase 0 — 스캐폴드 (현재 저장소 상태) ✅
- [x] 디렉터리/빌드 시스템 (CMake, host/target)
- [x] common: 로깅, 설정 로더, 공통 타입
- [x] 각 plane 모듈 스텁 + 메인 상태머신
- [x] eCPRI 헤더 빌드/파싱 + 단위테스트
- [x] 문서 1~7

## Phase 1 — 호스트 기능 심화 (HW 불필요)
- [ ] O-RAN C-plane Section Type 1/3 완전 인코딩/디코딩
- [ ] U-plane BFP 압축/해제 구현 + round-trip 테스트
- [ ] 프론트홀 타이밍 윈도(T2a/Ta3) 모델링 + 시뮬 스케줄러
- [ ] 설정을 YANG 인스턴스(JSON/XML)로 표현, 검증

## Phase 2 — 플랫폼 brings-up (HW 필요)
- [ ] Vivado PL 디자인: JESD204 IP + AXI 매핑
- [ ] PetaLinux BSP/커널/rootfs, oru_app 크로스 빌드 통합
- [ ] HAL_TARGET: SPI로 ADRV9025 ID read, 프로파일 로드(ADI API)
- [ ] JESD204 실제 lock (CGS/ILAS/SYSREF)

## Phase 3 — RF & 데이터패스
- [ ] TX/RX enable, LO 설정, atten/AGC
- [ ] PL DFE(DUC/DDC, CFR) ↔ U-plane IQ 연결
- [ ] DL: eCPRI U-plane → RF 방출, 스펙트럼/EVM 확인
- [ ] UL: RF → eCPRI U-plane, NF/감도 확인

## Phase 4 — 동기 & 관리
- [ ] S-plane: ptp4l/phc2sys 연동, LLS-C 구성, TAE 측정
- [ ] M-plane: sysrepo/netopeer2 + O-RAN YANG, 설정/알람/PM
- [ ] C-plane↔U-plane 시간정합(PTP 기준) 검증

## Phase 5 — 통합/검증
- [ ] 실제 O-DU(or 테스터)와 프론트홀 IOT
- [ ] UE 연결, 처리량/지연 측정
- [ ] 알람/복구, 장시간 안정성, 전력/온도

## 권장 학습 자료 (외부, 별도 확보)
- O-RAN.WG4.CUS-Plane / M-plane / Management Architecture 사양
- ADI ADRV9025 데이터시트 & UG, no-OS, TES
- Xilinx UG: Zynq UltraScale+ MPSoC TRM, JESD204 IP, PetaLinux UG1144
- IEEE 1588-2019, ITU-T G.8275.1 (PTP telecom profile)
