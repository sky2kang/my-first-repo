# 02. 시작하기 (개발 환경 & 첫 빌드)

## 1. 두 가지 빌드 타겟

| 타겟 | 용도 | 컴파일러 | RF 동작 |
|------|------|----------|---------|
| **host** | PC에서 로직/프로토콜 개발·단위테스트 | 시스템 gcc/clang | 스텁(시뮬레이션) |
| **target** | 실제 Zynq MPSoC 보드 | aarch64 크로스 컴파일러 | 실제 ADRV9025 |

처음에는 **host 모드**로 시작하세요. 하드웨어 없이 전체 소프트웨어
흐름(상태머신, plane 오케스트레이션, eCPRI 패킷 빌드)을 검증할 수 있습니다.

## 2. 호스트 개발 환경 준비

필요 패키지 (Ubuntu/Debian 기준):

```bash
sudo apt update
sudo apt install -y build-essential cmake git
```

## 3. 첫 빌드 & 실행

```bash
git clone <this-repo>
cd zynq-adrv9025-p5g-oru

# 빌드 (host 모드, 기본값)
./scripts/build.sh

# 실행 (시뮬레이션)
./build/host/src/app/oru_app --config config/oru-config.ini
```

기대 출력 (요약):

```
[INFO ] oru_app  starting (build=host, sim=on)
[INFO ] hal      ADRV9025 init (SIM) ... ok
[INFO ] jesd204  link bring-up (SIM) ... LANES=4 lock=1
[INFO ] splane   PTP waiting for lock (SIM) ... LOCKED
[INFO ] mplane   applying config: carrier=n78 bw=100MHz scs=30kHz
[INFO ] fronthaul eCPRI listener ready (U-plane/C-plane)
[INFO ] oru_app  state -> OPERATIONAL
```

## 4. 테스트 실행

```bash
cd build/host
ctest --output-on-failure
```

## 5. 다음 단계

- 프론트홀 패킷 구조를 이해하려면 → `03-oran-fronthaul-7.2x.md`
- RF/JESD204를 실제 하드웨어로 올리려면 → `04-adrv9025-jesd204.md`,
  `06-build-and-deploy.md`
- 무엇을 어떤 순서로 구현할지 → `07-roadmap.md`

## 6. 실제 하드웨어로 갈 때 필요한 외부 자산 (별도 확보)

- **Xilinx Vivado / Vitis / PetaLinux** (PL 디자인 + BSP + 임베디드 Linux)
- **ADI ADRV9025 평가 패키지**: API 소스, TES로 생성한 **프로파일/.bin**
- **JESD204 IP** (Xilinx) 또는 ADI no-OS JESD 프레임워크
- **linuxptp**, **sysrepo/netopeer2** (S/M-plane)
- O-RU 보드 (예: ADI ADRV9025 EVB + Xilinx ZCU102/ZCU111 FMC 연결, 또는
  통합 O-RU 보드)
