# 03. O-RAN 프론트홀 & 7.2x split

## 1. 왜 7.2x split인가?

기능 분할(functional split)은 DU와 RU 사이에서 PHY를 어디서 자를지 정합니다.

| Split | 자르는 위치 | 프론트홀 대역폭 | O-RU 복잡도 |
|-------|-------------|-----------------|-------------|
| Option 8 (CPRI) | RF 직전 (시간영역 IQ) | 매우 큼 | 낮음 |
| **7.2x (O-RAN)** | **주파수영역 IQ (iFFT 전)** | 압축 시 합리적 | 중간 |
| Option 6 (MAC-PHY) | PHY 전체 RU | 작음 | 높음 |

O-RAN은 **7.2x**를 표준 채택. O-RU가 **iFFT/FFT, CP 삽입/제거, PRACH,
디지털 빔포밍, IQ 압축**을 수행합니다. 본 저장소의 `fronthaul` + `hal`이
이 경계를 다룹니다.

## 2. 전송 스택

```
┌───────────────────────────┐
│ O-RAN U-plane / C-plane    │  (application layer)
├───────────────────────────┤
│ eCPRI (또는 RoE)           │  message type, PC_ID, SEQ_ID
├───────────────────────────┤
│ Ethernet (VLAN, PCP)       │  CoS로 U/C/S/M 우선순위 분리
├───────────────────────────┤
│ 1/10/25 GbE                │
└───────────────────────────┘
```

- **eCPRI 공통 헤더**(4 bytes 이상): version, message type, payload size.
- 주요 message type: `0x00` IQ data(U-plane), `0x02` real-time control(C-plane),
  `0x05` one-way delay measurement(S-plane 보조).

본 저장소 구현: `src/fronthaul/ecpri.{h,c}`.

## 3. C-plane (제어 평면)

DU가 RU에게 "다음 슬롯에 어떤 자원(PRB)에서 송/수신하라"를 알려줍니다.

- **Section Type 1**: 대부분의 DL/UL (most data).
  - 필드: `frameId, subframeId, slotId, startSymbolId, startPrbc,
    numPrbc, beamId`.
- **Section Type 3**: PRACH 및 mixed-numerology.
  - Type 1 필드에 더해 **`timeOffset`(섹션 시간 오프셋),
    `frameStructure`([7:4] FFT 크기 / [3:0] numerology µ), `cpLength`(CP
    길이), `freqOffset`(부반송파 단위 주파수 오프셋, 부호 있음)** 를
    추가로 운반합니다. PRACH occasion의 시간/주파수 위치를 지정하는 데
    필요합니다.

본 저장소 구현: `src/fronthaul/oran_cplane.c`
(`oran_cplane_encode/decode` = Type 1, `oran_cplane3_encode/decode` = Type 3).

## 4. U-plane (사용자 평면 = IQ 데이터)

실제 주파수영역 IQ 샘플 전송.

- 각 PRB(12 subcarrier)의 IQ를 압축하여 전송.
- **압축 방식**(`udCompHdr`의 compMeth):
  - `ORAN_COMP_NONE` (0): 무압축 16-bit IQ. round-trip 정확.
  - `ORAN_COMP_BFP` (1): **Block Floating Point** — PRB(24개 값) 단위로
    공통 지수(exponent) 하나를 공유하고, 가장 큰 크기에 맞춰 우측
    시프트한 mantissa를 `iqWidth` 비트로 패킹. 블록이 이미 `iqWidth`
    비트에 들어가면 지수=0이라 무손실, 그렇지 않으면 하위 비트가
    버려지는 손실 압축(일반적 BFP 트레이드오프).
  - 예: 9-bit BFP는 PRB당 48바이트(무압축) → 28바이트로 축소.
  - `ORAN_COMP_MULAW` (3): **µ-law companding** — 비선형 압축. 작은
    크기에 더 많은 코드를, 큰 크기에 적은 코드를 할당(로그 특성).
    고정 비율(데이터와 무관하게 `iqWidth` 비트/성분)이며 소신호
    충실도가 균일 양자화보다 우수. 구현: `src/fronthaul/mulaw.c`.
  - BFP·µ-law는 MSB-first 비트 패커(`include/oru/bitpack.h`)를 공유.
- 시간 정렬: C-plane의 section과 U-plane 메시지가 `frame/subframe/slot/symbol`로
  매칭됩니다 (S-plane PTP 시간 기준).

본 저장소 구현: `src/fronthaul/oran_uplane.c` (+ BFP 코어
`src/fronthaul/bfp.c`, MSB-first 비트 패커 포함).

### 4.1 메시지당 다중 섹션 + radio-app 헤더

실제 프론트홀 메시지는 한 eCPRI 패킷 안에 **공통 radio-application 헤더**
하나와 **여러 개의 섹션**을 담습니다. 각 섹션은 서로 다른 PRB 범위와
압축 방식을 가질 수 있습니다 (예: 데이터 섹션 + PRACH 섹션 혼재).

