# DH AG95 그리퍼 ROS 2 서비스 제어

## 1. 통신 구조 및 설명

FR5 플랜지에 연결된 **DH Robotics AG-160-95-W-S** 그리퍼를 FAIRINO 공식 `fairino_hardware` 패키지로 제어합니다.

```text
PC의 ROS 2 서비스 호출
    → ros2_cmd_server
    → Ethernet → FR5 제어기 (192.168.58.2)
    → 플랜지 RS485 통신 → DH 그리퍼
```

PC에서 명령을 보내면 FR5 제어기가 플랜지 통신을 통해 그리퍼에 전달합니다. 따라서 **이 제어 방식은 FAIRINO 제어기에 의존합니다.** DH 그리퍼 자체는 다른 장비에서도 사용할 수 있지만, 여기서 사용하는 `ActGripper()`, `MoveGripper()`는 FAIRINO API입니다. [공식 ROS 2 안내](https://fairino-doc-en.readthedocs.io/3.9.3/ROSGuide/ros2guide.html#quick-start)

명령은 하나의 ROS 2 서비스로 전달하며, 요청의 `cmd_str`에 명령 문자열을 넣고 응답의 `cmd_res`로 결과를 받습니다.

| 항목 | 값 |
|---|---|
| 서비스 이름 | `/fairino_remote_command_service` |
| 서비스 타입 | `fairino_msgs/srv/RemoteCmdInterface` |
| 활성화 명령 | `ActGripper(그리퍼번호, 활성화여부)` |
| 이동 명령 | `MoveGripper(그리퍼번호, 목표위치)` |

## 2. 사용법 (실습 - 그리퍼 활성화, 제어)

Ubuntu 22.04 / ROS 2 Humble에서 `~/fairino_ws`의 **패키지 빌드가 완료되어 있고**, PC–FR5 연결 및 WebApp의 **1번 그리퍼 등록**이 끝난 상태를 기준으로 합니다.

기존 MoveIt 실행을 종료하고 두 터미널을 사용합니다. 터미널에 `(base)`가 표시되면 먼저 `conda deactivate`를 실행합니다.

> 활성화와 이동 명령은 실제 그리퍼를 움직일 수 있습니다. 손과 물체를 그리퍼 사이에서 치운 뒤 실행합니다.

### 2-1. 명령 서버 실행 — 터미널 1

```bash
source /opt/ros/humble/setup.bash
source ~/fairino_ws/install/local_setup.bash

ros2 run fairino_hardware ros2_cmd_server --ros-args \
  -p gripper_vel:=20 \
  -p gripper_force:=20
```

로봇 연결 성공 로그를 확인하고 터미널을 켜 둡니다. `gripper_vel`과 `gripper_force`는 이후 이동 명령에 전달하는 속도·힘의 백분율 요청값입니다. `20`이 20 mm/s 또는 20 N을 뜻하지는 않습니다.

### 2-2. 명령 호출 준비 — 터미널 2

```bash
source /opt/ros/humble/setup.bash
source ~/fairino_ws/install/local_setup.bash
```

이후 명령은 모두 터미널 2에서 실행합니다. 응답의 첫 번째 결과 코드가 `0`이 아니면 오류를 확인한 뒤 다음 단계로 진행합니다.

### 2-3. 그리퍼 활성화

```bash
ros2 service call /fairino_remote_command_service \
  fairino_msgs/srv/RemoteCmdInterface \
  "{cmd_str: 'ActGripper(1,1)'}"
```

첫 번째 `1`은 그리퍼 번호, 두 번째 `1`은 활성화를 뜻합니다. `cmd_res='0'`이면 SDK 호출 성공입니다. 이미 활성화되어 있다면 상태 조회부터 진행합니다.

초기화 동작이 끝나면 활성화 상태를 조회합니다.

```bash
ros2 service call /fairino_remote_command_service \
  fairino_msgs/srv/RemoteCmdInterface \
  "{cmd_str: 'GetGripperActivateStatus()'}"
```

현재 장비에서 확인한 응답은 `cmd_res='0,0,1'`입니다. 순서대로 **조회 성공, 고장 없음, 활성화 비트마스크 1(bit 0 켜짐)**을 의미합니다. 마지막 값은 그리퍼 번호나 이동 완료 여부가 아닙니다.

### 2-4. 그리퍼 위치 제어

1번 그리퍼를 SDK 기준 목표 위치 50%로 이동시킵니다.

```bash
ros2 service call /fairino_remote_command_service \
  fairino_msgs/srv/RemoteCmdInterface \
  "{cmd_str: 'MoveGripper(1,50)'}"
```

- `1`: WebApp에 등록한 그리퍼 번호
- `50`: 목표 위치 백분율. 입력 범위는 `0~100`이며 mm나 관절 각도가 아닙니다.

속도와 힘은 터미널 1에서 설정한 값을 사용합니다. 위치값을 바꿀 때는 실제 열림·닫힘 방향과 위치 피드백을 확인하며 진행합니다.

응답 `cmd_res='0'`은 SDK 호출 성공을 의미하며, 실제 동작 완료는 다음 단계에서 확인합니다.

### 2-5. 동작 완료 및 현재 위치 확인

**동작 완료 조회:**

```bash
ros2 service call /fairino_remote_command_service \
  fairino_msgs/srv/RemoteCmdInterface \
  "{cmd_str: 'GetGripperMotionDone()'}"
```

| 응답 예시 | 의미 |
|---|---|
| `0,0,0` | 조회 성공, 고장 없음, 동작 미완료 |
| `0,0,1` | 조회 성공, 고장 없음, 동작 완료 |

동작 미완료라면 잠시 후 다시 조회합니다. 이 명령의 마지막 값은 **동작 완료 여부**이며, 활성화 조회의 마지막 값과 의미가 다릅니다.

**현재 위치 조회:**

```bash
ros2 service call /fairino_remote_command_service \
  fairino_msgs/srv/RemoteCmdInterface \
  "{cmd_str: 'GetGripperCurPosition()'}"
```

응답은 `결과코드,고장상태,현재위치(%)` 순서입니다. 예를 들어 `0,0,50`이면 조회 성공, 고장 없음, 위치 피드백 50%입니다. 위 완료·위치 응답은 해석을 위한 예시이며 실제 움직임과 함께 확인합니다. [공식 그리퍼 API 설명](https://fairino-doc-en.readthedocs.io/3.9.3/SDKManual/PythonRobotPeripherals.html)

실습을 마치면 그리퍼가 정지한 것을 확인하고 터미널 1에서 `Ctrl+C`로 서버를 종료합니다.
