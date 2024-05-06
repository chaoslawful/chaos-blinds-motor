#pragma once

#include <Encoder.h>
#include <PID_v1.h>

#include <functional>

class MotorService
{
public:
    static constexpr int PWM_RANGE = 255;               // PWM 输出值范围
    static constexpr int PWM_FREQ = 128;                // PWM 输出频率
    static constexpr int PWM_DEADZONE = 30;             // PWM 输出死区
    static constexpr int PWM_MIN_SPEED = 40;            // PWM 最低速阈值
    static constexpr int PPR = 12;                      // 编码器每转一圈的脉冲数
    static constexpr int UPDATE_DT_IN_MS = 10;          // 电机速度更新时间间隔 (ms)
    static constexpr int SPEED_CUTOFF_FREQ = 5;         // 电机速度低通滤波截止频率 5 Hz
    static constexpr int SPEED_PULSE_THRESHOLD = 10;    // 编码器改变量超过 10 个脉冲就更新速度
    static constexpr int SPEED_INTERVAL_THRESHOLD = 50; // 编码器采样间隔超过 10ms 就更新速度
    static constexpr int STABLE_N_SAMPLE = 20;          // 判定进入稳态要求所需满足误差的连续样本数
    static constexpr int STABLE_SAMPLE_TIME = 50;       // 判定样本采样时间(ms)
    static constexpr int PID_SAMPLE_TIME = 5;           // PID 控制采样时间(ms), 临界振荡周期 ~0.3s
    static constexpr int PID_REPORT_INTERVAL = 10;      // PID 输出报告间隔时间(ms)
    static constexpr double PID_DEF_KP = 2.0;           // PID 控制参数 P
    static constexpr double PID_DEF_KI = 0.2;           // PID 控制参数 I
    static constexpr double PID_DEF_KD = 0.12;          // PID 控制参数 D

    using motor_stop_callback_t = std::function<void(long)>;

    static MotorService *get_instance()
    {
        if (m_instance == nullptr)
        {
            m_instance = new MotorService();
        }
        return m_instance;
    }

    ~MotorService();

    /** 启动电机控制和状态感知服务 */
    void begin(long motor_init_pos = 0, bool is_reverse = false);

    /** 更新电机位置和速度信息 */
    void update();

    /** 设置电机编码器初始位置值 */
    void set_motor_pos(long motor_pos) { m_encoder.write(motor_pos); }
    /** 电机运行至目标位置值 */
    void goto_pos(float motor_pos);
    /** 电机正转 */
    void forward(int pwm);
    /** 电机反转 */
    void backward(int pwm);
    /** 电机停止 */
    void stop();

    /** 获取 PID 控制参数 */
    void get_pid_tunings(double *kp, double *ki, double *kd)
    {
        *kp = m_pid.GetKp();
        *ki = m_pid.GetKi();
        *kd = m_pid.GetKd();
    }
    /** 设置 PID 控制参数 */
    void set_pid_tunings(double kp, double ki, double kd)
    {
        m_pid.SetTunings(kp, ki, kd);
    }

    /** 设置电机转向反向标志 */
    void set_reverse(bool is_reverse) { m_reverse_dir = is_reverse; }
    /** 获取电机转向反向标志 */
    bool get_reverse() const { return m_reverse_dir; }

    long get_pos_pulse()
    {
        long val = m_encoder.read();
        return m_reverse_dir ? -val : val;
    }
    float get_speed_pulse() const { return m_last_speed_pulse; }

    float get_angle_deg() { return this->get_pos_pulse() * 360.0 / PPR; }
    float get_speed_deg() const { return m_last_speed_pulse * 360.0 / PPR; }

    void set_stop_callback(motor_stop_callback_t callback) { m_stop_callback = callback; }

protected:
    MotorService();

    /** 唤醒电机驱动模块 */
    void driver_wakeup();
    /** 休眠电机驱动模块*/
    void driver_sleep();

    /** 电机停止 */
    void motor_brake();
    /** 电机正向旋转 (CW, PWM 值 >= 0)*/
    void motor_forward(int pwm);
    /** 电机反向旋转 (CCW, PWM 值 >= 0)*/
    void motor_backward(int pwm);
    /** 电机旋转
     * 正常情况下正 PWM 值表示正转 CW, 负 PWM 值表示反转 CCW, 0 表示停止, 设置反转标志时转向相反
     * */
    void motor_run(int pwm);

    /** 计算电机当前角速度 */
    void _poll_measure_speed();
    /** 检查 PID 控制时电机是否已进入稳态 */
    void _poll_check_stable();
    /** 运行 PID 电机控制 */
    void _poll_run_pid();
    /** 启用 PID 算法*/
    void _enable_pid();
    /** 停用 PID 算法 */
    void _disable_pid();

    static MotorService *m_instance;

    // 电机位置编码器
    Encoder m_encoder;
    bool m_reverse_dir;
    long m_last_pos_pulse;
    float m_last_speed_pulse;
    unsigned long m_last_enc_read_ms;
    float speed_ema_alpha;

    // PID 控制器
    PID m_pid;
    double m_pid_input, m_pid_output, m_pid_setpoint;
    unsigned long m_pid_target_set_ms;  // 最近一次设置目标位置的时间戳
    unsigned long m_pid_last_report_ms; // 最近一次 PID 输出报告的时间戳

    bool m_motor_reached_stable;    // 电机是否稳定
    int m_good_sample_count;        // 电机稳定采样计数
    unsigned long m_stable_last_ms; // 最近一次稳定采样的时间戳
    long m_stable_last_pos;         // 最近一次稳定采样的位置值

    motor_stop_callback_t m_stop_callback;
};
