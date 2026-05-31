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
  - `ORAN_COMP_MODULATION` (4): **modulation compression** — 알려진
    변조 차수(BPSK/QPSK/16/64/256QAM)의 콘스텔레이션 격자를 이용.
    full IQ 대신 PRB당 스케일러 + RE별 콘스텔레이션 인덱스를 전송하고
    수신측이 `IQ = scaler × constellation[index]`로 복원. `iqWidth`
    필드는 변조 차수(bits/symbol)로 재사용. 깨끗한 변조 신호에 대해
    최고의 압축률. 구현: `src/fronthaul/modcomp.c`.
  - BFP·µ-law·modulation은 MSB-first 비트 패커
    (`include/oru/bitpack.h`)를 공유.
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

### 5.3 슬롯-cadence 처리 루프

`src/fronthaul/slot_loop.c`는 데이터패스를 **슬롯 단위로 주기 구동**
합니다. 실제 O-RU에서는 CPU에 핀된 전용 스레드가 PTP 클럭에서 매 슬롯
경계에 깨어나 그 슬롯의 U-plane 작업을 처리합니다.

- **주입 가능한 클럭(`slot_clock_fn`)** 과 **슬롯별 작업 콜백
  (`slot_work_fn`)** 으로 구성 — 호스트에서는 가상 클럭으로 시간을
  결정론적으로 주입해 테스트 가능, 타겟에서는 동일 로직이 실제 PTP
  클럭·스레드 위에서 동작.
- `slot_loop_run(max_slots)` 가 슬롯 경계를 계산하고 콜백을 호출하며
  슬롯 카운터를 누적. `oru_app`은 OPERATIONAL에서 5슬롯 동안 DL 수신 +
  UL 송신 데이터패스를 시연합니다.

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

## 7. C-plane ↔ U-plane 시간 정합 (matcher)

