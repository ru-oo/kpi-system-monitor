# 유아용 전동차 기반 자율주행 시스템 프로젝트

SLAM · AI 인식 · CAN 모니터링 · KPI 검증 / 2026년 5월 ~ 6월 (6주)

이 문서는 통합 최신본이다. CAN 신호 정의는 `valeo_project_can.dbc`를 정본으로 반영했고, 내비게이션·위치 추정은 현장 재평가에 따른 전환(원래 §7) 내용을 본문에 녹였다. 전환의 배경과 근거, 적용 게이팅은 문서 끝의 "설계 변경 기록"에 보존한다.

---

## 1. 프로젝트 개요

### 1.1 배경 및 목적

본 프로젝트는 유아용 전동차를 자율주행 플랫폼으로 개조하여, 자동차 산업에서 활용되는 핵심 기술을 소규모 하드웨어 환경에서 직접 구현하고 검증한다. 위치 추정, AI 객체 인식, 적응형 Fail-safe, CAN 기반 모니터링까지의 자율주행 파이프라인을 6주 안에 완성한다.

방향성은 "AI 연산 결과를 CAN 통신으로 외부 PC에 송출하고, KPI 수치를 측정하며 개선해 가는 과정 자체가 학습 목표"라는 데 있다. 따라서 단순히 동작하는 시스템을 만드는 것이 아니라, AI 모델의 성능을 단계적으로 끌어올려 그 변화를 KPI 수치 변화로 증명하는 것을 최우선 목표로 한다. 이 핵심 목표는 내비게이션 계층의 설계가 바뀌어도 변하지 않는다.

### 1.2 테스트 환경

시험 구간은 대구 수성 알파시티 일대의 직선 양방향 1차선 도로 약 250m이다. 현장 항공·근접 사진 재평가 결과 확인된 실제 특성은 다음과 같다.

| 항목 | 내용 |
| --- | --- |
| 장소 | 대구 수성 알파시티(자율주행특화지구) 내부도로, 약 250m 구간 |
| 도로 특성 | 아스팔트 포장, 차선 표시 풍부·또렷(황색 중앙선·백색 차선·화살표·횡단보도) |
| 측면 환경 | 건물 배치 비균질 — 외벽이 보이는 구간과 개활지(주차장·공터·녹지)가 번갈아 나타남(특징 공백 구간 존재) |
| 주요 장애물 | 도로 양옆 밀집 주차 차량, 일부는 주행 차로 가장자리 잠식(준정적 객체) |
| 실차량 통행 | 없음 (테스트 전용 구간) |
| 조명·노면 | 주간 자연광, 직사광선 역광 가능. 흐리고 젖은 노면 조건 발생 |
| 주행 속도 | 3 ~ 5 km/h |
| 지도 자원 | 국가 정밀도로지도(2023년 구축, 벡터)가 본 도로를 커버 |

기본 직선 구간을 기본 검증 구간으로 두되, 곡선·코너가 포함된 추가 구간을 별도로 선정해 각각 독립 맵으로 테스트한다. 측면 건물이 비균질하고 주차 차량이 밀집해 있다는 점이 위치 추정·제어 설계의 전제를 바꾸었으며, 그 결과가 본문 전반(§2.5·§2.10·§3.2~§3.6)에 반영돼 있다.

### 1.3 프로젝트 목표와 기술 구성

#### 1.3.1 프로젝트 목표

유아용 전동차가 사용자가 지정한 목적지까지 차로를 따라 자율로 이동하면서 갓길 주차 차량을 회피·통과하는 시스템을 구현한다. 위치 추정은 비전 차선 추종과 공식 HD 차로 중심선 기준 경로, 그리고 휠 오도메트리·IMU·GPS를 융합한 상시 추정을 기둥으로 한다. 단순 동작이 아니라 AI 인식 모듈의 성능을 단계적으로 끌어올리는 과정과 그 변화를 KPI 수치로 증명하는 것이 핵심 학습 목표다.

#### 1.3.2 자율주행 파이프라인

| # | 단계 | 목표 | 핵심 기술 | 완료 기준 |
| --- | --- | --- | --- | --- |
| 1 | Localization | 위치 추정 및 목적지 설정 | robot_localization 이중 ekf(휠·IMU·GPS) + 비전 차선 + HD 차로 중심선 (SLAM/AMCL은 보조) | 특징 구간 정적 오차 50cm 이내, 차로 중앙 편차 관리 |
| 2 | Planning | 경로 탐색 및 추종 | 차로 중심선 기준 경로 (Hybrid A*는 주차 진입 기동 한정) | 목표 도달·정지 판정 |
| 3 | Perception | AI 객체 인식 및 회피 | YOLO26 + LiDAR DBSCAN(라이브 코스트맵 실시간 장애물) | 주차 차량 감지율 8/10 이상 |
| 4 | Control | 제어 | Pure Pursuit + PID, 비전 차선(중앙선) 횡방향 보정 | 추종 오차 30cm 이내 |
| 5 | Safety | AI 성능 강화 및 운영모드 FSM | TensorRT INT8 + 운영모드 상태기계 + MRM | INT8 가속 3배 이상(416 해상도 기준) |
| 6 | Monitoring | CAN 기반 KPI 모니터링 | python-can + SavvyCAN + rosbag2 | CAN 송출 지연 10ms 이내 |

### 1.4 AI 성능 강화 관점의 KPI 설계

KPI 비교 실험은 "AI를 쓰면 좋아진다"가 아니라 "같은 AI를 이렇게 최적화하면 이만큼 좋아진다"를 증명하는 구조로 설계한다. 이 부분은 본 프로젝트의 핵심이며 내비게이션 전환의 영향을 받지 않는다.

| 실험 | 비교 대상 | 측정 KPI | 기대 결과 |
| --- | --- | --- | --- |
| A | YOLO26s FP32 vs FP16 vs INT8 | 추론 지연(ms), mAP, 회피 성공률 | 양자화 단계별 가속 효과 정량화 |
| B | YOLO26s(Heavy) vs YOLO26n(Light) | 추론 지연, mAP, 정확도-속도 트레이드오프 | 엣지 적합 모델 사이즈 결정 근거 |
| C | 고정 해상도(640) vs 동적 해상도(640/416/320) | GPU 부하, 추론 지연, 감지율 | 엣지 최적화 기법의 실효성 증명 |

