# KPI Monitor — 리눅스 / WSL 브릿지 (CANable · SocketCAN)

CANable은 리눅스에서 **SocketCAN**(`can0`)으로 잡힙니다. (PCAN은 Windows 드라이버라 CANable에 안 됨.)
이 앱은 리눅스에서 Qt의 **socketcan** 플러그인으로 `can0`을 읽으므로 코드 수정 없이 동작합니다.

> 검증됨: WSL2(커스텀 커널, CAN 지원)에서 빌드 → `[main] CAN online: "online: socketcan/can0"` 로 연결, UDP 전송 정상.

---

## 1. 빌드 (한 번)

```bash
# Qt6 개발 패키지 (없으면)
sudo apt install -y cmake ninja-build g++ \
     qt6-base-dev qt6-declarative-dev qt6-serialbus-dev qt6-serialport-dev

# 빌드 (프로젝트 루트에서)
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux
```
> 산출물: `build-linux/KpiProjectApp` (+ 옆에 `config.json`·`maps/` 자동 복사).

---

## 2. CANable → can0 연결

### A) candleLight 펌웨어 (권장 · gs_usb)
USB로 꽂으면 자동으로 `can0` 이 생깁니다. 비트레이트만 올려주면 됩니다:
```bash
sudo ip link set can0 up type can bitrate 500000
ip -br link show can0          # state UP 확인
```

### B) slcan 펌웨어 — 두 가지 방법
**방법 1: 앱이 직접 slcan (slcand·sudo 불필요, 권장)** — Windows와 동일한 직렬 slcan 경로:
```bash
KPI_MODE=bridge KPI_CAN_PLUGIN=slcan KPI_CAN_DEVICE=/dev/ttyACM0 ./build-linux/KpiProjectApp
```
**방법 2: SocketCAN 경유 (slcand)**:
```bash
sudo slcand -o -c -s6 /dev/ttyACM0 can0   # s6 = 500kbps
sudo ip link set can0 up
# 그 뒤 ./run-bridge.sh  (또는 KPI_CAN_PLUGIN=socketcan KPI_CAN_DEVICE=can0)
```

> **WSL2 사용 시**: CANable USB를 WSL에 붙여야 합니다 (`usbipd`).
> - Windows(관리자 PowerShell): `winget install usbipd` → `usbipd list` → `usbipd bind --busid <X-Y>` → `usbipd attach --wsl --busid <X-Y>`
> - WSL: `ip link` 에 `can0` 가 보이면 위 A) 명령으로 올림. (이 WSL 커널은 CAN/gs_usb 지원 확인됨.)

---

## 3. 실행 (브릿지)

```bash
./run-bridge.sh            # can0 자동 up + 브릿지 실행 (헤드리스)
```
또는 수동:
```bash
KPI_MODE=bridge KPI_CAN_PLUGIN=socketcan KPI_CAN_DEVICE=can0 ./build-linux/KpiProjectApp
```
- 아이패드를 **같은 Wi-Fi / 핫스팟**에 두고, `build-linux/config.json` 의 `bridge_host` 를 이 노트북 IP로 설정.
- 중지: 터미널에서 **Ctrl-C**.

### 비트레이트 바꾸기
```bash
CAN_IF=can0 BITRATE=250000 ./run-bridge.sh
```

---

## 4. 참고 / 트러블슈팅

- **`socketcan: Cannot apply parameter: BitRateKey` 경고는 정상**입니다. SocketCAN은 비트레이트를 인터페이스(`ip link`)에서 설정하고 앱 API로는 못 바꾸기 때문 — 무시하세요. (위 2번에서 `ip link ... bitrate` 로 설정함.)
- **`peakcan: Cannot load library pcanbasic`** 도 정상(리눅스엔 PCAN 없음). 앱은 자동으로 `socketcan` 로 넘어갑니다.
- CAN이 없으면 자동으로 **가상버스(데모)** 로 동작합니다.
- 데모/단독(UI) 실행: WSLg(GUI)에서 `./build-linux/KpiProjectApp` (KPI_MODE 없이) — 가상 데이터로 전체 UI.
- **다른 리눅스 PC로 배포**: 그 PC에 `qt6-*` apt 설치 후 바이너리 복사, 또는 `linuxdeployqt`/AppImage 로 자립 번들 생성.
- 모드/포트 등 설정은 `build-linux/config.json` (Windows판과 동일 키: `mode`, `bridge_host`, `snapshot_port`, `command_port`, `snapshot_hz`).
