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
- 시간 정렬: C-plane의 section과 U-plane 메시지가 `frame/subframe/slot/symbol`로
  매칭됩니다 (S-plane PTP 시간 기준).

본 저장소 구현: `src/fronthaul/oran_uplane.c` (+ BFP 코어
`src/fronthaul/bfp.c`, MSB-first 비트 패커 포함).

## 5. 타이밍 (매우 중요)

O-RU는 정해진 윈도(window) 안에 IQ를 처리/전송해야 합니다.

```
   T1a_max  ────▶│       │◀──── Ta3 (UL)
                 │  슬롯 │
  DL: DU 송신 ──▶│ 경계 │──▶ RU가 RF로 방출
                 (PTP 동기된 공통 시간축)
```

- `T2a`, `Ta3`, `T1a` 등 윈도 파라미터는 M-plane(YANG)으로 설정.
- 이 시간축의 기준이 **S-plane PTP** 입니다 → `05-mplane-splane.md`.

## 6. 본 저장소에서의 데이터 흐름 (DL 예시)

```
[DU] ──eCPRI(C-plane Sec1)──▶ oran_cplane_parse() ── 스케줄 메모
[DU] ──eCPRI(U-plane IQ)────▶ oran_uplane_parse() ── IQ 압축해제
                                        │
                                        ▼
                              hal_adrv9025_tx_iq()  (PL DFE → JESD204 → ADRV9025)
                                        │
                                        ▼
                                   RF 방출 📶
```

UL은 역방향: ADRV9025 수신 → JESD204 → PL(FFT/압축) → `oran_uplane_build()`
→ eCPRI로 DU에 전송.
