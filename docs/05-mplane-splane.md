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