```
┌───────────────────── 한 U-plane eCPRI 메시지 ─────────────────────┐
│ radio-app 헤더                                                     │
│   dataDirection / payloadVersion / filterIndex                     │
│   frameId / subframeId / slotId / startSymbolId / numberOfSections │
├──────────────┬──────────────┬──────────────┬─────────────────────┤
│ 섹션 헤더 0  │ 섹션 헤더 1  │ 섹션 헤더 2  │ ...                  │
│ +IQ payload  │ +IQ payload  │ +IQ payload  │                      │
│ (sectionId,  │              │              │                      │
│  startPrbu,  │              │              │                      │
│  numPrbu,    │              │              │                      │
│  udCompHdr)  │              │              │                      │
└──────────────┴──────────────┴──────────────┴─────────────────────┘
```

본 저장소 구현: `oran_uplane_msg_encode()` / `oran_uplane_msg_decode()`
(`oran_uplane_msg_t`에 radio-app 헤더 + 섹션 헤더 배열 + 섹션별 IQ
오프셋이 채워집니다). 섹션별 압축은 위의 단일-섹션 코덱과 동일한
NONE/BFP 경로를 재사용합니다.

## 5. 타이밍 (매우 중요)

O-RU는 정해진 윈도(window) 안에 IQ를 처리/전송해야 합니다.

```
        DL 수신 윈도              UL 송신 윈도
   [boundary-T2a_max,        [boundary+Ta3_min,
    boundary-T2a_min]         boundary+Ta3_max]
   ───────────■───────────────────■───────────▶ 시간
              │                    │
        ┌─────┴─────┐        ┌─────┴─────┐
        │ DL packet │        │ UL packet │
        │ 너무 이름 │EARLY   │ 마감 초과 │LATE
        │ 윈도 안   │ON_TIME │ 윈도 안   │ON_TIME
        │ 마감 초과 │LATE    │ 너무 이름 │EARLY
        └───────────┘        └───────────┘
              ▲ slot boundary (PTP 동기된 공통 시간축)
```

- `T2a`(DL 수신), `Ta3`(UL 송신) 윈도 파라미터는 M-plane(YANG)으로 설정.
- 이 시간축의 기준이 **S-plane PTP** 입니다 → `05-mplane-splane.md`.

### 5.1 타이밍 윈도 시뮬 스케줄러

`src/fronthaul/fh_sched.c`가 이 윈도를 모델링합니다. PTP·RF 하드웨어
없이도 호스트에서 데이터 경로 타이밍을 검증할 수 있습니다.

- `fh_sched_slot_ns(scs)` — numerology(SCS)로 슬롯 길이(ns) 산출
  (15→1000µs, 30→500µs, 60→250µs, 120→125µs).
- `fh_sched_slot_boundary(t0, slot)` — 슬롯 경계 시각(ns).
- `fh_sched_classify(dir, boundary, event)` — 패킷을
  **EARLY / ON_TIME / LATE** 로 분류하고 방향별 카운터를 누적.

윈도 경계값은 `config/oru-config.ini`의 `[fronthaul]`
`t2a_*_ns` / `ta3_*_ns`로 설정하며, `oru_app`이 OPERATIONAL 진입 시
합성 도착 샘플로 분류 동작을 시연합니다.

### 5.2 타이밍 ↔ 데이터패스 통합 (데드라인 enforcement)

`src/fronthaul/datapath.c`가 스케줄러를 HAL·U-plane 코덱과 묶어
실제 RX/TX 경로의 데드라인을 강제합니다.

- `datapath_handle_dl()` — DL 패킷 도착 시 T2a 윈도로 분류:
  **ON_TIME/EARLY → 디코드 후 `hal_tx_iq()`로 방출**, **LATE → 폐기**
  (이미 슬롯 경계가 지나 방출 불가) 후 `dl_dropped_late` 카운트.
- `datapath_build_ul()` — `hal_rx_iq()`로 IQ 캡처 후 Ta3 데드라인
  검사: 마감을 넘겨도 데이터 손실을 막기 위해 **패킷은 전송하되**
  `ul_late`로 플래그(추후 o-ran-fm 알람 연동).

## 6. 본 저장소에서의 데이터 흐름 (DL 예시)

```
[DU] ──eCPRI(C-plane Sec1)──▶ oran_cplane_decode() ── 스케줄 메모
[DU] ──eCPRI(U-plane IQ)────▶ datapath_handle_dl()
                                  │  fh_sched_classify(T2a)
                                  ├─ LATE  → 폐기(드롭 카운트)
                                  └─ ON_TIME/EARLY
                                        │  oran_uplane_decode()
                                        ▼
                              hal_tx_iq()  (PL DFE → JESD204 → ADRV9025)
                                        │
                                        ▼
                                   RF 방출 📶
```

UL은 역방향: `hal_rx_iq()`(ADRV9025→JESD204→PL) →
`datapath_build_ul()`(Ta3 데드라인 검사 + `oran_uplane_encode()`) →
eCPRI로 DU에 전송.