세 실험 모두 동일 구간·동일 주차 배치에서 10회 반복하여 평균과 분산을 함께 기록한다.

---

## 2. 하드웨어 구성

### 2.1 기반 차량: 유아용 전동차

12V SLA 배터리 기반의 시판 유아용 전동차를 개조한다. 12V 단일 전원으로 외부 분기가 용이하고, 전륜 조향·후륜 구동의 아커만 구조라 자율주행 차량의 운동학 모델을 그대로 적용할 수 있다.

| 항목 | 내용 |
| --- | --- |
| 차종 | 대호토이즈 12V 유아용 전동차 |
| 조향 | 전륜 아커만 조향 → FT5330M 서보로 교체 |
| 구동 | 후륜 12V DC 모터 → BTS7960 드라이버 PWM 제어 |
| 배터리 | 12V 7Ah SLA(메인, 모터 전용). 컴퓨팅 계열은 별도 보조 배터리로 분리 |
| 운용 속도 | 3 ~ 5 km/h |

개조 절차는 다음 순서로 진행한다. 1단계로 기존 수동 조향부를 제거하고 FT5330M 서보(PWM 50Hz, 35kg·cm)를 장착한다. 2단계로 후륜 DC 모터 제어를 BTS7960(43A 연속, 25kHz PWM)으로 교체한다. 3단계로 전원을 물리 분리한다 — 메인 12V SLA는 BTS7960→후륜 모터 전용으로 직결하고, Jetson은 65W PD 보조배터리(20,000mAh)와 PD 트리거 케이블(15V/3A→5.5×2.5mm 배럴잭)로 별도 공급하여, 모터의 순간 전류가 컴퓨팅에 영향을 주지 않게 한다. STM32·BNO055·서보 등 5~6V 페리페럴은 메인 12V SLA에서 5V/5A UBEC로 강압 공급한다. 4단계로 STM32F446RE를 차량에 고정하고 Jetson과 CAN으로 연결한다(STM32 측 bxCAN+TJA1051, Jetson 측 USB-CAN). 5단계로 카메라·LiDAR를 후방 알루미늄 마스트(1.2m)에 마운트해 시점을 확보한다.

### 2.2 메인 컴퓨터: Jetson Orin Nano Super Developer Kit

AI 추론·위치 추정·경로 계획·ROS2 노드 전체를 담당한다. JetPack 6.2 SUPER 모드 기준 67 TOPS를 제공한다.

| 항목 | 사양 |
| --- | --- |
| 모델 | NVIDIA Jetson Orin Nano Super Dev Kit (8GB) |
| GPU | 1024-core Ampere + 32 Tensor Core |
| AI 성능 | 67 TOPS (SUPER 모드, JetPack 6.2 이상) |
| CPU | 6-core ARM Cortex-A78AE |
| RAM | 8GB LPDDR5 (102 GB/s) |
| 전력 | 7W ~ 25W |
| 저장 | NVMe SSD 256GB |
| OS/SDK | Ubuntu 22.04 + JetPack 6.2 (TensorRT 10.x, CUDA 12.x) |

8GB 통합 메모리에서 인지·위치추정·제어를 동시 운영하기 위해 부팅 시 GUI 비활성화(multi-user.target, 약 500MB 확보), ZRAM 비활성화 및 swap을 NVMe로 분리(4GB), YOLO26 단일 모델 로드(Heavy↔Light 전환 시 사전 변환 `.engine` 동적 swap), jtop 상시 모니터링을 적용한다.

### 2.3 LiDAR: RPLIDAR C1

| 항목 | 사양 |
| --- | --- |
| 측정 방식 | DTOF |
| 측정 거리 | 0.05 ~ 12m(반사율 70%), 0.05 ~ 6m(반사율 10%) |
| 스캔 주파수 | 8 ~ 12Hz (일반 10Hz) |
| 측정 주파수 | 5,000Hz, 각도 분해능 0.72° |
| 정확도 | ±30mm |
| 인터페이스 | UART 460800bps (USB 어댑터 포함) |
| 보호 등급 | IP54, 주변광 한계 40,000 lux |

LiDAR의 12m 사정거리는 본 환경에서 두 역할을 한다. 첫째는 갓길 주차 차량 감지로, 진행 방향 8~10m 전방에서 감지해 회피·정지 반응 시간을 확보한다(반사율 10% 어두운 차량도 6m 보장). 둘째는 특징 구간에서의 측면 외벽 스캔 매칭 보조다. 다만 측면이 비균질하므로 외벽 매칭은 보조 보정 수단으로만 쓰고, 위치 추정의 기둥은 융합 추정과 비전 차선이 담당한다(§3.2). 정오 직사광선 시간대를 피해 오전·오후에 실험한다.

### 2.4 카메라

전방 1대는 Jetson에 직결하여 YOLO26 객체 인식을 담당하고, 보조 2대(좌·우 차선)는 Raspberry Pi 5에 연결해 BEV·OpenCV 처리 결과(편차·신뢰도)만 UDP로 Jetson에 전송한다. 영상 자체는 전송하지 않아 대역폭을 아낀다. Pi5와 Jetson은 기가비트 LAN 직결, 정적 IP(Jetson 192.168.10.1 / Pi5 192.168.10.2)다.

| 카메라 | 모델 | 연결 | 역할 |
| --- | --- | --- | --- |
| 전방(메인) | USB 웹캠 | USB 직결 | YOLO26 객체 인식 |
| 좌측 | IMX708 CSI wide | Raspberry Pi 5 | 좌 차선 감지 (BEV + OpenCV) |
| 우측 | IMX708 CSI wide | Raspberry Pi 5 | 우 차선 감지 (BEV + OpenCV) |

차체 폭(약 70cm)이 차로 폭(3~3.5m)의 1/4 수준이라 승용차 시점 차선 알고리즘을 그대로 쓰기 어렵다. 좌·우 보조 카메라를 마스트 좌우 끝에 두고 각각 BEV 변환(호모그래피 캘리브레이션, HSV 필터링) 후 차선을 분리 추출하고, 두 결과를 합쳐 차로 중심 편차(m)를 계산해 제어에 전달한다. 출력 20Hz, 평균 처리 25ms 이내(Pi5 CPU 1코어, 좌·우 각각)를 목표로 한다. 비전 차선은 본 설계에서 횡방향의 1차 기준으로 승격되며, 특히 황색 중앙선을 기준으로 삼는다(§3.4.3).

### 2.5 IMU: BNO055

