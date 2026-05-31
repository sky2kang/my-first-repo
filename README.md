# 📡 zynq-adrv9025-p5g-oru

> **Private 5G (P5G) O-RAN Radio Unit (O-RU)** reference software for
> **Xilinx Zynq UltraScale+ MPSoC** + **Analog Devices ADRV9025** wideband transceiver.

이 저장소는 O-RAN 7.2x split 기반의 P5G O-RU 시스템 소프트웨어를
*처음부터* 학습하고 구축하기 위한 레퍼런스 스켈레톤입니다. 실제 양산
펌웨어가 아니라, **아키텍처 골격 + 빌드 시스템 + 동작 가능한 스텁 코드 +
단계별 문서**로 구성되어 있습니다.

---

## ⚡ TL;DR

```bash
# 호스트(개발 PC)에서 시뮬레이션 모드로 빌드 & 실행
./scripts/build.sh
./build/host/src/app/oru_app --config config/oru-config.ini
```

타겟(Zynq) 크로스 빌드와 배포는 [`docs/06-build-and-deploy.md`](docs/06-build-and-deploy.md) 참고.

---

## 🧱 무엇을 만드는가?

O-RU(Radio Unit)는 5G 기지국(gNB)을 O-RAN 표준으로 분할(disaggregation)했을 때
**안테나/RF에 가장 가까운 박스**입니다. 상위 DU(Distributed Unit)와는
**프론트홀(fronthaul)** 로 연결됩니다.

```
          ┌────────────┐   Fronthaul (eCPRI / O-RAN 7.2x)   ┌────────────────────────┐
  Core ───│  O-DU      │═══════════════════════════════════│  O-RU (이 프로젝트)     │── 🔵 RF ── 📶
  (5GC)   │ (L1-high/  │   - C-plane (제어)                 │  Zynq MPSoC + ADRV9025  │
          │  L2/L3)    │   - U-plane (IQ 데이터)            │                        │
          └────────────┘   - S-plane (PTP 동기)             └────────────────────────┘
                           - M-plane (NETCONF 관리)
```

이 프로젝트가 다루는 **4개의 plane**:

| Plane | 역할 | 본 저장소 모듈 | 주요 기술 |
|-------|------|----------------|-----------|
| **M-plane** | 관리/설정 | `src/mplane` | NETCONF / YANG |
| **S-plane** | 시간 동기 | `src/splane` | IEEE 1588 PTP, SyncE |
| **C-plane** | 무선 스케줄 제어 | `src/fronthaul` | eCPRI, O-RAN section type |
| **U-plane** | IQ 샘플 전송 | `src/fronthaul` | eCPRI, IQ compression |

그리고 RF 하드웨어 제어:

| 계층 | 역할 | 모듈 |
|------|------|------|
| **HAL / RF driver** | ADRV9025 제어, 프로파일 로드 | `src/hal/adrv9025` |
| **JESD204** | SoC ↔ 트랜시버 시리얼 링크 | `src/hal/adrv9025/jesd204` |

---

## 📁 디렉터리 구조

```
.
├── README.md                # (이 파일)
├── CMakeLists.txt           # 최상위 빌드
├── docs/                    # 단계별 학습/설계 문서  ← 여기부터 읽으세요
├── config/                  # 런타임 설정, YANG 모델
├── include/oru/             # 공용 공개 헤더
├── src/
│   ├── common/              # 로깅, 설정 로더, 공통 타입
│   ├── hal/adrv9025/        # ADRV9025 RF 드라이버 + JESD204
│   ├── fronthaul/           # eCPRI + O-RAN C/U-plane
│   ├── mplane/              # 관리 평면 (NETCONF/YANG)
│   ├── splane/              # 동기 평면 (PTP)
│   └── app/                 # O-RU 메인 애플리케이션 (오케스트레이터)
├── tests/                   # 단위 테스트
└── scripts/                 # 빌드/셋업 스크립트
```

---

## 🗺️ 학습 순서 (문서)

1. [`docs/01-architecture.md`](docs/01-architecture.md) — 전체 시스템 아키텍처
2. [`docs/02-getting-started.md`](docs/02-getting-started.md) — 개발 환경 & 첫 빌드
3. [`docs/03-oran-fronthaul-7.2x.md`](docs/03-oran-fronthaul-7.2x.md) — 프론트홀 / 7.2x split
4. [`docs/04-adrv9025-jesd204.md`](docs/04-adrv9025-jesd204.md) — RF 트랜시버 & JESD204
5. [`docs/05-mplane-splane.md`](docs/05-mplane-splane.md) — 관리/동기 평면
6. [`docs/06-build-and-deploy.md`](docs/06-build-and-deploy.md) — 타겟 빌드 & 배포
7. [`docs/07-roadmap.md`](docs/07-roadmap.md) — 단계별 구현 로드맵
8. [`docs/glossary.md`](docs/glossary.md) — 용어집

---

## ⚠️ 면책

- 본 코드는 **교육/프로토타이핑용 스켈레톤**입니다. ADI/Xilinx의 실제
  벤더 라이브러리(no-OS, ADRV9025 API, PetaLinux BSP)와 O-RAN 인증
  스택은 포함하지 않으며, 연동 지점을 `TODO`/스텁으로 표시했습니다.
- O-RAN.WG4 사양, ADI ADRV9025 데이터시트, Xilinx UG 문서는 각
  벤더에서 별도로 확보해야 합니다.
