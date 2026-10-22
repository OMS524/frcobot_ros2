// This executable defines every SDK symbol used by the driver itself.
// Do not link libfairino: no RPC, socket connection, or real robot motion is possible.
#include "fairino_hardware/fairino_hardware_interface.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <stdexcept>

namespace {
struct FakeSDK {
    std::atomic<int> calls{0};
    std::atomic<int> concurrent{0};
    std::atomic<int> overlaps{0};
    int constructions = 0, destructions = 0, connects = 0, closes = 0, stops = 0;
    int result = 0;
    bool delay = false, throw_on_move = false;
    std::array<int, 7> move{};
    std::array<int, 2> activation{};
    std::array<double, 6> joints{};
} sdk;

struct SDKCall {
    SDKCall() {
        ++sdk.calls;
        if (sdk.concurrent.fetch_add(1) != 0) { ++sdk.overlaps; }
        if (sdk.delay) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    }
    ~SDKCall() { --sdk.concurrent; }
};
}  // namespace

FRRobot::FRRobot() { ++sdk.constructions; }
FRRobot::~FRRobot() { ++sdk.destructions; }
errno_t FRRobot::RPC(const char*) { SDKCall call; ++sdk.connects; return sdk.result; }
errno_t FRRobot::CloseRPC() { SDKCall call; ++sdk.closes; return 0; }
errno_t FRRobot::StopMotion() { SDKCall call; ++sdk.stops; return 0; }
errno_t FRRobot::GetActualJointPosDegree(uint8_t, JointPos* joints) {
    SDKCall call;
    std::copy(sdk.joints.begin(), sdk.joints.end(), joints->jPos);
    return sdk.result;
}
errno_t FRRobot::ServoJ(JointPos*, ExaxisPos*, float, float, float, float, float, int) {
    SDKCall call;
    return sdk.result;
}
errno_t FRRobot::ActGripper(int index, uint8_t act) {
    SDKCall call;
    sdk.activation = {index, act};
    return sdk.result;
}
errno_t FRRobot::MoveGripper(int index, int pos, int vel, int force, int max_time,
                           uint8_t block, int type, double, int, int) {
    SDKCall call;
    if (sdk.throw_on_move) { throw std::runtime_error("simulated SDK failure"); }
    sdk.move = {index, pos, vel, force, max_time, block, type};
    return sdk.result;
}
errno_t FRRobot::GetGripperActivateStatus(uint16_t* fault, uint16_t* status) {
    SDKCall call;
    if (sdk.result == 0) { *fault = 0; *status = 1; }
    return sdk.result;
}
errno_t FRRobot::GetGripperMotionDone(uint16_t* fault, uint8_t* status) {
    SDKCall call;
    if (sdk.result == 0) { *fault = 0; *status = 1; }
    return sdk.result;
}
errno_t FRRobot::GetGripperCurPosition(uint16_t* fault, uint8_t* position) {
    SDKCall call;
    if (sdk.result == 0) { *fault = 0; *position = 50; }
    return sdk.result;
}
errno_t FRRobot::GetGripperConfig(int* company, int* device, int* version, int* bus) {
    SDKCall call;
    if (sdk.result == 0) { *company = 4; *device = 0; *version = 0; *bus = 0; }
    return sdk.result;
}

namespace fairino_hardware {
class GripperServiceTestAccess {
public:
    static void activate(FairinoHardwareInterface& driver) {
        driver._ptr_robot = std::make_unique<FRRobot>();
        driver._hardware_active = true;
        driver._robot_connected = true;
        driver._control_mode = 0;
        std::fill(std::begin(driver._jnt_position_command), std::end(driver._jnt_position_command), 0.0);
    }
    static std::string command(FairinoHardwareInterface& driver, const std::string& text,
                               int velocity = 20, int force = 20) {
        return driver.execute_gripper_command(text, velocity, force);
    }
    static void invalid_last_joint(FairinoHardwareInterface& driver) {
        driver._jnt_position_command[5] = std::numeric_limits<double>::quiet_NaN();
    }
};
}  // namespace fairino_hardware

using fairino_hardware::FairinoHardwareInterface;
using Access = fairino_hardware::GripperServiceTestAccess;

class GripperServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        sdk.calls = 0; sdk.concurrent = 0; sdk.overlaps = 0;
        sdk.constructions = sdk.destructions = sdk.connects = sdk.closes = sdk.stops = 0;
        sdk.result = 0; sdk.delay = false; sdk.throw_on_move = false;
        sdk.joints.fill(0.0);
        Access::activate(driver);
    }
    FairinoHardwareInterface driver;
};

TEST_F(GripperServiceTest, UsesExistingSDKAndForcesNonblockingMotion) {
    EXPECT_EQ(Access::command(driver, "ActGripper(1,1)"), "0");
    EXPECT_EQ(sdk.activation, (std::array<int, 2>{1, 1}));
    EXPECT_EQ(Access::command(driver, "MoveGripper(1,50)"), "0");
    EXPECT_EQ(sdk.move, (std::array<int, 7>{1, 50, 20, 20, 30000, 1, 0}));
    EXPECT_EQ(Access::command(driver, " MoveGripper ( 1, 25, 10, 15 ) "), "0");
    EXPECT_EQ(sdk.move, (std::array<int, 7>{1, 25, 10, 15, 30000, 1, 0}));
    EXPECT_EQ(Access::command(driver, "MoveGripper(1,0)", 7, 8), "0");
    EXPECT_EQ(sdk.move[2], 7); EXPECT_EQ(sdk.move[3], 8);
    EXPECT_EQ(sdk.constructions, 1);
    EXPECT_EQ(sdk.connects, 0);  // Service code never opens a new connection.
}