SLAM 코너 보정·오도메트리 보완을 넘어, 융합 위치 추정(ekf)의 상시 백본 한 축으로 사용한다. 절대 자세(yaw)와 함께 각속도(yaw rate)를 제공하는 것이 핵심이다. 아커만 차량은 휠 오도메트리만으로 회전을 안정 추정하기 어려운데, IMU 각속도를 융합하면 코너 자세가 안정화되고 GPS 업데이트 사이를 데드레커닝으로 메운다. 노이즈가 큰 선가속도는 융합에서 제외하고 yaw·yaw rate 중심으로 운용한다.

| 항목 | 내용 |
| --- | --- |
| 모델 | Bosch BNO055 9축 IMU |
| 연결 | I2C (STM32F446 하드웨어 I2C, 클럭 스트레칭 허용, 100kbps). UART 미사용 |
| 출력 | 쿼터니언·오일러각·각속도·선가속도 100Hz |

UART 모드는 모듈 PS0/PS1 핀 변경에 납땜이 필요해 채택하지 않았다. BNO055의 클럭 스트레칭은 STM32 하드웨어 I2C가 지원하므로 허용 설정하고 적절한 풀업으로 안정성을 확보한다.

### 2.6 휠 인코더

후륜 양 바퀴 축에서 이동 거리·속도를 측정해 `/odom`으로 변환한다.

| 항목 | 내용 |
| --- | --- |
| 모델 | CHR-RS-555-ABHL (RS-550 모터 + 내장 홀센서 엔코더) |
| 엔코더 | 17 PPR(모터축), 쿼드러처 68 CPR |
| 기어박스 | SZH-GNP208-4 + Pinion Gear 2 |
| 연결 | STM32 TIM2/TIM3 인코더 모드(A/B 쿼드러처) |
| 분해능 | 17PPR × 4체배 × 감속비 ÷ 타이어 둘레 (감속비 확정 후 재계산 필요) |
| 출력 | /odom 20Hz |

### 2.7 STM32F446RE Nucleo-64

저수준 하드웨어 제어를 전담한다.

| 역할 | 구현 |
| --- | --- |
| PWM | TIM1: BTS7960 모터 25kHz, TIM4: FT5330M 서보 50Hz |
| 인코더 | TIM2/TIM3: 좌우 17PPR 쿼드러처 카운팅 |
| IMU | I2C2: BNO055 100Hz 수신(하드웨어 I2C, 클럭 스트레칭 허용) |
| Jetson 통신 | CAN1(bxCAN) + TJA1051, 500kbps. 0x010 수신, 0x020·0x021 송신 |
| Watchdog | 0x010(제어 명령) 500ms 이상 미수신 또는 Bus-off 감지 시 독립 정지 |

차내 CAN 프레임 정의는 `valeo_project_can.dbc`를 정본으로 한다. IMU 자세 프레임(0x021)은 ekf용 각속도(IMU_GyroZ_dps)를 포함하도록 재패킹되었으며, 캘리브레이션 상태는 런타임 프레임에서 제외했다(상세는 §2.9.1·§3.6.1).

### 2.8 모터 드라이버 및 서보

| 부품 | 모델 | 사양 |
| --- | --- | --- |
| 모터 드라이버 | BTS7960 | 43A 연속, PWM 25kHz |
| 조향 서보 | FT5330M | 35kg·cm @6V, 메탈 기어, PWM 50Hz |
| 전원 변환 | 5V/5A UBEC | 12V → STM32·BNO055·서보 (Jetson은 PD 보조배터리 직결) |

### 2.9 CAN 통신 하드웨어

차내·차외 통신을 모두 CAN으로 통일한다. 차내 CAN은 Jetson↔STM32 제어·센서 교환에, 차외 CAN은 Jetson의 AI 연산 결과·차량·시스템 상태를 외부 모니터링 PC에 송출하고 PC UI 명령·목적지를 받는 데 쓴다.

| 항목 | 내용 |
| --- | --- |
| Jetson 측 | USB-CAN 어댑터 (CANable v2) |
| PC 측 | USB-CAN 어댑터 (CANable Pro) |
| STM32 측 | bxCAN + TJA1051 |
| 라이브러리 | Jetson: python-can / PC: SavvyCAN |
| 속도 | 500kbps |
| 종단 저항 | 양 끝단 120Ω |

구현상 차내·차외는 동일한 500kbps 스택을 공유하며, 현재 운용은 CANable(can1) 단일 버스 기준이다. CAN ID로 메시지를 구분한다.

#### 2.9.1 차내 CAN 프레임 (DBC 정본)

| ID | DBC 메시지 | 방향 | 주기 | 주요 신호(스케일/단위) |
| --- | --- | --- | --- | --- |
| 0x010 | STM32_Control_Command | Jetson→STM32 | 20ms | Target_Speed_mmps(1, mm/s, int16), Target_Steering_deg(0.01°, int16), Driving_State_Cmd, Alive_Counter_010, Control_Flag(255=E_Stop/1=Encoder_Reset/0=Normal), Reserved |
| 0x020 | STM32_Encoder_Feedback | STM32→Jetson | 20ms | Left_Count(int32), Right_Count(int32) — 카운터 없음 |
| 0x021 | STM32_IMU_Feedback | STM32→Jetson | 20ms | IMU_Yaw_deg(0.01°), IMU_GyroZ_dps(0.01°/s), IMU_Roll_deg(0.01°), IMU_Pitch_deg(0.01°) — 캘리브레이션 상태·카운터 없음 |

차내 목표 속도 단위는 mm/s, 조향각 스케일은 0.01°다(차외 0x100/0x101의 각도 0.1°와 혼동 금지). 비상정지는 UI가 직접 0x010을 쓰지 않고, 액추에이터 정지는 0x010 Control_Flag=255로 내린다.

### 2.10 GPS

MicoAir MG-A01 M10 GPS(u-blox, UART) + CP2102 USB-TTL 컨버터, ROS2 nmea/navsat 계열 드라이버를 사용한다(정확도 2~5m, 5~10Hz). 핸드폰 NMEA 스트리밍은 발열에 따른 USB 끊김 위험으로 채택하지 않는다.

