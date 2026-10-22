#include "fairino_hardware/fairino_hardware_interface.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <iterator>
#include <regex>
#include <stdexcept>

namespace {

// Keep the existing string service compatible, but only accept gripper commands.
// Malformed requests must never reach the SDK or escape a service callback.
struct GripperCommand {
    std::string name;
    std::vector<int> args;
};

GripperCommand parse_gripper_command(const std::string& text)
{
    static const std::regex syntax(R"(^\s*([A-Za-z][A-Za-z0-9_]*)\s*\(([^()]*)\)\s*$)");
    std::smatch match;
    if (text.size() > 160 || !std::regex_match(text, match, syntax)) {
        throw std::invalid_argument("expected GripperCommand(integer arguments)");
    }
    GripperCommand command{match[1].str(), {}};
    std::string arguments = match[2].str();
    if (arguments.find_first_not_of(" \t\r\n") != std::string::npos) {
        size_t begin = 0;
        do {
            const size_t end = arguments.find(',', begin);
            const std::string token = arguments.substr(begin, end - begin);
            const size_t first = token.find_first_not_of(" \t\r\n");
            const size_t last = token.find_last_not_of(" \t\r\n");
            int value = 0;
            if (first == std::string::npos) {
                throw std::invalid_argument("empty argument");
            }
            const auto parsed = std::from_chars(token.data() + first, token.data() + last + 1, value);
            if (parsed.ec != std::errc{} || parsed.ptr != token.data() + last + 1) {
                throw std::invalid_argument("arguments must be integers");
            }
            command.args.push_back(value);
            if (command.args.size() > 4) {
                throw std::invalid_argument("too many arguments");
            }
            if (end == std::string::npos) {
                break;
            }
            begin = end + 1;
        } while (true);
    }
    const auto& args = command.args;
    if (command.name == "ActGripper" || command.name == "MoveGripper") {
        const bool activate = command.name == "ActGripper";
        if (args.size() != 2 && (activate || args.size() != 4)) {
            throw std::invalid_argument(activate ? "ActGripper(index,0|1)" :
                "MoveGripper(index,position[,velocity,force])");
        }
        if (args[0] < 1 || args[0] > 16 || args[1] < 0 || args[1] > (activate ? 1 : 100)) {
            throw std::invalid_argument("index must be 1..16; activation 0..1; position 0..100");
        }
        if (args.size() == 4 && (args[2] < 0 || args[2] > 100 || args[3] < 0 || args[3] > 100)) {
            throw std::invalid_argument("velocity and force must be 0..100");
        }
    } else if (command.name == "GetGripperActivateStatus" ||
               command.name == "GetGripperMotionDone" ||
               command.name == "GetGripperCurPosition" || command.name == "GetGripperConfig") {
        if (!args.empty()) {
            throw std::invalid_argument("this getter takes no arguments");
        }
    } else {
        throw std::invalid_argument("unsupported command; this service controls only the gripper");
    }
    return command;
}

}  // namespace

