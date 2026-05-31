# 01. 시스템 아키텍처

## 1. O-RAN 전체 그림과 O-RU의 위치

5G RAN은 전통적으로 하나의 박스(gNB)였지만, O-RAN Alliance는 이를
**CU / DU / RU** 로 분할합니다.

```
   ┌──────┐   F1   ┌──────┐  Fronthaul (7.2x)  ┌──────┐
   │ O-CU │────────│ O-DU │═══════════════════│ O-RU │── RF ── 안테나
   └──────┘        └──────┘                    └──────┘
   RRC/PDCP        RLC/MAC/                     L1-low /
                   L1-high                      RF front-end
```

- **O-RU (우리 프로젝트)**: 안테나에 가장 가까움. PHY 하위 계층(L1-low:
  CP 추가/제거, (i)FFT, PRACH, 디지털 빔포밍, 압축)과 RF front-end
  (ADRV9025)를 담당.
- **Split 옵션**: O-RAN은 **7.2x** (intra-PHY split)를 주로 사용. DU와
  RU 사이에 주파수영역 IQ를 주고받음. → 자세히는 `03-oran-fronthaul-7.2x.md`.

## 2. P5G(Private 5G) 컨텍스트

- 공용망 대비 **소규모/저전력/실내 또는 캠퍼스** 배치.
- 일반적으로 **TDD**, n78(3.5GHz) / CBRS(3.55–3.7GHz) / n79 등.
- 대역폭 20~100MHz, 4T4R(ADRV9025 1개로 4 송신/4 수신) 구성이 자연스러움.

## 3. 하드웨어 블록도

```
┌──────────────────────────────────────────────────────────────┐
│  Zynq UltraScale+ MPSoC                                        │
│                                                                │
│  ┌────────────── PS (Processing System) ──────────────┐       │
│  │  Quad Cortex-A53 (APU)   ── 임베디드 Linux           │       │
│  │   ├─ oru_app (이 저장소: M/S/C/U-plane 오케스트레이션)│       │
│  │   ├─ linuxptp (S-plane)                              │       │
│  │   └─ netopeer2 / sysrepo (M-plane NETCONF)           │       │
│  │  Dual Cortex-R5 (RPU)    ── 실시간 제어(옵션)         │       │
│  └─────────────────────────────────────────────────────┘       │
│            │ AXI                                                │
│  ┌─────────┴──── PL (Programmable Logic / FPGA) ───────┐       │
│  │  - eCPRI/O-RAN 패킷 처리 (옵션: HW 오프로드)          │       │
│  │  - O-RAN U-plane (de)compression                     │       │
│  │  - CFR / DPD / DUC·DDC (디지털 프론트엔드, DFE)       │       │
│  │  - JESD204B/C IP  ←──────────────────┐               │       │
│  └───────────────────────────────────────┼─────────────┘       │
└────────────────────────────────────────┼───────────────────┘
                                          │ JESD204B/C (SERDES)
                                  ┌───────┴────────┐
                                  │   ADRV9025      │  4T4R wideband
                                  │  (RF transceiver)│  transceiver
                                  └───────┬────────┘
                                          │ analog RF
                                  ┌───────┴────────┐
                                  │ PA / LNA / 필터 │ → 안테나
                                  └────────────────┘
       SPI: PS ──→ ADRV9025 레지스터/프로파일 제어
```

## 4. 소프트웨어 계층 (이 저장소)

```
┌──────────────────────────────────────────────────────┐
│                 oru_app (src/app)                      │  오케스트레이터
│   상태머신: INIT → SYNC → CONFIG → OPERATIONAL          │
└───┬───────────┬────────────┬────────────┬─────────────┘
    │           │            │            │
┌───┴───┐  ┌────┴────┐  ┌────┴─────┐  ┌───┴──────┐
│ mplane│  │ splane  │  │ fronthaul│  │   hal     │
│NETCONF│  │  PTP    │  │ eCPRI +  │  │ adrv9025  │
│ /YANG │  │ /SyncE  │  │ C/U-plane│  │ + jesd204 │
└───────┘  └─────────┘  └──────────┘  └───────────┘
                  └──────── common (log, config, types) ────────┘
```

각 모듈은 `include/oru/<module>.h` 의 공개 API로만 서로를 호출하며,
하드웨어 의존부는 HAL 뒤로 숨깁니다. → 호스트에서 스텁으로 빌드/테스트 가능.

## 5. 런타임 상태 머신

```
        ┌──────┐  HW/드라이버 초기화   ┌──────┐
  ──────│ INIT │─────────────────────▶│ SYNC │  PTP lock 대기
        └──────┘                      └──┬───┘
                                         │ locked
        ┌─────────────┐  M-plane 설정 적용 ┌─────────┐
        │ OPERATIONAL │◀──────────────────│ CONFIG  │  ADRV9025 프로파일/캐리어
        └──────┬──────┘                   └─────────┘   로드, JESD204 bring-up
               │ fault
        ┌──────┴──────┐
        │   FAULT     │ → 알람 보고(M-plane), 복구 시도
        └─────────────┘
```

자세한 plane별 설계는 다음 문서들에서 다룹니다.