GPS의 역할은 두 가지다. 첫째, 부팅 후 첫 fix를 지도 원점(datum)으로 자동 설정한다(1회). 둘째, 이후 모든 fix를 ekf에 연속 융합해 전역 위치를 보정한다. 절대 정확도가 2~5m이므로 정밀 위치원이 아니라 휠 오도메트리·IMU 드리프트를 수 m 안으로 묶는 거친 전역 닻으로 쓴다. 차로 내 정밀 횡방향 정렬은 GPS가 아니라 비전 차선이 담당한다.

### 2.11 부품 목록

| 부품 | 모델 |
| --- | --- |
| 기반 차량 | 대호토이즈 12V 전동차 |
| 메인 컴퓨터 | Jetson Orin Nano Super Dev Kit |
| 보조 컴퓨터 | Raspberry Pi 5 8GB |
| NVMe SSD | M.2 NVMe 256GB |
| LiDAR | RPLIDAR C1 |
| 카메라 | IMX708 CSI ×2 + USB 웹캠(전방) |
| IMU | BNO055 |
| 인코더 | 홀센서 17PPR(CHR-RS-555-ABHL) |
| MCU | STM32F446RE Nucleo-64 |
| 모터 드라이버 | BTS7960 |
| 서보 | FT5330M 35kg |
| UBEC | 5V/5A |
| PD 보조배터리 | Morui MT-65 65W 20,000mAh |
| PD 트리거 케이블 | USB-C → 5.5×2.5mm 배럴잭 (15V/3A) |
| CAN 어댑터 | CANable v2 / Pro |
| GPS | MicoAir MG-A01 M10 + CP2102 |
| 마운트 | 알루미늄 마스트 |

---

## 3. 소프트웨어 및 기술 스택

### 3.1 운영체제 및 미들웨어

| 항목 | 버전 | 비고 |
| --- | --- | --- |
| Ubuntu | 22.04 LTS | JetPack 6.2 기반, ROS2 Humble 지원 |
| ROS2 | Humble | Nav2·robot_localization 지원 |
| Python | 3.10 | 기본 탑재 |
| JetPack | 6.2 | CUDA 12.x, TensorRT 10.x |
| TensorRT | 10.x | YOLO26 INT8 엣지 최적화 핵심 |

### 3.2 목표 1: 위치 추정

위치 추정의 기둥은 휠 오도메트리·IMU·GPS를 융합한 상시 추정과 비전 차선 추종, 그리고 공식 HD 차로 중심선 기준 경로다. SLAM과 AMCL은 폐기하지 않고 특징 구간 보조 보정·KPI 산출 소스로 강등한다.

#### 3.2.1 융합 위치 추정 (robot_localization 이중 ekf)

robot_localization의 ekf를 이중으로 구성한다. 첫째 ekf는 휠 오도메트리와 IMU를 융합해 `odom → base_link`를 연속 발행한다. 둘째 ekf는 GPS를 더해 `map → odom`을 발행하며, navsat_transform이 GPS·방위·전역 추정치를 받아 `/odometry/gps`를 둘째 ekf에 공급한다. 프레임 트리는 REP-105의 `map → odom → base_link`를 따른다. map 원점은 주행 시작점(datum)에 두고 그 지점 기준 ENU(동/북, use_local_cartesian) 평면으로 정의한다. UTM 격자는 본 구역에서 약 0.9° 회전돼 250m 끝에서 수 m 오차가 나므로 datum 기준 ENU로 통일한다.

방위 소스는 하나로 일원화한다. M10 나침반을 navsat_transform 방위 정렬 기준으로 쓰고(마스트 장착·캘리브레이션으로 모터 EMI 저감), BNO055는 각속도·단기 자세를 담당한다. 나침반 0=북을 ENU 0=동에 맞추기 위해 yaw_offset(π/2)을, 진북·자북 차이 보정을 위해 지역 자기편차(대구 약 9°W 내외)를 설정한다.

좌표 원점은 사람이 입력하지 않고 시스템이 자동 확정한다. datum 관리 노드가 첫 GPS fix를 수 초 평균해 원점으로 고정하고 navsat_transform에 set_datum으로 넣은 뒤, 그 원점을 CAN(0x108 Map_Datum)으로 1Hz 송신한다. 모니터링 UI는 HD 차로를 위·경도로 보관하다가 수신 원점 기준으로 런타임에 ENU로 변환해 표시한다.

#### 3.2.2 SLAM·AMCL (보조)

slam_toolbox 점유 격자는 주차 차량을 제거한 베이스맵을 특징 구간 보조 보정·모니터 표시용으로 유지한다. nav2_amcl(min_particles=500, max_particles=2000, update_min_d=0.25m)은 특징 구간에서 위치 보정과 KPI 산출 소스로 동작한다. 맵은 nav2 map_saver로 구간별 `구간이름.pgm`+`구간이름.yaml`(원점 좌하단, 점유 임계값)로 저장하며, `.posegraph`/`.data`는 재매핑 보관용으로 운영 로드 대상이 아니다. 맵은 구간 이름·버전으로 관리하고 버전은 Map_Info(0x10C)로 송출해 UI와 일치 검증한다.

#### 3.2.3 목적지 설정

개발 단계에서는 RViz2 "2D Nav Goal"이나 NavigateToPose로 직접 전송한다. 운영 단계에서는 모니터링 PC UI에서 지도 위 한 점을 클릭하면 그 점을 가장 가까운 차로 중심선에 스냅하고 월드 좌표(ENU m)로 변환해 목적지로 전송한다. 클릭 목적지는 UI_Goal_Pose(0x201)와 UI_Command(0x200) Set_Destination으로 Jetson에 전달된다(§3.6.3).

### 3.3 목표 2: 경로 탐색 및 추종

전역 경로는 국가 정밀도로지도 벡터에서 추출한 차로 중심선을 기준 경로로 쓴다. 본 구간은 단일 회랑이므로 "중심선을 따라가다 목적지 투영 지점에서 정지"로 충분하며, 교차로·분기에서만 a1_node/a2_link 그래프 탐색으로 갈래를 이어붙인다. HD 지도는 차로 중심선이라는 기하 정보를 기준 경로로 쓰고 화면에 표시하는 얕은 수준으로만 활용한다(풀 lanelet2 의미 라우팅은 하지 않음). HD 변환·정합이 초반에 안정화되지 않으면 수동 1회 주행으로 경로를 기록하는 teach-and-repeat 중심선으로 대체한다.