namespace fairino_hardware{

FairinoHardwareInterface::~FairinoHardwareInterface()
{
    stop_gripper_service();
    close_robot();
}

hardware_interface::CallbackReturn FairinoHardwareInterface::on_init(const hardware_interface::HardwareInfo& sysinfo){
    if (hardware_interface::SystemInterface::on_init(sysinfo) != hardware_interface::CallbackReturn::SUCCESS) {
        return hardware_interface::CallbackReturn::ERROR;
    }
    info_ = sysinfo;//info_是父类中定义的变量

    // The SDK buffers and ServoJ command contain exactly six arm joints.
    if (info_.joints.size() != 6) {
        RCLCPP_ERROR(rclcpp::get_logger("FairinoHardwareInterface"),
                     "Expected only the six arm joints j1..j6; gripper control uses the ROS service.");
        return hardware_interface::CallbackReturn::ERROR;
    }
    for (size_t i = 0; i < 6; ++i) {
        if (info_.joints[i].name != "j" + std::to_string(i + 1)) {
            RCLCPP_ERROR(rclcpp::get_logger("FairinoHardwareInterface"),
                         "Arm joints must be listed in the order j1, j2, j3, j4, j5, j6.");
            return hardware_interface::CallbackReturn::ERROR;
        }
    }
    
    for (const hardware_interface::ComponentInfo& joint : info_.joints) {

        //指令部分
        if (joint.command_interfaces.size() != 1) {//开放servoJ
            RCLCPP_FATAL(rclcpp::get_logger("FairinoHardwareInterface"),
                        "Joint '%s' has %zu command interfaces found. 1 expected.", joint.name.c_str(),
                        joint.command_interfaces.size());
            return hardware_interface::CallbackReturn::ERROR;
        }

        if (joint.command_interfaces[0].name != hardware_interface::HW_IF_POSITION) {
            RCLCPP_FATAL(rclcpp::get_logger("FairinoHardwareInterface"),
                   "Joint '%s' have %s command interfaces found as first command interface. '%s' expected.",
                   joint.name.c_str(), joint.command_interfaces[0].name.c_str(), hardware_interface::HW_IF_POSITION);
            return hardware_interface::CallbackReturn::ERROR;
        }

        // if (joint.command_interfaces[1].name != hardware_interface::HW_IF_EFFORT){//预留，用于关节扭矩直接控制
        //     RCLCPP_FATAL(rclcpp::get_logger("FairinoHardwareInterface"),
        //            "Joint '%s' have %s command interfaces found as first command interface. '%s' expected.",
        //            joint.name.c_str(), joint.command_interfaces[1].name.c_str(), hardware_interface::HW_IF_EFFORT);
        //     return hardware_interface::CallbackReturn::ERROR;
        // }

        //关节状态部分
        if (joint.state_interfaces.size() != 1) {
            RCLCPP_FATAL(rclcpp::get_logger("FairinoHardwareInterface"), "Joint '%s' has %zu state interface. 3 expected.",
                        joint.name.c_str(), joint.state_interfaces.size());
            return hardware_interface::CallbackReturn::ERROR;
        }

        if (joint.state_interfaces[0].name != hardware_interface::HW_IF_POSITION) {
            RCLCPP_FATAL(rclcpp::get_logger("FairinoHardwareInterface"),
                        "Joint '%s' have %s state interface as first state interface. '%s' expected.", joint.name.c_str(),
                        joint.state_interfaces[0].name.c_str(), hardware_interface::HW_IF_POSITION);
            return hardware_interface::CallbackReturn::ERROR;
        }

        // if (joint.state_interfaces[1].name != hardware_interface::HW_IF_VELOCITY) {
        //     RCLCPP_FATAL(rclcpp::get_logger("FairinoHardwareInterface"),
        //                 "Joint '%s' have %s state interface as second state interface. '%s' expected.", joint.name.c_str(),
        //                 joint.state_interfaces[1].name.c_str(), hardware_interface::HW_IF_VELOCITY);
        //     return hardware_interface::CallbackReturn::ERROR;
        // }

        // if (joint.state_interfaces[2].name != hardware_interface::HW_IF_EFFORT) {
        //     RCLCPP_FATAL(rclcpp::get_logger("FairinoHardwareInterface"),
        //                 "Joint '%s' have %s state interface as third state interface. '%s' expected.", joint.name.c_str(),
        //                 joint.state_interfaces[2].name.c_str(), hardware_interface::HW_IF_EFFORT);
        //     return hardware_interface::CallbackReturn::ERROR;
        // }

    }
    return hardware_interface::CallbackReturn::SUCCESS;
}//end on_init



std::vector<hardware_interface::StateInterface> FairinoHardwareInterface::export_state_interfaces()
{
  std::vector<hardware_interface::StateInterface> state_interfaces;

  //导出关节相关的状态接口(位置，速度，扭矩)
  for (size_t i = 0; i < info_.joints.size(); ++i){
    state_interfaces.emplace_back(hardware_interface::StateInterface(
        info_.joints[i].name, hardware_interface::HW_IF_POSITION, &_jnt_position_state[i]));

    // state_interfaces.emplace_back(hardware_interface::StateInterface(
    //     info_.joints[i].name, hardware_interface::HW_IF_VELOCITY, &_jnt_velocity_state.at(i)));

    // state_interfaces.emplace_back(hardware_interface::StateInterface(
    //     info_.joints[i].name, hardware_interface::HW_IF_EFFORT, &_jnt_torque_state.at(i)));
  }

  //导出
  return state_interfaces;
}

std::vector<hardware_interface::CommandInterface> FairinoHardwareInterface::export_command_interfaces()
{
  std::vector<hardware_interface::CommandInterface> command_interfaces;
  for (size_t i = 0; i < info_.joints.size(); ++i) {
    command_interfaces.emplace_back(hardware_interface::CommandInterface(
        info_.joints[i].name, hardware_interface::HW_IF_POSITION, &_jnt_position_command[i]));

//     command_interfaces.emplace_back(hardware_interface::CommandInterface(//预留的扭矩控制接口
//         info_.joints[i].name, hardware_interface::HW_IF_EFFORT, &_jnt_torque_command.at(i)));
  }

  return command_interfaces;
}



hardware_interface::CallbackReturn FairinoHardwareInterface::on_activate(const rclcpp_lifecycle::State& previous_state)
{
    (void)previous_state;
    using namespace std::chrono_literals;
    stop_gripper_service();
    close_robot();
    std::unique_lock<std::mutex> lock(_sdk_mutex);
    RCLCPP_INFO(rclcpp::get_logger("FairinoHardwareInterface"), "Starting ...please wait...");
    //做变量的初始化工作
    _ptr_robot = std::make_unique<FRRobot>();//创建机器人实例
    for(int i=0;i<6;i++){//初始化变量
        _jnt_position_command[i] = 0;
        _jnt_velocity_command[i] = 0;
        _jnt_torque_command[i] = 0;
        _jnt_position_state[i] = 0;
        _jnt_velocity_state[i] = 0;
        _jnt_torque_state[i] = 0;
    }
    _control_mode = 0;//默认是位置控制,0-位置控制，1-扭矩控制 2-速度控制
    errno_t returncode = _ptr_robot->RPC(_controller_ip.c_str());//建立xmlrpc连接
    rclcpp::sleep_for(200ms);//等待一段时间让控制器的rpc连接建立完毕
    if(returncode != 0){
        RCLCPP_ERROR(rclcpp::get_logger("FairinoHardwareInterface"), "SDK connection failed: %d", returncode);
        lock.unlock();
        close_robot();
        return hardware_interface::CallbackReturn::ERROR;
    }else{
        _robot_connected = true;
        RCLCPP_INFO(rclcpp::get_logger("FairinoHardwareInterface"), "机械臂SDK连接成功！");
    }
    //做第一步的工作，读取当前状态数据
    JointPos jntpos{};
    returncode = _ptr_robot->GetActualJointPosDegree(0,&jntpos);
    /*
    获取反馈位置后同步到指令位置以维持当前状态，如果发现读取失败，那么就无法激活插件，
    因为错误的反馈位置会导致初始指令位置下发出现严重偏差导致事故
    */
    if(returncode == 0 && std::all_of(std::begin(jntpos.jPos), std::end(jntpos.jPos),
                                    [](double value) { return std::isfinite(value); })){
        for(int j=0;j<6;j++){
            _jnt_position_command[j] = jntpos.jPos[j]/180.0*M_PI;
            _jnt_position_state[j] = _jnt_position_command[j];
        }
        RCLCPP_INFO(rclcpp::get_logger("FairinoHardwareInterface"),"初始指令位置: %f,%f,%f,%f,%f,%f",_jnt_position_command[0],\
        _jnt_position_command[1],_jnt_position_command[2],_jnt_position_command[3],_jnt_position_command[4],_jnt_position_command[5]);    
        RCLCPP_INFO(rclcpp::get_logger("FairinoHardwareInterface"), "机械臂硬件启动成功!");
        _hardware_active = true;
        lock.unlock();
        try {
            start_gripper_service();
        } catch (const std::exception& error) {
            RCLCPP_ERROR(rclcpp::get_logger("FairinoHardwareInterface"),
                         "Cannot start gripper service: %s", error.what());
            stop_gripper_service();
            close_robot();
            return hardware_interface::CallbackReturn::ERROR;
        }
        return hardware_interface::CallbackReturn::SUCCESS;
    }else{
        RCLCPP_INFO(rclcpp::get_logger("FairinoHardwareInterface"), "读取初始关节角度错误，硬件无法启动！请检查通讯内容");
        lock.unlock();
        close_robot();
        return hardware_interface::CallbackReturn::ERROR;
    }
}



hardware_interface::CallbackReturn FairinoHardwareInterface::on_deactivate(const rclcpp_lifecycle::State& previous_state)
{
    (void)previous_state;
    RCLCPP_INFO(rclcpp::get_logger("FairinoHardwareInterface"), "Stopping ...please wait...");
    stop_gripper_service();
    close_robot();
    RCLCPP_INFO(rclcpp::get_logger("FairinoHardwareInterface"), "System successfully stopped!");
    return hardware_interface::CallbackReturn::SUCCESS;
}

hardware_interface::CallbackReturn FairinoHardwareInterface::on_shutdown(const rclcpp_lifecycle::State& previous_state)
{
    return on_deactivate(previous_state);
}

hardware_interface::CallbackReturn FairinoHardwareInterface::on_error(const rclcpp_lifecycle::State& previous_state)
{
    return on_deactivate(previous_state);
}



hardware_interface::return_type FairinoHardwareInterface::read(const rclcpp::Time& time,const rclcpp::Duration& period)
{//从RTDE反馈数据中获取所需的位置，速度和扭矩信息
    (void)time;
    (void)period;
    std::lock_guard<std::mutex> lock(_sdk_mutex);
    if (!_hardware_active || !_ptr_robot) {
        return hardware_interface::return_type::ERROR;
    }
    JointPos state_data{};
    error_t returncode = _ptr_robot->GetActualJointPosDegree(1,&state_data);
    if(returncode == 0 && std::all_of(std::begin(state_data.jPos), std::end(state_data.jPos),
                                    [](double value) { return std::isfinite(value); })){
        for(int i=0;i<6;i++){
            _jnt_position_state[i] = state_data.jPos[i]/180.0*M_PI;//注意单位转换，moveit统一用弧度
            //_jnt_torque_state[i] = state_data.jt_cur_tor[i];//注意单位转换
        }
    }else{
        _hardware_active = false;
        return hardware_interface::return_type::ERROR;
    }

  return hardware_interface::return_type::OK;

}

hardware_interface::return_type FairinoHardwareInterface::write(const rclcpp::Time& time,const rclcpp::Duration& period)
{
    (void)time;
    (void)period;
    std::lock_guard<std::mutex> lock(_sdk_mutex);
    if (!_hardware_active || !_ptr_robot) {
        return hardware_interface::return_type::ERROR;
    }
    if(_control_mode == 0){//位置控制模式
        if (std::any_of(std::begin(_jnt_position_command), std::end(_jnt_position_command),\
            [](double c) { return not std::isfinite(c); })) {
            _hardware_active = false;
            return hardware_interface::return_type::ERROR;
        }
        JointPos cmd;
        ExaxisPos extcmd{0,0,0,0};
        for(auto j=0;j<6;j++){
            cmd.jPos[j] = _jnt_position_command[j]/M_PI*180; //注意单位转换
        }
        int returncode = _ptr_robot->ServoJ(&cmd,&extcmd,0,0,0.008,0,0);
        if(returncode != 0){
            RCLCPP_INFO(rclcpp::get_logger("FairinoHardwareInterface"), "ServoJ指令下发错误,错误码:%d",returncode);
            _hardware_active = false;
            return hardware_interface::return_type::ERROR;
        }
    }else if(_control_mode == 1){//扭矩控制模式
        if (std::any_of(std::begin(_jnt_torque_command), std::end(_jnt_torque_command),\
            [](double c) { return not std::isfinite(c); })) {
            return hardware_interface::return_type::ERROR;
        }
        //_ptr_robot->write(_jnt_torque_command);//注意单位转换
    }else{
        RCLCPP_INFO(rclcpp::get_logger("FairinoHardwareInterface"), "指令发送错误:未识别当前所处控制模式");
        return hardware_interface::return_type::ERROR;
    }
 
    return hardware_interface::return_type::OK;
}


void FairinoHardwareInterface::start_gripper_service()
{
    // Ignore controller_manager's global __node remap and parameter files.
    // This node exposes services only; it must never construct another FRRobot or call RPC.
    _gripper_node = std::make_shared<rclcpp::Node>(
        "fairino_gripper_service", rclcpp::NodeOptions().use_global_arguments(false));

    rcl_interfaces::msg::ParameterDescriptor percentage;
    percentage.description = "Gripper SDK percentage request (0..100), not mm/s or N";
    rcl_interfaces::msg::IntegerRange range;
    range.from_value = 0;
    range.to_value = 100;
    range.step = 1;
    percentage.integer_range.push_back(range);
    _gripper_node->declare_parameter<int>("gripper_vel", 20, percentage);
    _gripper_node->declare_parameter<int>("gripper_force", 20, percentage);

    _gripper_service = _gripper_node->create_service<GripperService>(
        "fairino_remote_command_service",
        [this](const GripperService::Request::SharedPtr request,
               GripperService::Response::SharedPtr response) {
            if (!_gripper_running.load()) {
                response->cmd_res = "-10002,hardware inactive";
                return;
            }
            response->cmd_res = execute_gripper_command(
                request->cmd_str,
                static_cast<int>(_gripper_node->get_parameter("gripper_vel").as_int()),
                static_cast<int>(_gripper_node->get_parameter("gripper_force").as_int()));
        });
    _gripper_executor = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
    _gripper_executor->add_node(_gripper_node);
    _gripper_running.store(true);
    _gripper_thread = std::thread([this]() {
        try {
            while (_gripper_running.load() && rclcpp::ok()) {
                // A finite wait also handles cancellation before the first spin starts.
                _gripper_executor->spin_once(std::chrono::milliseconds(20));
            }
        } catch (const std::exception& error) {
            RCLCPP_ERROR(rclcpp::get_logger("FairinoHardwareInterface"),
                         "Gripper service executor stopped: %s", error.what());
        }
        _gripper_running.store(false);
    });
    RCLCPP_INFO(rclcpp::get_logger("FairinoHardwareInterface"),
        "Gripper service ready: /fairino_remote_command_service (shared arm SDK; velocity=20, force=20). "
        "Do not start ros2_cmd_server alongside this driver.");
}

void FairinoHardwareInterface::stop_gripper_service()
{
    // Stop accepting work before joining. Never hold the SDK mutex while joining:
    // an in-flight callback may be waiting for that mutex.
    _gripper_running.store(false);
    {
        std::lock_guard<std::mutex> lock(_sdk_mutex);
        _hardware_active = false;
    }
    if (_gripper_executor) {
        try {
            _gripper_executor->cancel();
        } catch (const std::exception& error) {
            // The ROS context may already be shutting down. The finite spin wait
            // and running flag still allow the executor thread to exit.
            RCLCPP_WARN(rclcpp::get_logger("FairinoHardwareInterface"),
                        "Gripper executor cancellation: %s", error.what());
        }
    }
    if (_gripper_thread.joinable()) {
        _gripper_thread.join();
    }
    _gripper_service.reset();
    _gripper_executor.reset();
    _gripper_node.reset();
}

void FairinoHardwareInterface::close_robot()
{
    std::lock_guard<std::mutex> lock(_sdk_mutex);
    _hardware_active = false;
    if (_ptr_robot) {
        if (_robot_connected) {
            try {
                _ptr_robot->StopMotion();
            } catch (const std::exception& error) {
                RCLCPP_ERROR(rclcpp::get_logger("FairinoHardwareInterface"),
                             "StopMotion during shutdown: %s", error.what());
            }
        }
        // Also close a partially established RPC connection before destroying its SDK object.
        try {
            _ptr_robot->CloseRPC();
        } catch (const std::exception& error) {
            RCLCPP_ERROR(rclcpp::get_logger("FairinoHardwareInterface"),
                         "CloseRPC during shutdown: %s", error.what());
        }
        _ptr_robot.reset();
    }
    _robot_connected = false;
}

std::string FairinoHardwareInterface::execute_gripper_command(
    const std::string& text, int velocity, int force)
{
    try {
        const auto command = parse_gripper_command(text);
        if (velocity < 0 || velocity > 100 || force < 0 || force > 100) {
            throw std::invalid_argument("velocity and force must be 0..100");
        }
        std::lock_guard<std::mutex> lock(_sdk_mutex);
        if (!_hardware_active || !_ptr_robot) {
            return "-10002,hardware inactive";
        }
        const auto& args = command.args;
        if (command.name == "ActGripper") {
            return std::to_string(_ptr_robot->ActGripper(args[0], static_cast<uint8_t>(args[1])));
        }
        if (command.name == "MoveGripper") {
            if (args.size() == 4) {
                velocity = args[2];
                force = args[3];
            }
            // Parallel gripper, asynchronous physical motion. Poll MotionDone separately.
            return std::to_string(_ptr_robot->MoveGripper(
                args[0], args[1], velocity, force, 30000, 1, 0, 0, 0, 0));
        }
        if (command.name == "GetGripperConfig") {
            int company = 0, device = 0, version = 0, bus = 0;
            const int result = _ptr_robot->GetGripperConfig(&company, &device, &version, &bus);
            if (result != 0) {
                return std::to_string(result);
            }
            return "0," + std::to_string(company) + "," + std::to_string(device) + "," +
                std::to_string(version) + "," + std::to_string(bus);
        }
        uint16_t fault = 0;
        uint16_t status = 0;
        uint8_t value = 0;
        int result = 0;
        if (command.name == "GetGripperActivateStatus") {
            result = _ptr_robot->GetGripperActivateStatus(&fault, &status);
        } else if (command.name == "GetGripperMotionDone") {
            result = _ptr_robot->GetGripperMotionDone(&fault, &value);
            status = value;
        } else {  // GetGripperCurPosition, validated above.
            result = _ptr_robot->GetGripperCurPosition(&fault, &value);
            status = value;
        }
        // Never return invented/stale output fields when a query fails.
        if (result != 0) {
            return std::to_string(result);
        }
        return "0," + std::to_string(fault) + "," + std::to_string(status);
    } catch (const std::invalid_argument& error) {
        return std::string("-10001,") + error.what();
    } catch (const std::exception& error) {
        RCLCPP_ERROR(rclcpp::get_logger("FairinoHardwareInterface"),
                     "Gripper SDK call failed: %s", error.what());
        return "-10003,SDK exception";
    }
}

}//end namesapce

#include "pluginlib/class_list_macros.hpp"
PLUGINLIB_EXPORT_CLASS(fairino_hardware::FairinoHardwareInterface, hardware_interface::SystemInterface)