TEST_F(GripperServiceTest, RejectsInvalidAndArmCommandsWithoutCallingSDK) {
    const std::vector<std::string> commands = {
        "", "MoveGripper", "MoveGripper()", "MoveGripper(1)", "MoveGripper(1,50,20)",
        "MoveGripper(1,50,20,20,1)", "MoveGripper(0,50)", "MoveGripper(17,50)",
        "MoveGripper(1,-1)", "MoveGripper(1,101)", "MoveGripper(1,nan)",
        "MoveGripper(1,50.5)", "MoveGripper(1,50x)", "MoveGripper(1,,50)",
        "MoveGripper(1,50,)", "MoveGripper(1,50,-1,20)", "MoveGripper(1,50,20,101)",
        "MoveGripper(999999999999999999999999,50)", "MoveGripper(1,50);StopMotion()",
        "ActGripper(1,2)", "ActGripper(1,1,1,1)", "GetGripperMotionDone(1)",
        "GetGripperCurPosition(,)", "RobotEnable(1)", "StopMotion()", "ServoJ(1,2)",
        std::string(161, 'x')
    };
    for (const auto& text : commands) {
        EXPECT_EQ(Access::command(driver, text).find("-10001,"), 0u) << text;
    }
    EXPECT_EQ(Access::command(driver, "MoveGripper(1,50)", 101, 20).find("-10001,"), 0u);
    EXPECT_EQ(sdk.calls.load(), 0);
}

TEST_F(GripperServiceTest, ReturnsQueryFieldsOnlyOnSuccess) {
    EXPECT_EQ(Access::command(driver, "GetGripperActivateStatus()"), "0,0,1");
    EXPECT_EQ(Access::command(driver, "GetGripperMotionDone()"), "0,0,1");
    EXPECT_EQ(Access::command(driver, "GetGripperCurPosition()"), "0,0,50");
    EXPECT_EQ(Access::command(driver, "GetGripperConfig()"), "0,4,0,0,0");
    sdk.result = -3;
    for (const auto* text : {"GetGripperActivateStatus()", "GetGripperMotionDone()",
                            "GetGripperCurPosition()", "GetGripperConfig()", "ActGripper(1,1)"}) {
        EXPECT_EQ(Access::command(driver, text), "-3");
    }
    sdk.throw_on_move = true;
    EXPECT_EQ(Access::command(driver, "MoveGripper(1,50)"), "-10003,SDK exception");
}

TEST_F(GripperServiceTest, DeactivationClosesOnceAndRejectsFurtherCommands) {
    const rclcpp_lifecycle::State state;
    EXPECT_EQ(driver.on_deactivate(state), hardware_interface::CallbackReturn::SUCCESS);
    EXPECT_EQ(Access::command(driver, "ActGripper(1,1)"), "-10002,hardware inactive");
    EXPECT_EQ(driver.on_shutdown(state), hardware_interface::CallbackReturn::SUCCESS);
    EXPECT_EQ(sdk.stops, 1); EXPECT_EQ(sdk.closes, 1); EXPECT_EQ(sdk.destructions, 1);
}

TEST_F(GripperServiceTest, SerializesGripperAndArmSDKCalls) {
    sdk.delay = true;
    std::thread arm([this]() {
        for (int i = 0; i < 30; ++i) {
            EXPECT_EQ(driver.read(rclcpp::Time(0), rclcpp::Duration(0, 10000000)),
                      hardware_interface::return_type::OK);
            EXPECT_EQ(driver.write(rclcpp::Time(0), rclcpp::Duration(0, 10000000)),
                      hardware_interface::return_type::OK);
        }
    });
    std::thread gripper([this]() {
        for (int i = 0; i < 30; ++i) {
            EXPECT_EQ(Access::command(driver, "MoveGripper(1,50)"), "0");
        }
    });
    arm.join(); gripper.join();
    EXPECT_EQ(sdk.overlaps.load(), 0);
    EXPECT_EQ(sdk.calls.load(), 90);
}

TEST_F(GripperServiceTest, InvalidSixthArmCommandDoesNotReachServoJ) {
    Access::invalid_last_joint(driver);
    EXPECT_EQ(driver.write(rclcpp::Time(0), rclcpp::Duration(0, 10000000)),
              hardware_interface::return_type::ERROR);
    EXPECT_EQ(sdk.calls.load(), 0);
    EXPECT_EQ(Access::command(driver, "MoveGripper(1,50)"), "-10002,hardware inactive");
}

TEST_F(GripperServiceTest, ArmReadFailureDisablesGripperCommands) {
    sdk.result = -3;
    EXPECT_EQ(driver.read(rclcpp::Time(0), rclcpp::Duration(0, 10000000)),
              hardware_interface::return_type::ERROR);
    EXPECT_EQ(Access::command(driver, "MoveGripper(1,50)"), "-10002,hardware inactive");
    EXPECT_EQ(sdk.calls.load(), 1);
}

TEST_F(GripperServiceTest, ServoFailureDisablesGripperCommands) {
    sdk.result = -3;
    EXPECT_EQ(driver.write(rclcpp::Time(0), rclcpp::Duration(0, 10000000)),
              hardware_interface::return_type::ERROR);
    EXPECT_EQ(Access::command(driver, "MoveGripper(1,50)"), "-10002,hardware inactive");
    EXPECT_EQ(sdk.calls.load(), 1);
}

TEST(ArmConfigurationTest, RejectsGripperJointInArmHardware) {
    FairinoHardwareInterface driver;
    hardware_interface::HardwareInfo info;
    info.joints.resize(7);
    EXPECT_EQ(driver.on_init(info), hardware_interface::CallbackReturn::ERROR);
}