제어는 Pure Pursuit이 중심선을 추종하고 비전 차선 편차로 횡방향을 보정하며, PID는 잔차 보정에만 쓴다. look-ahead는 속도 비례(3km/h→0.4m, 5km/h→0.7m)다. 장애물 대응은 자유공간 재계획이 아니라 차로 내 정지/넛지를 기본으로 하고, 반대 차로가 비었음을 비전·LiDAR로 확인하면 중앙선을 잠깐 넘어 추월 후 복귀하는 행동을 선택적으로 둔다(시험 구간 한정, 안전 판단 불확실 시 정지-대기로 강등). Hybrid A*는 도로 주행에서 은퇴하고 목적지가 차로 밖 주차 칸일 때 마지막 진입 기동에만 선택적으로 쓴다. 도착 판정은 융합 위치로 계산한 기준 경로상 진행 거리가 목적지 반경에 드는 것으로 한다. 출력 `/cmd_vel`(20Hz)은 stm32_bridge를 거쳐 CAN 0x010으로 STM32에 전달된다.

### 3.4 목표 3: AI 객체 인식 및 회피

#### 3.4.1 객체 인식: YOLO26

Ultralytics가 2025년 9월 발표한 YOLO26을 사용한다. NMS 제거로 후처리 없는 end-to-end 추론, DFL 제거로 단순해진 TensorRT 변환·양자화 호환성이 특징이다. COCO 사전학습 가중치를 그대로 쓰며 fine-tuning은 6주 일정상 본 범위에서 제외한다(인식 미달 시 5주차 후반 보강 검토).

| 모델 | 크기 | mAP COCO | 용도 |
| --- | --- | --- | --- |
| YOLO26n | 약 5MB | 39.8% | Light 모드(Fail-safe) |
| YOLO26s | 약 19MB | 47.2% | Heavy 모드(정상) |

#### 3.4.2 LiDAR 장애물 감지: DBSCAN

LiDAR 2D 포인트클라우드를 DBSCAN(eps=0.3m, min_samples=5)으로 클러스터링해 장애물 중심(x,y)과 바운딩 박스를 추출하고, 라이브 코스트맵을 갱신한다. 양옆 주차 차량은 지도 앵커가 아니라 라이브 코스트맵의 실시간 장애물(raytrace clearing 적용)로만 처리한다 — 위치가 매번 다른 준정적 객체이므로 지도에 굽지 않는다. CPU 단독 동작이라 AI 배제 단계에서도 쓸 수 있다.

#### 3.4.3 차선 감지: BEV + OpenCV

좌·우 보조 카메라가 Pi5에서 BEV 변환·차선 추출 후 픽셀 편차와 신뢰도만 UDP 20Hz로 Jetson에 보낸다. Jetson의 lane_fusion 노드가 두 결과를 합쳐 `/lane/center_deviation`으로 발행한다. Pi5는 ROS2 DDS 부담을 피해 raw UDP 소켓으로 송신한다. 비전 차선은 look-ahead 보정에서 횡방향 1차 기준으로 승격되며, 오른쪽 가장자리 차선이 주차 차량에 자주 가려지므로 횡방향 기준을 황색 중앙선에 둔다("중앙선에서 일정 오프셋 우측 유지"). 젖은 노면 반사 강건성은 1차 테스트로 확인하고 부족하면 노출 고정·ROI 축소·신뢰도 임계 상향을 적용한다.

#### 3.4.4 점유 그리드: 로그-오즈 Bayesian Update

LiDAR 스캔마다 셀(5cm) 점유 확률을 베이지안으로 갱신하고 Ray Casting으로 자유 공간도 갱신한다(점유 +0.85, 자유 −0.40, 클리핑 [−5.0,+5.0]). 정적 SLAM 지도(위치 추정용)와 라이브 코스트맵(회피용)은 역할이 달라 둘 다 유지하되 전자의 비중을 낮춘다.

### 3.5 목표 4: AI 성능 강화 + 안전 구조

#### 3.5.1 AI 성능 강화: 단계적 최적화

같은 YOLO26 모델을 어떻게 최적화하느냐에 따른 추론 지연·GPU 부하 변화를 KPI로 정량화한다. 이 부분이 본 프로젝트의 핵심이다.

| 단계 | 적용 | 기대 효과 | 측정 KPI |
| --- | --- | --- | --- |
| 1. PyTorch FP32 | 베이스라인 | 기준 추론 시간 | 추론 지연(ms) |
| 2. TensorRT FP16 | .pt→.engine FP16 | 2~3배 가속 | 추론 지연, mAP 손실 |
| 3. TensorRT INT8 | INT8 캘리브레이션(300장) | 추가 1.5~2배 | 추론 지연, mAP 손실 |
| 4. 동적 해상도 | 정상 640/과부하 416/심각 320 | 40~60% 추가 가속 | GPU 부하, 추론 지연 |
| 5. 추론 주기 동적 | 없음 5fps/감지 15fps/근접 30fps | 평균 GPU 부하 감소 | 평균 GPU 사용률 |

INT8 캘리브레이션 데이터셋(주차 차량 영상 약 300장)은 4주차에 사전 수집해 양자화 정확도 손실을 1.5% 이내로 관리한다.

#### 3.5.2 안전: 운영모드 상태기계 + MRM

기존 4단계 Fail-safe를 운영모드 상태기계와 최소위험기동(MRM)으로 재구성한다. AI 부하·센서 상태에 따른 모드 단계는 다음과 같다.

| 레벨 | 상태 | 동작 | 전환 조건 |
| --- | --- | --- | --- |
| 1 | 정상(Heavy) | YOLO26s INT8 + 전체 스택 | 기본 |
| 2 | 과부하(Light) | YOLO26n INT8 유지 | GPU>85% AND 추론>150ms |
| 3 | AI 배제 | LiDAR DBSCAN 단독 | GPU>95% AND 추론>300ms |
| 4 | Emergency Stop | 정지 | LiDAR/통신 끊김 |

히스테리시스로 채터링을 막는다(GPU 복귀 60% 미만, CPU 복귀 55% 미만, GPU 온도 복귀 70°C 미만, 추론 복귀 90ms 미만, 쿨다운 3초). 명시적 행동 상태기계(LANE_FOLLOW → OBSTACLE_STOP → NUDGE/OVERTAKE → RESUME)를 두고, 자율 개입/해제(engage/disengage)를 명시하며 AUTO→STOP/ERROR 전이를 원인 코드와 함께 디스인게이지먼트 로그로 적재한다. STM32 워치독은 독립 안전 ECU로 유지하고, UI E-Stop은 소프트 정지 요청으로 구분한다.

