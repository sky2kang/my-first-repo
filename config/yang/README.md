# YANG 모델 (placeholder)

실제 O-RU의 M-plane은 O-RAN Alliance WG4가 정의한 YANG 모델을 사용합니다.
라이선스/배포 정책상 이 저장소에는 모델 파일을 포함하지 않으며, 아래
모듈을 O-RAN 사양 배포본에서 받아 이 디렉터리에 두고 sysrepo에 설치하세요.

권장(대표) 모듈:

- `o-ran-uplane-conf`      — 캐리어/엔드포인트/배열 등 U-plane 설정
- `o-ran-module-cap`       — O-RU 능력(밴드/대역폭/안테나)
- `o-ran-sync`             — S-plane(PTP/SyncE) 설정/상태
- `o-ran-fm`               — 결함/알람(fault management)
- `o-ran-performance-management` — 성능 카운터
- `o-ran-processing-element`, `o-ran-interfaces` — 전송/인터페이스

설치 예 (target):

```bash
sysrepoctl -i o-ran-uplane-conf.yang
netopeer2-server -d
```

본 저장소의 `src/mplane`는 이 모델들의 change 콜백을 받아
`mplane_apply_config()` 경로로 연결하는 자리를 제공합니다 (현재는 INI 파일
기반 스텁).
