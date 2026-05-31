# 05. M-plane (관리) & S-plane (동기)

## M-plane — 관리 평면

O-RU는 **NETCONF/YANG** 기반으로 관리됩니다 (O-RAN WG4 M-plane 사양).

```
[관리 시스템 / SMO / O-DU]  ── NETCONF(SSH/TLS) ──▶  [O-RU]
                                                      sysrepo (datastore)
                                                      netopeer2 (NETCONF 서버)
                                                          │ 콜백
                                                      oru_app (mplane 모듈)
```

- **YANG 모델**: `o-ran-uplane-conf`, `o-ran-module-cap`, `o-ran-fm`(알람),
  `o-ran-performance-management`, `o-ran-sync` 등.
- 흐름: 관리자가 설정 edit → datastore 변경 → mplane 콜백 → 본 저장소가
  carrier/array/endpoint 설정을 HAL과 fronthaul에 적용.
- 두 가지 아키텍처: **hierarchical**(DU 경유) / **hybrid**(직접 SMO). P5G는
  보통 단순화된 hierarchical.

본 저장소 구현: `src/mplane/mplane.{h,c}` — 실제 sysrepo 대신 설정파일/콜백
스텁으로 동일한 인터페이스(`mplane_apply_config`)를 제공.

샘플 설정: `config/oru-config.ini` 의 `[carrier]`, `[array]` 섹션 (실제로는
YANG 인스턴스 데이터에 해당).

### YANG 인스턴스 데이터 (검증 + JSON export)

`src/mplane/yang.{h,c}`가 설정을 O-RAN YANG 제약으로 검증하고
`o-ran-uplane-conf` 형태의 JSON 인스턴스 데이터로 내보냅니다.

- `yang_validate_carrier()` — center-freq(FR1 0.41–7.125GHz),
  bandwidth(5–100MHz), SCS(15/30/60/120kHz), 안테나 수(1–4T/R),
  duplex(TDD/FDD) 범위를 검사. **`mplane_apply_config()`가 설정 적용
  전에 이 검증을 호출**하여, NETCONF 서버가 잘못된 edit-config를
  거부하는 동작을 모사.
- `yang_carrier_to_json()` — `tx/rx-array-carriers` 트리로 직렬화.
- `yang_carrier_from_json()` — JSON 인스턴스 데이터를 파싱해
  carrier config로 역변환(의존성 없는 미니 리더). 파싱 후 자동 검증되어
  반환 시 항상 유효. export→parse round-trip 테스트로 보장.
- CLI: `oru_app --export-yang` 로 검증 후 JSON을 표준출력에 인쇄.
  샘플 결과는 `config/yang/oru-uplane-conf.sample.json`.

### 성능 관리 (PM, o-ran-performance-management)

O-RAN은 측정 구간(보통 15분/24시간)마다 카운터를 모아 SMO에 보고합니다.
`src/mplane/perf.c`가 그 집계 계층입니다.

- 각 서브시스템(datapath/fronthaul/cu_match)이 만드는 카운터를 한 곳에
  누적: 송수신 패킷/바이트, eAxC seq 갭, 타이밍 윈도 결과(DL/UL
  on-time/early/late), C/U 정합 결과(matched/orphan/out-of-range), 평균
  EVM%.
- `perf_snapshot()` — 현재 측정 구간을 닫아 `last`로 스냅샷하고 `live`를
  리셋(interval-id 증가). 다음 구간은 독립적으로 누적.
- `perf_to_json()` — 닫힌 구간을 `o-ran-performance-management` 형태의
  JSON 인스턴스 데이터로 직렬화.
- CLI: `oru_app --export-pm` 로 샘플 PM 인터벌 JSON을 인쇄.
  `demo_slot_loop`은 데이터패스 카운터를 PM 인터벌로 접어 보고합니다.

### 결함 관리 (FM, o-ran-fm)

O-RU는 결함을 **알람**(fault-id, severity, raised/cleared 상태)으로
SMO에 보고합니다. 알람은 한 번 찍고 끝나는 로그가 아니라 **상태**입니다:
결함이 나타나면 raise, 사라지면 clear, 이미 active한 알람을 다시 raise하면
중복 생성 없이 갱신(raise-count 증가)합니다. `src/mplane/fm.c`:

- `fm_raise()` / `fm_clear()` — 활성 알람 테이블 관리. raise는 최초
  전환 시에만 history 이벤트를 기록.
- `fm_max_severity()` / `fm_active_count()` — 집계 조회.
- `fm_check_perf()` — 닫힌 PM 구간(`perf_t.last`)을 임계값
  (`fm_thresholds_t`: dl_late/ul_late/seq_gaps/cu_orphan/EVM%)과 대조해
  자동으로 알람을 raise/clear. 다음 정상 구간에서 자동 clear.
- `fm_to_json()` — 활성 알람 목록을 `o-ran-fm:active-alarm-list` JSON으로
  직렬화.
- `mplane_raise_alarm()`은 이제 FM 스토어(`mplane_fm()`)로 전달됩니다.
- CLI: `oru_app --demo-fm` 로 degraded PM 구간 → 알람 생성 → JSON을 시연.

## S-plane — 동기 평면

O-RU의 모든 타이밍 기준. **IEEE 1588v2 PTP** (+ 옵션 **SyncE**).

```
  GrandMaster(GPS/GNSS) ──PTP──▶ [네트워크] ──PTP──▶ O-RU
                                                      ├ ptp4l (PTP)
                                                      └ phc2sys (PHC→시스템클럭)
```

- O-RAN 동기 등급: **LLS-C1 ~ C4** (구성에 따라 GM 위치 상이).
- 정확도 목표: 공기 인터페이스에서 ±1.5µs (TAE) 수준 → PTP가 핵심.
- SyncE: 주파수 동기 보강.

본 저장소 구현: `src/splane/splane.{h,c}` — host에서는 즉시 LOCKED를
시뮬레이션, target에서는 linuxptp(`ptp4l`)의 상태를 폴링.

### 상태머신에서의 역할

`oru_app`은 **SYNC** 상태에서 `splane_wait_lock()` 이 성공해야만
**CONFIG/OPERATIONAL** 로 진행합니다. PTP lock이 없으면 프론트홀 타이밍
윈도를 맞출 수 없기 때문입니다 (→ `03` 문서 §5).

## 우선순위 / VLAN

| Plane | 전형적 우선순위 | 비고 |
|-------|----------------|------|
| S-plane (PTP) | 최상 | 지연/지터 민감 |
| C-plane | 높음 | 슬롯 데드라인 |
| U-plane | 높음 | 대용량 IQ |
| M-plane | 낮음 | 비실시간 |

스위치/NIC에서 802.1Q VLAN + PCP로 분리합니다.