센서별 처리는 다음과 같다. LiDAR `/scan` 500ms 미수신 시 감속 후 정지, 카메라 200ms 미수신 시 LiDAR 단독, 차내 CAN 0x010 500ms 미수신 또는 Bus-off 시 STM32 독립 정지, AMCL covariance 임계 초과 시 추종 중단·정지, 경로 탐색 3회 실패 시 마지막 안전 위치 정지, Pi5 UDP 500ms 미수신 시 차선 정보 무효화 후 LiDAR+GPS 주행. 각 이벤트는 Fail_Safe_Event(0x1FF)로 송출한다.

### 3.6 목표 5: CAN 통신 모니터링

Jetson의 AI 연산 결과·차량·시스템 상태를 외부 PC로 송출하고, PC UI 명령·목적지를 Jetson으로 받는 양방향 인터페이스다. 전체 신호 정의는 `valeo_project_can.dbc`를 정본으로 하며 SavvyCAN에서 로드해 디코딩·시각화한다.

#### 3.6.1 차외 CAN 프레임 (DBC 정본)

방향은 별도 표기가 없으면 Jetson→PC_UI다.

| ID | DBC 메시지 | 방향 | 주기 | 주요 신호(스케일/단위) |
| --- | --- | --- | --- | --- |
| 0x100 | Obstacle_Detection | →PC_UI | 20ms | Obstacle_Distance(0.01m), Obstacle_Angle(0.1°), Class_Id, Confidence(0.5%), Fail_Safe_Level(1~4), Alive |
| 0x101 | Vehicle_Status | →PC_UI | 20ms | Vehicle_Speed(0.01m/s), Steering_Angle(0.1°), Driving_State, Yolo_Mode, Control_Enable, Alive |
| 0x102 | Realtime_KPI | →PC_UI | 100ms | Inference_Latency(ms), Gpu_Usage(%), CPU_Usage(%), Gpu_Temperature(°C), Path_Error(mm), Alive |
| 0x103 | System_Resource | →PC_UI | 100ms | Ram_Usage, Swap_Usage, Session_Rate(Hz), Bus_Load, Can_Loss_Rate(0.1%), Pi5_Status, Camera_Status, Alive |
| 0x104 | Route_Status | →PC_UI | 100ms | Route_Progress(0.1m), Amcl_Error(0.01m), Mission_Success(0.1%), Route_State, Alive |
| 0x105 | Hardware_Info | →PC_UI | 1s | Jetson_Model, Orin_Memory_GB, Counter |
| 0x106 | Planning_Status | →PC_UI | 100ms | PathPlan_Last_ms, PathPlan_Success_Runs, PathPlan_Total_Runs, Planning_State, Alive |
| 0x107 | Perception_Validation | →PC_UI | 100ms | Perception_Detected_Runs, Perception_Total_Runs, False_Positive_Count, Trigger_Accuracy(0.5%), Alive |
| 0x108 | Map_Datum | →PC_UI | 1s | Origin_Latitude_1e7(int32,1e-7°), Origin_Longitude_1e7(int32,1e-7°) |
| 0x109 | Network_Status | →PC_UI | 100ms | Wifi_Ping_ms, Network_Loss_Rate(0.1%), Wifi_Rssi_dBm(int8), Network_Status, Alive |
| 0x10A | Localization_Status | →PC_UI | 100ms | Loc_Mode(INIT/AMCL/ODOM/GPS), Loc_Quality(%), Loc_Lane_Dev(mm), Loc_Counter |
| 0x10B | Behavior_State | →PC_UI | 100ms | Behavior_Mode(LANE_FOLLOW/OBSTACLE_STOP/NUDGE/OVERTAKE/RESUME), Behavior_Counter |
| 0x10C | Map_Info | →PC_UI | 1s | (mux Map_Mux) m0: Map_Origin_X/Y(0.01m), Map_Resolution(0.001m) / m1: Map_Width/Height(px) / 공통: Map_Version, Counter |
| 0x10D | Ego_Pose | →PC_UI | 100ms | Ego_X/Y(0.01m), Ego_Yaw(0.1°), Ego_Counter |
| 0x1FF | Fail_Safe_Event | →PC_UI | 이벤트 | Event_Code, Reason_Code, Timestamp_ms(int32), Fail_Safe_Level_Event(1~4), Event_Counter |
| 0x200 | UI_Command | PC_UI→ | 이벤트 | Command_Id, Precision_Mode(FP32/FP16/INT8), Model_Select(YOLO26s/n), Amcl_Command, Estop_Command, Destination_Select, Counter |
| 0x201 | UI_Goal_Pose | PC_UI→ | 이벤트 | Goal_X_m/Goal_Y_m(0.01m), Goal_Yaw_deg(0.1°), Goal_Valid_Flag, Counter |

`Localization_Status(0x10A)`·`Behavior_State(0x10B)`는 행동·위치 추정 상태 송출용으로, 펌웨어/노드 송신 전까지 UI 측에서는 디코더만 준비한다.

#### 3.6.2 PC 수신 및 KPI 로깅

SavvyCAN으로 프레임을 실시간 시각화·로그 저장하고, rosbag2로 ROS2 토픽을 동시 기록해 재생·분석한다. KPI 로거 노드가 CSV(날짜_실험번호_조건.csv)로 자동 저장하며, 매일 엔지니어링 노트에 핵심 수치를 기록한다.

#### 3.6.3 모니터링 UI 목적지 설정 및 지도 표시

모니터링 PC UI에서 지도 위 한 점을 클릭하면 가장 가까운 차로 중심선에 스냅하고, 픽셀↔월드 변환에 Map_Info(0x10C) 메타데이터(원점·해상도·픽셀·버전)를 사용해 UI 지도와 Jetson 운용 지도의 좌표계·버전을 일치시킨다. HD 차로 정합은 위·경도로 보관하다가 Map_Datum(0x108) 원점 수신 시 런타임에 ENU로 변환한다. 클릭 목적지는 UI_Goal_Pose(0x201)와 UI_Command(0x200) Set_Destination으로 확정하고, 사전 지점(Start/Parking/Charge_Station/Garage)은 Destination_Select 프리셋으로 지정한다. 차량 현재 위치는 Ego_Pose(0x10D)로 받아 지도에 실시간 표시한다. UI_Command(0x200)는 E-Stop·AMCL 재초기화·YOLO 모델 전환·추론 정밀도 전환을 포함해, KPI 실험(A/B/C) 중 PC에서 직접 모델·정밀도를 바꾸며 측정할 수 있게 한다.

