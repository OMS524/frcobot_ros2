# frcobot_ros2

FAIRINO FR5의 MoveIt 2 환경 구성과 플랜지에 연결된 DH Robotics AG-160-95-W-S 그리퍼 제어를 위한 교육용 저장소입니다. Ubuntu 22.04 / ROS 2 Humble / FR5 V6.0 환경을 기준으로 정리했습니다.

[FAIRINO 공식 ROS 2 패키지](https://github.com/FAIR-INNOVATION/frcobot_ros2)의 `V3.0.0_RobotV3.9.3` 버전을 바탕으로, 그리퍼 서비스 연동 기능과 한국어 안내 문서를 추가했습니다. 원본 드라이버, 로봇 모델 및 MoveIt 설정의 출처는 FAIRINO입니다.

## 사용 문서

- [FR5 MoveIt 2 및 DH 그리퍼 통합 안내 — Markdown](docs/DOCUMENT_MOVEIT2_AND_GRIPPER.md)
- [FR5 MoveIt 2 및 DH 그리퍼 통합 안내 — HTML](docs/DOCUMENT_MOVEIT2_AND_GRIPPER.html)

통합 문서에는 PC–FR5 네트워크 설정, 패키지 빌드, MoveIt 실행, 그리퍼 활성화·이동·상태 조회 절차가 포함되어 있습니다. **HTML에는 이미지와 실행 영상이 포함되어 있으므로 HTML 파일 하나만 내려받아 브라우저에서 열면 됩니다.** Markdown으로 배포할 때는 `images/`, `videos/`의 상대 위치가 유지되도록 `docs` 폴더 전체를 함께 전달합니다.

## 원본 코드 및 출처

| 구분 | 출처 | 사용 내용 |
|---|---|---|
| 원본 ROS 2 패키지 | [FAIR-INNOVATION/frcobot_ros2](https://github.com/FAIR-INNOVATION/frcobot_ros2) | `fairino_hardware`, `fairino_msgs`, 로봇 모델 및 MoveIt 설정 패키지 |
| 기반 버전 | [V3.0.0_RobotV3.9.3](https://github.com/FAIR-INNOVATION/frcobot_ros2/tree/V3.0.0_RobotV3.9.3) | 이 저장소를 구성할 때 사용한 공식 버전 |
| FAIRINO C++ SDK | [fairino-cpp-sdk / v2.3.3-robot3.9.3](https://github.com/FAIR-INNOVATION/fairino-cpp-sdk/tree/v2.3.3-robot3.9.3) | `fairino_hardware/libfairino`에 포함된 SDK 2.3.3의 출처 |

## 이 저장소에서 수정·추가한 내용

- **FR5 실제 하드웨어 설정:** `fairino5_v6_moveit2_config`에서 실제 FR5 드라이버를 사용하도록 설정했습니다.
- **그리퍼 서비스 연동:** `FairinoHardwareInterface`가 기존 로봇 연결을 공유해 `/fairino_remote_command_service`를 제공하도록 수정했습니다. 터미널 또는 Python ROS 2 클라이언트에서 그리퍼 활성화·이동·상태 조회를 요청할 수 있습니다.
- **오프라인 테스트:** 실제 로봇에 접속하지 않는 가짜 SDK로 명령 입력 검증, 오류 처리, SDK 호출 동기화를 확인하는 테스트를 추가했습니다.
- **한국어 문서:** 네트워크 설정 캡처, 실행 영상, MoveIt과 그리퍼의 사용 절차 및 HTML 배포본을 정리했습니다.

MoveIt과 함께 사용할 때는 드라이버가 제공하는 그리퍼 서비스를 사용합니다. MoveIt 없이 그리퍼만 사용할 때는 기존 `ros2_cmd_server`를 실행합니다. **실제 FR5 드라이버와 별도 명령 서버는 동시에 실행하지 않습니다.**

## 참고 문서

| 자료 | 참고 내용 |
|---|---|
| [FAIRINO ROS 2 안내 — v3.9.3](https://fairino-doc-en.readthedocs.io/3.9.3/ROSGuide/ros2guide.html) | 공식 ROS 2 패키지와 명령 서비스 사용법 |
| [FAIRINO MoveIt 2 시작 안내](https://support.fairino.us/portal/en/kb/articles/getting-started-with-the-fairino-moveit2-plugin) | FR5 MoveIt 2 환경 설치 및 실행 |
| [FAIRINO 제어함 연결 안내](https://manual.fairino.support/latest/CobotsManual/install_power_robot.html#connect-the-control-box) | PC와 로봇의 네트워크 연결 |
| [DH Robotics AG 그리퍼 연결 안내](https://support.fairino.us/portal/en/kb/articles/setting-up-a-dh-robotics-ag-gripper) | 플랜지 연결과 WebApp의 DH 그리퍼 등록·활성화 |
| [FAIRINO 주변장치 API — Python SDK v3.9.3](https://fairino-doc-en.readthedocs.io/3.9.3/SDKManual/PythonRobotPeripherals.html) | 그리퍼 활성화·이동·상태 조회 API 참고 |
