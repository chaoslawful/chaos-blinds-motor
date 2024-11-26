#include "config/pins.h"
#include "utility/misc.h"
#include "service/motor.h"
#include "service/logger.h"

MotorService *MotorService::m_instance = nullptr;

MotorService::MotorService() : m_encoder(ENCODER_A_PIN, ENCODER_B_PIN),
                               m_reverse_dir(false),
                               m_last_pos_pulse(0),
                               m_last_speed_pulse(0.0),
                               m_last_enc_read_ms(0),
                               m_pid(&m_pid_input, &m_pid_output, &m_pid_setpoint, PID_DEF_KP, PID_DEF_KI, PID_DEF_KD, DIRECT),
                               m_pid_input(0.0),
                               m_pid_output(0.0),
                               m_pid_setpoint(0.0),
                               m_pid_target_set_ms(0),
                               m_pid_last_report_ms(0),
                               m_motor_reached_stable(true),
                               m_good_sample_count(0),
                               m_stable_last_ms(0),
                               m_stable_last_pos(0)
{
    pinMode(ENCODER_PWR, OUTPUT);
    // 启动编码器电源
    digitalWrite(ENCODER_PWR, HIGH);

    pinMode(DRV_EEP_PIN, OUTPUT);
    pinMode(DRV_IN1_PIN, OUTPUT);
    pinMode(DRV_IN2_PIN, OUTPUT);

    analogWriteRange(PWM_RANGE);
    analogWriteFreq(PWM_FREQ);

    analogWrite(DRV_IN1_PIN, 0);
    analogWrite(DRV_IN2_PIN, 0);
    digitalWrite(DRV_EEP_PIN, LOW);

    float tau = 1 / (2 * PI * SPEED_CUTOFF_FREQ);                 // 低通滤波器时间常数 (s)
    speed_ema_alpha = 1.0 - exp(-UPDATE_DT_IN_MS / 1000.0 / tau); // 指数加权滤波系数 α=1-e^(-T/τ)
}

MotorService::~MotorService()
{
    // 关闭编码器电源
    digitalWrite(ENCODER_PWR, LOW);
    digitalWrite(DRV_EEP_PIN, LOW);
}

void MotorService::begin(long motor_init_pos, bool is_reverse)
{
    // 初始化编码器位置
    this->set_motor_pos(motor_init_pos);
    this->set_reverse(is_reverse);

    // 初始化位置 PID 控制器
    m_pid.SetMode(MANUAL);
    m_pid.SetSampleTime(PID_SAMPLE_TIME);
    m_pid.SetOutputLimits(-PWM_RANGE, PWM_RANGE);
}

void MotorService::update()
{
    this->_poll_measure_speed();
    this->_poll_run_pid();
    this->_poll_check_stable();
}

void MotorService::goto_pos(float motor_pos)
{
    // 目标位置同当前位置不同时更新 PID 控制器设定点，并开启 PID 自动控制
    if (!is_close_enough(motor_pos, this->get_pos_pulse()))
    {
        m_pid_target_set_ms = millis();
        m_pid_setpoint = motor_pos;

        m_motor_reached_stable = false;
        this->_enable_pid();
    }
}

void MotorService::forward(int pwm)
{
    m_motor_reached_stable = false;
    this->_disable_pid();
    this->motor_forward(pwm);
}

void MotorService::backward(int pwm)
{
    m_motor_reached_stable = false;
    this->_disable_pid();
    this->motor_backward(pwm);
}

void MotorService::stop()
{
    if (!m_motor_reached_stable)
    {
        m_motor_reached_stable = false;
        this->_disable_pid();
        this->motor_brake();
    }
}

void MotorService::motor_run(int pwm)
{
    if (pwm > 0)
    {
        this->motor_forward(pwm);
    }
    else if (pwm < 0)
    {
        this->motor_backward(-pwm);
    }
    else
    {
        // 停止电机的同时让驱动模块休眠
        this->motor_brake();
    }
}

void MotorService::driver_wakeup()
{
    digitalWrite(DRV_EEP_PIN, HIGH);
}

void MotorService::driver_sleep()
{
    digitalWrite(DRV_EEP_PIN, LOW);
}

void MotorService::motor_brake()
{
    digitalWrite(DRV_IN1_PIN, LOW);
    digitalWrite(DRV_IN2_PIN, LOW);

    this->driver_sleep();
}