DU는 먼저 C-plane section으로 슬롯을 스케줄("frame F, slot SL, symbol
SYM에서 PRB [start, start+num) 예상")하고, 뒤이은 U-plane 패킷이 그
grant의 IQ를 나릅니다. O-RU는 도착한 U-plane을 어떤 grant가 허가한
것인지 짝지어야 합니다.

`src/fronthaul/cu_match.c`가 이 검증을 수행합니다:

- `cu_match_add_section1/3()` — C-plane grant를 (frame,subframe,slot,
  symbol) 키로 등록.
- `cu_match_check(u)` — U-plane 헤더를 outstanding grant와 대조:
  **OK**(PRB가 grant 범위 내), **NO_GRANT**(해당 슬롯 grant 없음 = orphan),
  **OUT_OF_RANGE**(PRB가 grant 밖). 각 결과를 카운트.
- `cu_match_grant_complete()` — 해당 grant의 U-plane PRB 누적이 numPrb에
  도달했는지(완전 커버) 확인.

실제 DU 없이 호스트에서 스케줄/데이터 페어링을 end-to-end로 검증합니다.

## 8. 압축 성능 측정

`src/fronthaul/comp_bench.c`는 동일 신호에 대해 각 압축 방식의
**압축률 vs 왕복 오차(max/RMSE)** 를 측정합니다. CLI로 표를 출력:

```
$ oru_app --bench-compression
method                raw     comp  ratio     rmse
none                 1536     1536   1.00      0.0
bfp-12               1536     1184   1.30      1.9
bfp-9                1536      896   1.71     18.1
mulaw-9              1536      864   1.78     37.8
mulaw-8              1536      768   2.00     69.2
modcomp-64qam        1536      384   4.00    581.4
modcomp-16qam        1536      288   5.33   1266.3
```

압축률이 높을수록 오차가 커지는 트레이드오프를 하드웨어 없이 비교해
적절한 `iqWidth`/방식을 고를 수 있습니다(값은 합성 정현파 신호 기준).

## 9. 패킷 조립/파싱 계층 (eCPRI + C/U-plane)

`src/fronthaul/fh_packet.c`는 eCPRI 공통 헤더(`oru/ecpri.h`)와 C/U-plane
페이로드(`oru/fronthaul.h`)를 묶어 **완전한 프론트홀 패킷 한 장**을
만들고 다시 파싱하는 최상위 계층입니다.

```
+------------------+-------------------------------------------+
| eCPRI header (8) | payload: C-plane section  OR  U-plane msg |
+------------------+-------------------------------------------+
```

- **송신(`fh_tx_t`)**: eAxC(pc_id)별 `seq_id`를 자동 증가시키며
  `fh_build_cplane_s1/s3()`, `fh_build_uplane()`로 패킷을 생성.
- **수신(`fh_rx_t`)**: `fh_parse_cplane()` / `fh_parse_uplane()`이
  eCPRI를 디코드하고 메시지 타입을 검증한 뒤, eAxC별 기대 `seq_id`와
  대조하여 **손실/재정렬(seq_gaps)** 을 카운트.
- eAxC 흐름마다 시퀀스 카운터가 독립적이며, C-plane은 페이로드 길이로
  Section Type 1(12B)/3(20B)을 구분.

CLI `oru_app --demo-fh`가 DU→O-RU 전체 경로(C-plane grant 빌드 →
U-plane 빌드 → 파싱 → `cu_match` 정합)를 시연합니다:

```
$ oru_app --demo-fh
fh demo: built C-plane (20 B) and U-plane (136 B) on eAxC 0x0001
fh demo: parsed C-plane/S1 seq=0 -> grant slot=0 PRB[0..4)
fh demo: parsed U-plane seq=1 -> match=OK grant_complete=yes
fh demo: rx pkts_ok=2 seq_gaps=0
```

## 10. PRACH 검출 시뮬레이션

DU가 Section Type 3로 PRACH occasion(시간/주파수 위치)을 스케줄하면,
UE는 그 occasion에 **Zadoff-Chu(ZC) preamble**을 송신하고, O-RU는 상관
(correlation)으로 어떤 preamble이 왔는지 검출합니다.

`src/fronthaul/prach.c` (주파수영역 모델, 짧은 ZC 길이 Nzc=139):

- `prach_gen_preamble(root, shift, amp, out)` — 논리 root와 cyclic
  shift로 ZC preamble 생성. ZC는 일정 진폭(constant amplitude)이며,
  자기 자신의 cyclic shift된 사본과의 상관이 임펄스가 되는 성질을 가짐.
- `prach_detect(rx, root, threshold, out)` — 알려진 root에 대해 모든
  cyclic shift에서 상관을 계산, **peak/mean 비율**이 threshold를 넘으면
  검출로 판정하고 추정 shift를 보고.

ZC의 임펄스 성질 덕에 잡음이 있어도 정확한 shift를 복원합니다. 잘못된
root나 잡음만 있을 때는 임펄스가 없어 비율이 낮아 오검출하지 않습니다.

CLI `oru_app --demo-prach`:

```
$ oru_app --demo-prach
prach demo: PRACH occasion scheduled (frame=4 slot=0 PRB[0..12) root=22)
prach demo: detect DETECTED shift=35 (tx=35) ratio=97.2
```

## 11. 디지털 빔포밍 가중치 적용

C-plane의 `beamId`는 O-RU의 빔포밍 가중치 테이블에서 안테나별 복소
가중치 집합으로 매핑됩니다. 단일 데이터 스트림 s에 대해 안테나 출력은:

```
y_a[n] = w_a * s[n]      (안테나 a = 0..num_ant-1)
```

가중치의 위상이 빔을 조향(steer)하고, 크기가 테이퍼링합니다. IFFT/RF
체인 이전에 수행되는 디지털 빔포밍의 핵심입니다.

`src/fronthaul/beamform.c` (Q1.15 고정소수점 가중치, 최대 4T):

- `bf_set_beam(beamId, w)` — beamId별 안테나 가중치 벡터 등록.
- `bf_set_steering_beam(beamId, theta)` — 반파장 ULA 조향 빔 생성:
  `w_a = exp(j·a·π·sin(theta))` (단위 크기).
- `bf_apply(beamId, in, n, out[])` — 입력 스트림에 가중치를 복소 곱하여
  안테나별 출력 생성.

CLI `oru_app --demo-beamform` (30° 조향 → 안테나마다 90° 위상 증가):

```
$ oru_app --demo-beamform
bf demo: beam 1 steering 30 deg, 4 antennas
bf demo:   ant0 weight = (+32767, +0j) Q1.15
bf demo:   ant1 weight = (+0, +32767j) Q1.15
bf demo:   ant2 weight = (-32767, +0j) Q1.15
bf demo:   ant3 weight = (+0, -32767j) Q1.15
bf demo: in[0]=(8000,0) -> ant0=(8000,0) ant1=(0,8000) ant2=(-8000,0) ant3=(0,-8000)
```

## 12. CFR (Crest Factor Reduction)

OFDM 파형은 PAPR(peak-to-average power ratio)이 높아, 드문 큰 피크 때문에
PA(전력증폭기)를 백오프시켜 효율을 떨어뜨립니다. CFR은 PA 직전에 이
피크를 깎아 평균 전력을 높일 수 있게 합니다. DFE에서 빔포밍 다음에
위치합니다(... → beamform → CFR → PA).

`src/fronthaul/cfr.c` — 위상 보존 hard clipping:

```
if |x| > T:  x' = x * (T / |x|)     else x' = x
```

- `cfr_measure(iq, n, out)` — peak/avg power와 PAPR(dB) 측정.
- `cfr_clip(iq, n, target_papr_db)` — RMS 기준 target PAPR에 해당하는
  임계값 T로 클리핑(위상 유지), 클리핑된 샘플 수 반환.

> 클리핑은 in-band 왜곡(EVM)과 out-of-band 방사를 유발하므로, 실제 CFR은
> 뒤에 peak windowing/filtering을 둡니다(TODO). 하드 클리핑이 평균도
> 약간 낮추므로 결과 PAPR은 목표보다 조금 높게 수렴합니다.

CLI `oru_app --demo-cfr`:

```
$ oru_app --demo-cfr
cfr demo: PAPR 9.03 dB -> 7.45 dB (target 6.0), clipped 115/1024 samples
```
