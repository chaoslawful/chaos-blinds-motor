#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/preferences.h"
#include "esphome/components/cover/cover.h"

#include <Encoder.h>

namespace esphome {
namespace motor_pid {

static const char *const TAG = "motor_pid";

// 持久化状态魔数，用于识别有效标定数据（配合 ESPHome preferences 的 CRC）
static const uint32_t STATE_MAGIC = 0x43424D31;  // "CBM1"

struct MotorState {
  uint32_t magic;
  int32_t full_open;   // 完全打开标定位置（编码器脉冲）
  int32_t full_close;  // 完全关闭标定位置（编码器脉冲）
  int32_t current;     // 最近一次停机位置
  uint8_t reversed;    // 电机转向反转标志
};

/**
 * 带位置闭环 PID 的百叶窗电机 Cover 组件。
 *
 * 移植自 Arduino 版 MotorService + Application 的电机控制部分：
 *  - 位置 PID（与 PID_v1 同语义：微分作用于测量值，ki 乘 dt、kd 除 dt，积分限幅）
 *  - 到位判稳：每 stable_window_ms 采样一次，连续 stable_samples 个样本位置不变即到位
 *    （该机制同时兼任堵转保护）
 *  - 新增：max_run_time_ms 运动硬超时，到点无条件停机（原版缺失的安全兜底）
 *  - DRV8833 驱动：IN1/IN2 PWM + EEP 休眠，停机即休眠省电
 *  - 标定与当前位置经 ESPHome preferences 持久化（原子写 + CRC，替代原版裸 JSON 写）
 */
class MotorPIDCover : public cover::Cover, public Component {
 public:
  // ---- 配置注入（由 __init__.py 代码生成调用）----
  void set_in1_pin(GPIOPin *pin) { in1_ = pin; }
  void set_in2_pin(GPIOPin *pin) { in2_ = pin; }
  void set_sleep_pin(GPIOPin *pin) { sleep_ = pin; }
  void set_encoder_a_pin(GPIOPin *pin) { enc_a_ = pin; }
  void set_encoder_b_pin(GPIOPin *pin) { enc_b_ = pin; }
  void set_pid(double kp, double ki, double kd) { kp_ = kp; ki_ = ki; kd_ = kd; }
  void set_sample_time_ms(uint32_t ms) { sample_time_ms_ = ms; }
  void set_stable_params(int samples, uint32_t window_ms) { stable_samples_ = samples; stable_window_ms_ = window_ms; }
  void set_tolerance(float abs_tol, float rel_tol) { abs_tol_ = abs_tol; rel_tol_ = rel_tol; }
  void set_max_run_time_ms(uint32_t ms) { max_run_time_ms_ = ms; }
  void set_pwm_frequency(uint32_t freq) { pwm_freq_ = freq; }
  void set_reverse(bool rev) { reversed_ = rev; }
  void set_speed_cutoff_hz(float hz) { speed_cutoff_hz_ = hz; }

  // ---- 生命周期 ----
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }
  void dump_config() override;

  // ---- Cover 接口 ----
  cover::CoverTraits get_traits() override;

  // ---- 业务接口（供 YAML lambda / 红外状态机调用）----
  long get_pos_pulse();                       // 逻辑位置（考虑反转标志）
  float get_speed_pulse() const { return speed_ema_; }
  bool get_reverse() const { return reversed_; }
  int32_t get_full_open() const { return full_open_; }
  int32_t get_full_close() const { return full_close_; }

  void manual_open();      // 手动升起（CCW，全速），对应红外 LEFT
  void manual_close();     // 手动放下（CW，全速），对应红外 RIGHT
  void stop_motor();       // 立即停止并持久化位置，对应红外 OK / HA stop
  void goto_open();        // 运行至完全打开，对应红外 UP
  void goto_close();       // 运行至完全关闭，对应红外 DOWN
  void toggle_reverse();   // 切换电机转向并持久化，对应红外 0,#
  void set_reverse_persist(bool rev);  // 设置转向并持久化，对应 HA 开关
  void mark_open_here();   // 标定当前位置为全开点，对应红外 0,1
  void mark_close_here();  // 标定当前位置为全关点，对应红外 0,3
  void clear_calibration();// 清除标定并归零编码器，对应红外 0,2

 protected:
  void control(const cover::CoverCall &call) override;

  void goto_pos_(long target);
  void run_pid_(uint32_t now);
  void check_stable_(uint32_t now);
  void measure_speed_(uint32_t now);
  void finish_move_(bool reached);
  void motor_run_(int pwm);
  void motor_forward_(int pwm);
  void motor_backward_(int pwm);
  void motor_brake_();
  bool is_close_(float val, float dst) const;
  void publish_position_();
  void save_state_();
  bool load_state_();

  // ---- 硬件 ----
  GPIOPin *in1_{nullptr};
  GPIOPin *in2_{nullptr};
  GPIOPin *sleep_{nullptr};
  GPIOPin *enc_a_{nullptr};
  GPIOPin *enc_b_{nullptr};
  Encoder *encoder_{nullptr};

  // ---- 配置 ----
  double kp_{2.0}, ki_{0.2}, kd_{0.12};
  uint32_t sample_time_ms_{5};
  int stable_samples_{20};
  uint32_t stable_window_ms_{50};
  float abs_tol_{80.0f};
  float rel_tol_{0.01f};
  uint32_t max_run_time_ms_{60000};
  uint32_t pwm_freq_{20000};
  bool reversed_{false};
  float speed_cutoff_hz_{5.0f};

  // ---- 标定 ----
  int32_t full_open_{0};
  int32_t full_close_{0};

  // ---- 运行状态 ----
  bool moving_{false};        // 是否处于运动中（自动或手动）
  bool pid_active_{false};    // 是否处于 PID 自动控制
  double setpoint_{0};
  double integral_{0};
  long last_pid_input_{0};
  uint32_t last_pid_ms_{0};
  uint32_t move_start_ms_{0};

  // 判稳状态
  uint32_t stable_last_ms_{0};
  long stable_last_pos_{0};
  int good_samples_{0};

  // 测速状态
  long last_speed_pos_{0};
  uint32_t last_speed_ms_{0};
  float speed_ema_{0};
  float speed_alpha_{0.27f};

  // Cover 状态发布节流
  uint32_t last_publish_ms_{0};

  ESPPreferenceObject pref_;
};

}  // namespace motor_pid
}  // namespace esphome