### 3.7 ROS2 노드 구성

| 노드 | 발행 | 구독 |
| --- | --- | --- |
| rplidar_node | /scan | - |
| camera_node | /image_raw | - |
| pi5_lane_left/right | UDP → 192.168.10.1:9001/9002 | (Pi5 카메라) |
| lane_fusion | /lane/center_deviation | UDP 9001/9002 |
| stm32_bridge | /odom, /imu | /cmd_vel |
| gps_driver | /gps/fix | - |
| ekf_odom (robot_localization) | odom→base_link TF | /odom, /imu |
| ekf_map + navsat_transform | map→odom TF, /odometry/gps | /odom, /imu, /gps/fix |
| datum_node | 0x108 Map_Datum, set_datum | /gps/fix |
| slam_toolbox | /map, /tf | /scan, /odom, /imu |
| amcl | /amcl_pose, /tf | /scan, /map |
| yolo26_detector | /detections | /image_raw |
| lidar_clusterer | /lidar_obstacles | /scan |
| controller (Pure Pursuit) | /cmd_vel | 차로 중심선, /lidar_obstacles, /lane/center_deviation |
| behavior_fsm | /current_mode, /behavior_state | 전 토픽 감시 |
| edge_optimizer | /model_mode | /system_metrics |
| can_uplink | 차외 CAN 송신 | /detections, ego pose, /system_metrics 등 |
| ui_command | /goal_pose, 0x010 E-Stop | 차외 CAN 수신 |
| kpi_logger | - | 전 토픽 |

---

## 4. KPI 설계

### 4.1 KPI 설계 원칙

모든 KPI는 측정 가능성(rosbag2·CAN 로그·직접 측정), 재현 가능성(동일 조건 반복, 시작/종료 명시), 현실 가능성(6주·유아용 전동차·야외)을 동시에 만족한다. 필수 KPI는 특징 구간 정적 오차 50cm 이내, 추종 오차 30cm 이내, 주차 차량 감지율 8/10 이상, TensorRT INT8 가속 3배 이상(416 해상도 기준), CAN 송출 지연 10ms 이내다. 나머지는 확장 KPI로 둔다.

### 4.2 목표별 KPI

| 목표 | KPI | 목표값 | 측정 방법 |
| --- | --- | --- | --- |
| 위치 추정 | 목표 도달 성공률 | 7/10 이상 | 반경 50cm 이내 도달 |
| 위치 추정 | 정적 오차(특징 구간) | 50cm 이내 | 정지 상태 추정 vs 실제 |
| 위치 추정 | 차로 중앙 편차 | 관리 목표 | 비전 차선 기준 횡방향 안전 |
| 경로 | 경로 탐색 성공률 | 9/10 이상 | 기준 경로 생성 횟수 |
| 경로 | 추종 오차 | 30cm 이내 | 추정 위치 vs 기준 경로 평균 편차 |
| 객체 인식 | 주차 차량 감지율 | 8/10 이상 | /detections 로그 |
| 객체 인식 | 오탐율 | 1회/주행 미만 | 장애물 없는 구간 회피 발생 |
| 객체 인식 | 감지 응답 지연 | 200ms 이내 | 장애물 진입 ~ /detections |
| AI 최적화 | TensorRT INT8 가속 | 3배 이상(416) | 추론 ms 비교 |
| AI 최적화 | INT8 정확도 손실 | 1.5% mAP 이하 | FP32 vs INT8 mAP |
| 안전 | 전환 트리거 정확도 | 90% 이상 | 0x1FF 로그 |
| 안전 | 전환 지연 | 100ms 이내 | 전환 결정 ~ 경량 모델 첫 추론 |
| CAN | 송출 지연 | 10ms 이내 | 이벤트 ~ PC 수신 |
| CAN | 프레임 손실률 | 0.1% 미만 | 송출 vs 수신 카운트 |

정적 오차 50cm는 특징 구간 기준으로 보고하고, 공백 구간의 횡방향 안전은 비전 차선 추종으로 보장한다고 분리 보고한다. 차외 KPI 중 연속 스칼라는 Runs 페이지가, 성공률·횟수·이벤트형은 KPI Validation 페이지가, mAP 손실은 Accuracy Tracker가 분담한다. AI 최적화 실험 A/B/C와 그 KPI는 변경하지 않는다.

### 4.3 KPI 비교 실험 운영

| 실험 | 조건 | 측정 항목 | 주차 |
| --- | --- | --- | --- |
| A(양자화) | YOLO26s, FP32/FP16/INT8 | 추론 지연, mAP, 회피 성공률 | 5주차 |
| B(모델 사이즈) | YOLO26s INT8 vs YOLO26n INT8 | 추론 지연, mAP, 회피 성공률 | 5주차 |
| C(해상도 동적) | 고정 640 vs 동적 640/416/320 | GPU 부하, 추론 지연, 감지율 | 5주차 |

### 4.4 실험 프로토콜

기본 검증 구간·주간·동일 주차 배치(마스킹 테이프 고정)에서 각 KPI 10회 측정, 이상치 제거 후 평균을 쓴다. kpi_logger가 CSV로 자동 기록하고, CAN 로그는 SavvyCAN으로 PC에서 자동 수신·저장한다. 매 실험 시작 전 동일 출발점에서 위치 초기화를 수행하고, 주행 시작 전 GPS fix 확보를 절차에 포함한다. PD 보조배터리는 시작 전 잔량 4칸 확인, 실험 1회당 약 5분, 잔량 2칸 이하 시 충전 후 재시작한다.

---

## 5. 6주 개발 일정

### 5.0 일정 리스크 관리

6주 내 통합 완료를 위해 타임박싱과 범위 축소를 대원칙으로 한다. 특정 단계가 기간 내 목표에 도달하지 못하면 일정을 늘리지 않고 KPI를 낮추거나 차선책으로 전환한다. 완벽한 단일 모듈보다 전체 통합과 MVP 확보를 우선하고, 고도화는 기본 파이프라인 통합 이후 잔여 일정에 진행한다. 하드웨어 트러블슈팅이 2일 이상 길어지면 물리 해결을 보류하고 시뮬레이션·로그로 소프트웨어를 선행한다. 6주차 발표를 위해 최소 1주의 통합·디버깅 버퍼를 확보하고, 시간 부족 시 A/B/C 시나리오를 축소해 단일 주행 성공에 집중한다.