void MotorService::motor_forward(int pwm)
{
    this->driver_wakeup();

    if (this->m_reverse_dir)
    {
        // 反向正转 (即反转 CCW)
        analogWrite(DRV_IN1_PIN, 0);
        analogWrite(DRV_IN2_PIN, pwm);
    }
    else
    {
        // 正常正转 CW
        analogWrite(DRV_IN1_PIN, pwm);
        analogWrite(DRV_IN2_PIN, 0);
    }
}

void MotorService::motor_backward(int pwm)
{
    this->driver_wakeup();

    if (this->m_reverse_dir)
    {
        // 反向反转 (即正转 CW)
        analogWrite(DRV_IN1_PIN, pwm);
        analogWrite(DRV_IN2_PIN, 0);
    }
    else
    {
        // 正常反转 CCW
        analogWrite(DRV_IN1_PIN, 0);
        analogWrite(DRV_IN2_PIN, pwm);
    }
}

void MotorService::_poll_run_pid()
{
    // 自动控制时根据 PID 运算结果驱动电动机
    if (m_pid.GetMode() == AUTOMATIC)
    {
        // 读取编码器位置作为位置 PID 输入
        m_pid_input = (double)this->get_pos_pulse();

        // 执行位置 PID 计算
        m_pid.Compute();

        // 获取 PID 输出结果作为 PWM 强度信号
        int pwm_signal = (int)m_pid_output;

        // 通过 PWM 强度信号设定电机转速
        this->motor_run(pwm_signal);

        // 定时输出 PID 控制结果
        unsigned long cur_ms = millis();
        if (cur_ms - m_pid_last_report_ms > PID_REPORT_INTERVAL)
        {
            m_pid_last_report_ms = cur_ms;

            // 以 Teleplot 格式输出 PWM 数据以绘图
            LoggerService::printf(">pwm: %d\n", pwm_signal);
        }
    }
}

void MotorService::_poll_check_stable()
{
    unsigned long cur_ms = millis();

    if (!m_motor_reached_stable && cur_ms - m_stable_last_ms > STABLE_SAMPLE_TIME)
    {
        m_stable_last_ms = cur_ms;

        // 读取编码器位置
        long enc_val = this->get_pos_pulse();
        if (is_close_enough((float)enc_val, (float)m_stable_last_pos))
        {
            m_good_sample_count++;
        }
        else
        {
            m_stable_last_pos = enc_val;
            m_good_sample_count = 0;
        }

        if (m_good_sample_count > STABLE_N_SAMPLE)
        {
            // 连续多个采样点满足误差要求，可以认为电机到达目标
            unsigned long stable_time = millis() - m_pid_target_set_ms;
            LoggerService::printf("cur_pos=%ld, pos_setpoint=%f, n_good_sample=%d\n", enc_val, m_pid_setpoint, m_good_sample_count);
            LoggerService::printf("Stable time %ld ms\n", stable_time);
            m_motor_reached_stable = true;

            // 重置达标样本点数
            m_good_sample_count = 0;

            // 停止电机并关闭 PID 控制
            this->_disable_pid();
            this->motor_brake();

            // 调用电机停止回调函数
            if (this->m_stop_callback)
            {
                this->m_stop_callback(this->get_pos_pulse());
            }
        }
    }
}

void MotorService::_poll_measure_speed()
{
    unsigned long cur_ms = millis();

    // 读取编码器位置
    long enc_val = this->get_pos_pulse();

    // 读取编码器位置差
    long enc_diff = enc_val - m_last_pos_pulse;

    // 当编码器位置变化超过阈值(适应高转速情况)或者采样间隔超过阈值(适应低转速情况)时更新电机速度
    if (abs(enc_diff) > SPEED_PULSE_THRESHOLD || cur_ms - m_last_enc_read_ms > SPEED_INTERVAL_THRESHOLD)
    {
        m_last_enc_read_ms = cur_ms;

        // 计算电机速度 (pulse/s)
        float speed = (double)enc_diff / (cur_ms - m_last_enc_read_ms + 1) * 1000;

        // 指数移动平均更新上次电机速度
        m_last_speed_pulse = (1 - speed_ema_alpha) * m_last_speed_pulse + speed_ema_alpha * speed;

        // 电机位置变化时以 Teleplot 格式输出位置和速度信息
        if (enc_val != m_last_pos_pulse)
        {
            LoggerService::printf(">pos: %ld\n", enc_val);
            LoggerService::printf(">speed: %.3f\n", m_last_speed_pulse);
        }

        // 更新上一次的编码器位置和时间
        m_last_pos_pulse = enc_val;
    }
}

void MotorService::_enable_pid()
{
    m_pid.SetMode(AUTOMATIC);
}

void MotorService::_disable_pid()
{
    m_pid.SetMode(MANUAL);
}
