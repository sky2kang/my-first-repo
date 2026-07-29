# 08. HAL 스텁 ↔ ADI ADRV9025 API 매핑

Phase 2(하드웨어 bring-up)에서 본 저장소의 HAL 스텁(`src/hal/adrv9025/`)을
실제 ADI API 호출로 치환하기 위한 매핑 문서입니다. 함수명은 ADI **no-OS
madura 드라이버**(`drivers/rf-transceiver/madura/devices/adrv9025/public/include/`)
헤더에서 직접 확인한 것이며, 절차 설명은 **UG-1727** (ADRV9022/9026/9029
System Development User Guide)을 따릅니다.

> **명명 주의**: "ADRV9025"는 ADRV902x 패밀리(9022/9026/9029)의 공통 API
> 명칭입니다. 모든 API 함수는 `adi_adrv9025_*` 접두사를 쓰며, 문서는
> ADRV9026/9029 이름으로 발행됩니다 (→ `docs/04` 참고).

## 1. 초기화: `hal_init()` 매핑

`hal_init(profile_path)`의 `TODO(target)` 블록은 아래 순서로 치환합니다.

| 단계 | 본 저장소 (SIM) | 실제 ADI API | 헤더 |
|------|-----------------|--------------|------|
| 1. HW open / reset | `g_hal` 초기화 | `adi_adrv9025_HwOpen()` | `adi_adrv9025.h` |
| 2. SPI 검증 | (없음) | `adi_adrv9025_SpiVerify()` | `adi_adrv9025.h` |
| 3. 칩 ID 확인 | 로그만 | `adi_adrv9025_ProductIdGet()`, `adi_adrv9025_DeviceRevGet()` | `adi_adrv9025.h` |
| 4. 프로파일 로드 | `profile_path` 문자열 보관 | `adi_adrv9025_ConfigFileLoad()` (TES 생성 JSON) | `adi_adrv9025_utilities.h` |
| 5. Pre-MCS init | 즉시 성공 | `adi_adrv9025_PreMcsInit_v2()` → `adi_adrv9025_PreMcsInit_NonBroadCast()` | `adi_adrv9025_utilities.h` |
| 6. MCS | 즉시 성공 | `adi_adrv9025_MultichipSyncSet()` → SYSREF 펄스 인가 → `adi_adrv9025_MultichipSyncStatusGet()` 폴링 | `adi_adrv9025.h` |
| 7. Post-MCS init | 즉시 성공 | `adi_adrv9025_PostMcsInit()` (+ `adi_adrv9025_UtilityInitFileLoad()`로 설정 주입 가능) | `adi_adrv9025_utilities.h` |
| 8. 라디오 제어 초기화 | (없음) | `adi_adrv9025_RadioctrlInit()` (GPIO/PLL 초기값) | `adi_adrv9025_utilities.h` |

Pre-MCS 단계 내부에서 펌웨어 바이너리 로드가 일어납니다(개별 호출도 가능):
`adi_adrv9025_ArmImageLoad()` / `adi_adrv9025_CpuImageLoad()` /
`adi_adrv9025_StreamImageLoad()`. TES가 생성하는 산출물(프로파일 JSON, ARM
이미지, stream 이미지, 이득 테이블)을 타겟 파일시스템(`/opt/oru/`)에 두고
경로를 `config/oru-config.ini`의 `radio.profile_path` 계열 키로 전달하는
구조를 권장합니다.

`hal_shutdown()` → `adi_adrv9025_Shutdown()` (HW reset 포함).

## 2. JESD204: `jesd204_bringup()` 매핑

본 저장소의 상태머신 `RESET → CGS → ILAS → DATA`(`jesd204.c`)는 아래 API로
구현됩니다. 방향 주의 — **framer = ADRV9025 → FPGA (RX 경로)**,
**deframer = FPGA → ADRV9025 (TX 경로)**.

| 본 저장소 상태 | 실제 절차 | ADI API | 헤더 |
|----------------|-----------|---------|------|
| 링크 리셋 | framer/deframer 비활성 후 재활성 | `adi_adrv9025_FramerLinkStateSet()`, `adi_adrv9025_DfrmLinkStateSet()` | `adi_adrv9025_data_interface.h` |
| CGS | SYSREF 게이팅 활성 | `adi_adrv9025_FramerSysrefCtrlSet()`, `adi_adrv9025_DeframerSysrefCtrlSet()` | 〃 |
| ILAS | ILAS 설정 대조 | `adi_adrv9025_DfrmIlasMismatchGet()` (Lane0 ILAS vs deframer 설정 비교) | 〃 |
| DATA (lock) | 링크 상태 폴링 | `adi_adrv9025_FramerStatusGet()`, `adi_adrv9025_DeframerStatusGet()`, `adi_adrv9025_DfrmLinkConditionGet()` (User Data Ready) | 〃 |

- `hal_jesd204_is_locked()` → `DeframerStatusGet()`/`FramerStatusGet()`의
  링크 상태 비트로 치환.
