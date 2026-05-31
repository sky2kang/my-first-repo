# 06. 타겟(Zynq) 빌드 & 배포

## 1. 전체 플랫폼 빌드 흐름 (Xilinx)

O-RU 한 대를 올리려면 소프트웨어 외에 **PL 디자인 + 부트 이미지**가
필요합니다. 본 저장소는 ③ 애플리케이션 계층입니다.

```
① Vivado     PL 디자인(JESD204 IP, DFE, eCPRI) → hardware.xsa
② PetaLinux  XSA 기반 BSP/커널/rootfs/u-boot/디바이스트리 생성
③ 이 저장소   aarch64 크로스 컴파일 → oru_app  (rootfs에 포함)
④ 부트이미지  BOOT.BIN(FSBL+PMU+ATF+bitstream+u-boot) + image.ub
⑤ 배포       SD/eMMC/네트워크 부팅 → 보드에서 oru_app 실행
```

## 2. 크로스 컴파일 (이 저장소)

PetaLinux/Yocto SDK 또는 Linaro aarch64 툴체인이 필요합니다.

```bash
# 예: PetaLinux SDK 환경설정 후
source <sdk-install>/environment-setup-cortexa72-cortexa53-xilinx-linux

# 또는 직접 툴체인 지정
cmake -S . -B build/target \
  -DORU_TARGET=ON \
  -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux.cmake
cmake --build build/target -j
```

`cmake/aarch64-linux.cmake` 는 컴파일러 prefix만 바꾸면 되도록
템플릿으로 제공됩니다. `ORU_TARGET=ON`이면 HAL이 실제 드라이버 경로
(SIM 비활성)로 빌드됩니다.

## 3. PetaLinux에 통합하는 두 가지 방법

1. **앱 레시피**: `petalinux-create -t apps --name oru-app` 후, 본
   저장소를 소스로 하는 `.bb` 레시피에서 CMake 빌드 → rootfs 포함.
2. **수동 복사**: 크로스 빌드한 `oru_app` 바이너리와 `config/`를 rootfs의
   `/opt/oru/` 에 복사.

## 4. 보드에서 실행

```bash
# 보드 콘솔
/opt/oru/oru_app --config /opt/oru/oru-config.ini
```

부팅 시 자동 실행하려면 systemd 유닛(`scripts/oru-app.service` 템플릿 참고)을
설치하세요.

## 5. 함께 띄워야 하는 서비스 (target rootfs)

- `ptp4l` / `phc2sys` — S-plane (PetaLinux의 linuxptp 패키지)
- `netopeer2-server` + `sysrepo` — M-plane (선택, 본 저장소는 스텁 연동 지점 제공)

## 6. 디버깅 팁

- JESD lock 실패 → SYSREF/디바이스클럭, IP 파라미터 vs TES 프로파일 불일치 확인
- PTP unlock → 네트워크 경로의 PTP 인지 스위치(BC/TC) 여부, VLAN/PCP
- U-plane IQ 깨짐 → 압축 헤더(udCompHdr) 불일치, 엔디안, PRB 매핑