### 5.1 주차별 계획

1주차는 하드웨어 조립과 ROS2 환경 구축(JetPack·ROS2 설치, 전동차 개조, STM32 펌웨어, 센서 마운트·연결, 전 토픽 확인). 2주차는 SLAM 매핑과 위치 추정 기반 구축. 3주차는 경로·제어(중심선 추종, 직선 20m·50m 추종 오차 측정). 4주차는 YOLO26·BEV 차선·DBSCAN 회피와 INT8 캘리브레이션 데이터 수집. 5주차는 AI 최적화 실험(A/B/C)과 안전·CAN 통합. 6주차는 통합 검증, 10회 반복 실험·KPI 수집, 발표·시연 준비. 매주 금요일 PPT 리딩 발표, 매일 엔지니어링 노트(수치 기록 중심)를 작성한다.

---

## 6. 예상 이슈 및 대응

| 이슈 | 원인 | 대응 |
| --- | --- | --- |
| 특징 공백 구간 위치 발산 | 측면 외벽 비균질 | 융합 추정(ekf) + 비전 차선으로 횡방향 보장, 공백 구간 사전 표식, 안 되면 teach-and-repeat |
| 주차 차량 시야 차단 | 차체·카메라 낮음 | 주차 차량을 라이브 코스트맵 실시간 장애물로 처리, 차량이 적은 시간대 베이스맵 확보 |
| Jetson 발열/OOM | 인지+위치추정 동시 | SUPER 모드 + 활성 쿨링, GUI 비활성화, 모델 단일 로드 |
| BNO055 I2C 클럭 스트레칭 | 일부 마스터 통신 실패 | 하드웨어 I2C 클럭 스트레칭 허용, 100kbps, 풀업 확보 |
| CAN Bus-off | 모터 EMI, 종단 미설치 | 양 끝단 120Ω, 트위스트 페어, 공통 GND, bxCAN ABOM |
| BEV 차선 오탐 | 역광·젖은 노면 | 노출 고정, ROI 축소, 신뢰도 임계 상향 |
| 배터리 소모 | 반복 실험 | PD 보조배터리 운용, SLA는 모터 전용 |
| Pi5↔Jetson 지연 | UDP 드롭 | LAN 직결, 8byte 송신, 시퀀스·타임스탬프 손실 검출, 임계 초과 시 LiDAR 단독 |

---

## 7. 설계 변경 기록 (Decision Record)

본 절은 §1~§6에 이미 반영된 설계 전환의 배경과 근거, 적용 게이팅을 보존한다. 위 본문이 현재 상태이며, 본 절은 "왜 그렇게 바꿨는가"의 기록이다.

### 7.1 변경 배경: 현장 재평가

대구 수성 알파시티 시험 구간의 항공·근접 사진 분석에서 초기 전제와 네 가지 차이가 확인됐다. 측면 건물 배치가 비균질하여 외벽을 보는 구간과 특징 공백 구간이 공존한다(SLAM 외벽 앵커 전제 약화). 도로 양옆 밀집 주차로 회피·통과가 핵심 난제가 됐고, 주차 차량은 위치가 매번 다른 준정적 객체다. 차선 표시가 풍부·또렷해 비전 차선 추종을 데모의 등뼈로 삼을 근거가 됐다(단 젖은 노면 강건성 별도 검증). 본 구간이 자율주행특화지구라 국가 정밀도로지도(2023, 벡터)를 공문 없이 받을 수 있다(점군은 2D LiDAR라 미사용).

### 7.2 변경의 핵심 방향

위치 추정의 기둥을 "사전 SLAM 점유 격자 + 건물 외벽 앵커 AMCL"에서 "융합 위치 추정(휠·IMU·GPS) + 비전 차선 추종 + HD 차로 중심선 기준 경로"로 전환했다. 양산 AV 스택(차로 단위 의미 지도, 운영모드 상태기계, ODD 감시·MRM, 운영 콘솔)을 의도적으로 모방하되, Autoware 설치나 풀 lanelet2 라우팅은 하지 않는다(Orin Nano 8GB·2D LiDAR·일정상 비현실). HD 지도는 차로 중심선을 기준 경로·표시로만 쓰는 얕은 수준으로 활용한다.

### 7.3 적용 우선순위 및 게이팅

| 단계 | 범위 | 비고 |
| --- | --- | --- |
| Tier 1(필수·안전 코어) | 비전 차선 추종 + LiDAR 장애물 정지 + 융합 위치 추정 + 거친 목적지 | 보호 대상 MVP |
| Tier 2(저위험 AV) | 행동 FSM, 운영모드/MRM·디스인게이지먼트, AV 콘솔화, 버스 레벨 CAN 증빙 | 대부분 소프트웨어 |
| Tier 3(초반 성공에 게이트) | HD 차로 중심선 기준 경로 + Tactical HD 렌더 | 성공 시 진행, 실패 시 teach-and-repeat로 강등 |
| Tier 4(제외) | 풀 lanelet2, Autoware, 3D 점군 정합 | 하드웨어·일정상 비현실 |

결정 규칙은 "HD 지도는 신뢰성 있게 굴릴 수 있는 깊이까지만 통합하며, 데모나 AI 코어를 위협하면 한 단계 얕게 내린다"이다.

### 7.4 CAN·하드웨어 반영 사항

전환에 따라 CAN과 하드웨어 절이 다음과 같이 갱신됐고, 그 결과가 §2·§3.6의 본문·표에 반영돼 있다. IMU(0x021)는 ekf용 각속도(IMU_GyroZ_dps)를 포함하도록 재패킹됐고 캘리브레이션 상태는 런타임 프레임에서 제외됐다. 차외 CAN에 Map_Datum(0x108)이 추가됐고, 위치·행동 상태용 Localization_Status(0x10A)·Behavior_State(0x10B)가 정의됐으며, Map_Info는 0x10C(멀티플렉스), Ego_Pose는 0x10D로 배치됐다. GPS는 초기 위치 주입 전용에서 "첫 fix 원점 자동 설정 + 연속 거친 전역 입력"으로 확장됐다. 이 표·신호의 정본은 항상 `valeo_project_can.dbc`이며, 본 문서 표와 DBC가 다르면 DBC를 따른다.
