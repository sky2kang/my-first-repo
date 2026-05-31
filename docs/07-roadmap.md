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
- [x] O-RAN C-plane Section Type 1/3 인코딩/디코딩 (`oran_cplane*`)
- [x] U-plane BFP 압축/해제 구현 + round-trip 테스트 (`bfp.c`, `test_bfp`)
- [x] U-plane 메시지당 다중 섹션 + radio-app 헤더 (`oran_uplane_msg_*`)
- [x] 프론트홀 타이밍 윈도(T2a/Ta3) 시뮬 스케줄러 (`fh_sched.c`, `test_fh_sched`)
- [x] U-plane µ-law 압축 추가 (compMeth=3, `mulaw.c`, `test_mulaw`)
- [x] 타이밍 윈도 ↔ RX/TX 경로 통합 + 데드라인 enforcement (`datapath.c`)
- [x] 설정을 YANG 인스턴스(JSON)로 표현 + 검증 (`yang.c`, `--export-yang`)
- [x] U-plane modulation compression (compMeth=4, `modcomp.c`, `test_modcomp`)
- [x] 슬롯-cadence 처리 루프 + 데이터패스 구동 (`slot_loop.c`, `test_slot_loop`)
- [x] YANG 인스턴스 입력 파싱 (`yang_carrier_from_json`, round-trip 테스트)
- [x] C-plane↔U-plane 시간 정합 매처 (`cu_match.c`, `test_cu_match`)
- [x] 압축 성능 측정 하니스 (`comp_bench.c`, `--bench-compression`)
- [x] eCPRI+C/U-plane 패킷 조립/파싱 + eAxC seq 추적 (`fh_packet.c`, `--demo-fh`)
- [x] PRACH preamble(Zadoff-Chu) 생성/검출 시뮬 (`prach.c`, `--demo-prach`)

> Phase 1(호스트 기능 심화) 항목 완료. 남은 실시간화(전용 스레드/CPU 핀,
> 실제 PTP 클럭 연동)와 실제 패킷 I/O는 Phase 2(하드웨어)에서 다룹니다.

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