- FPGA 쪽(Xilinx JESD204 IP)의 파라미터(L/M/F/S/N')는 TES 프로파일과
  일치해야 하며, 불일치 시 `DfrmIlasMismatchGet()`이 잡아냅니다.
- deterministic latency를 위해 SYSREF는 MCS(§1-6)와 동일한 소스를 사용.

## 3. RF 제어 매핑

| 본 저장소 HAL | 실제 ADI API | 헤더 | 비고 |
|---------------|--------------|------|------|
| `hal_set_carrier(freq)` | `adi_adrv9025_PllFrequencySet()` / `_v2()` | `adi_adrv9025_radioctrl.h` | LO1/LO2/AUX 선택 인자 |
| `hal_tx_enable(mask)` `hal_rx_enable(mask)` | `adi_adrv9025_RxTxEnableSet()` | 〃 | SPI(non-pin) 모드 기준. 채널 mask 개념 동일 |
| (TDD 스위칭) | `adi_adrv9025_RadioCtrlCfgSet()` + `adi_adrv9025_ArmGpioPinsSet()` | 〃 | P5G TDD에서는 pin 모드 + GPIO로 슬롯 단위 TX/RX 전환 권장 |
| (TX 전력 조정) | `adi_adrv9025_TxAttenSet()` (mdB 단위) | `adi_adrv9025_tx.h` | 스텁에 대응 함수 없음 — Phase 3에서 HAL에 추가 |
| (RX 이득) | `adi_adrv9025_RxGainSet()` (manual) 또는 AGC(`adi_adrv9025_agc.h`) | `adi_adrv9025_rx.h` | 〃 |
| (상태 확인) | `adi_adrv9025_RadioStateGet()` | `adi_adrv9025_radioctrl.h` | FAULT 진단에 활용 |

## 4. 데이터패스: `hal_tx_iq()` / `hal_rx_iq()`

**이 두 함수는 ADI API로 매핑되지 않습니다.** IQ 샘플은 SPI가 아니라
JESD204 데이터 평면으로 흐르므로, 실제 구현은:

```
hal_tx_iq() → PL DFE TX FIFO/DMA (AXI-Stream) → Xilinx JESD204 IP → ADRV9025
hal_rx_iq() ← PL DFE RX FIFO/DMA (AXI-Stream) ← Xilinx JESD204 IP ← ADRV9025
```

- Linux 타겟에서는 ADI IIO 프레임워크(`linux/drivers/iio/adc/adrv902x/`)의
  버퍼 인터페이스 또는 커스텀 DMA(UIO/dmaengine)로 구현.
- ADI API는 제어 평면(SPI)만 담당한다는 것이 HAL 경계 설계의 핵심입니다.

## 5. 초기 캘리브레이션 (스텁에 없는 필수 단계)

PostMcsInit 이후, TX/RX 활성 전에 초기 캘리브레이션이 필요합니다
(UG-1727 초기화 시퀀스의 일부):

- `adi_adrv9025_cals.h` — init cals 실행/대기 (QEC, LOL 등).
- DPD를 칩 내장 기능으로 쓸 경우 `adi_adrv9025_dfe.h` (ADRV9029 계열).
  본 저장소의 소프트웨어 DPD(`src/fronthaul/dpd.c`)는 이 경우 학습
  루프의 레퍼런스/검증용으로 활용.

## 6. 파일 ↔ 모듈 참조표

| ADI 헤더 | 담당 영역 | 본 저장소 대응 |
|----------|-----------|----------------|
| `adi_adrv9025.h` | open/init/MCS/ID | `hal_init()` |
| `adi_adrv9025_utilities.h` | PreMcs/PostMcs/파일 로드 | `hal_init()` |
| `adi_adrv9025_data_interface.h` | JESD204 framer/deframer | `jesd204.c` |
| `adi_adrv9025_radioctrl.h` | PLL/LO, TxRx enable, TDD | `hal_set_carrier()`, `hal_tx/rx_enable()` |
| `adi_adrv9025_tx.h` / `_rx.h` | 감쇠/이득 | (Phase 3에서 HAL 확장) |
| `adi_adrv9025_agc.h` | AGC | 〃 |
| `adi_adrv9025_cals.h` | 초기/추적 캘리브레이션 | (Phase 3) |
| `adi_adrv9025_dfe.h` | 칩 내장 DPD/CFR (9029) | `cfr.c`/`dpd.c`와 역할 분담 결정 |
| `adi_adrv9025_arm.h`/`_cpu.h` | 펌웨어 제어 | `hal_init()` 내부 |
| `adi_adrv9025_gpio.h` | GPIO/모니터링 | TDD 핀 제어 |
| `adi_adrv9025_error.h` | 오류 코드 | `oru_status_t`로 변환 계층 필요 |

## 7. 권장 작업 순서 (Phase 2 체크리스트에 대응)

1. SPI 확인: `SpiVerify()` + `ProductIdGet()` — `docs/04` 체크리스트 1번
2. `hal_init()` 치환 (§1 표의 1~8) — 에러는 `adi_adrv9025_error.h` 코드를
   `oru_status_t`로 변환하는 헬퍼를 하나 두고 일괄 처리
3. `jesd204_bringup()` 치환 (§2) — FPGA IP 파라미터 = TES 프로파일 검증
4. `hal_set_carrier()`/`hal_tx_enable()`/`hal_rx_enable()` 치환 (§3)
5. init cals 추가 (§5) 후 스펙트럼/EVM 확인
6. 데이터패스(§4)는 PL 디자인과 함께 Phase 3에서

## 참고 자료

- UG-1727: https://www.analog.com/media/radioverse-adrv9026/adrv9022-adrv9026-adrv9029-system-development-user-guide-ug-1727.pdf
- no-OS madura 드라이버: https://github.com/analogdevicesinc/no-OS/tree/main/drivers/rf-transceiver/madura
- ADI Linux IIO 드라이버: https://github.com/analogdevicesinc/linux/tree/main/drivers/iio/adc/adrv902x
- 평가보드/TES 가이드: https://wiki.analog.com/resources/eval/user-guides/adrv9025
