#include "motor_pid.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <cmath>

namespace esphome {
namespace motor_pid {

static const int PWM_RANGE = 255;  // 与原版一致：analogWriteRange(255)

// ------------------------------------------------------------------
// 生命周期
// ------------------------------------------------------------------

void MotorPIDCover::setup() {
  // DRV8833 引脚初始化：默认停机 + 驱动模块休眠（直接操作原始 GPIO，与原版一致）
  pinMode(in1_, OUTPUT);
  pinMode(in2_, OUTPUT);
  pinMode(sleep_, OUTPUT);

  analogWriteRange(PWM_RANGE);
  analogWriteFreq(pwm_freq_);
  analogWrite(in1_, 0);
  analogWrite(in2_, 0);
  digitalWrite(sleep_, LOW);

  // 编码器（PJRC，中断驱动）
  encoder_ = new Encoder(enc_a_, enc_b_);

  // 速度 EMA 滤波系数：α = 1 - e^(-T/τ)，τ = 1/(2π·fc)，T = 10ms（与原版一致）
  float tau = 1.0f / (2.0f * PI * speed_cutoff_hz_);
  speed_alpha_ = 1.0f - expf(-0.010f / tau);

  // 恢复持久化状态（标定点 + 停机位置 + 转向）
  MotorState st;
  int32_t restore_pos = 0;
  if (load_state_(st)) {
    full_open_ = st.full_open;
    full_close_ = st.full_close;
    reversed_ = st.reversed != 0;
    restore_pos = st.current;
    ESP_LOGI(TAG, "Restored: open=%d close=%d pos=%d reversed=%d", full_open_, full_close_, restore_pos,
             reversed_);
  } else {
    ESP_LOGW(TAG, "No valid calibration state found, using defaults (0/0/0)");
  }
  encoder_->write(restore_pos);

  publish_position_();
}

void MotorPIDCover::dump_config() {
  ESP_LOGCONFIG(TAG, "Motor PID Cover:");
  ESP_LOGCONFIG(TAG, "  PID: kp=%.3f ki=%.3f kd=%.3f sample=%ums", kp_, ki_, kd_, sample_time_ms_);
  ESP_LOGCONFIG(TAG, "  Stable: %d samples x %ums, tol abs=%.1f rel=%.3f", stable_samples_, stable_window_ms_,
                abs_tol_, rel_tol_);
  ESP_LOGCONFIG(TAG, "  Max run time: %ums, PWM freq: %uHz", max_run_time_ms_, pwm_freq_);
  ESP_LOGCONFIG(TAG, "  Calibration: open=%d close=%d reversed=%d", full_open_, full_close_, reversed_);
}

void MotorPIDCover::loop() {
  uint32_t now = millis();
  measure_speed_(now);

  if (pid_active_ && now - last_pid_ms_ >= sample_time_ms_) {
    last_pid_ms_ = now;
    run_pid_(now);
  }

  check_stable_(now);

  // 运动硬超时（新增安全兜底）：无论何种原因，超时无条件停机
  if (moving_ && now - move_start_ms_ > max_run_time_ms_) {
    ESP_LOGW(TAG, "SAFETY: max run time (%ums) exceeded, forcing stop", max_run_time_ms_);
    finish_move_(false);
  }

  // 运动中按 500ms 节流发布位置百分比
  if (moving_ && now - last_publish_ms_ >= 500) {
    last_publish_ms_ = now;
    publish_position_();
  }
}

// ------------------------------------------------------------------
// Cover 接口
// ------------------------------------------------------------------

cover::CoverTraits MotorPIDCover::get_traits() {
  auto traits = cover::CoverTraits();
  traits.set_supports_stop(true);
  traits.set_supports_position(true);
  return traits;
}

void MotorPIDCover::control(const cover::CoverCall &call) {
  if (call.get_stop()) {
    ESP_LOGI(TAG, "HA command: stop");
    stop_motor();
    return;
  }
  if (call.get_position().has_value()) {
    float pos = *call.get_position();
    // HA Cover 语义：1.0 = 全开，0.0 = 全关
    long target = full_close_ + lroundf((float) (full_open_ - full_close_) * pos);
    ESP_LOGI(TAG, "HA command: position %.2f -> %ld pulses", pos, target);
    goto_pos_(target);
  }
}

void MotorPIDCover::publish_position_() {
  float denom = (float) (full_open_ - full_close_);
  if (denom != 0.0f) {
    float p = (float) (get_pos_pulse() - full_close_) / denom;
    this->position = clamp(p, 0.0f, 1.0f);
  }
  if (!moving_) {
    this->current_operation = cover::COVER_OPERATION_IDLE;
  }
  this->publish_state();
}

// ------------------------------------------------------------------
// 业务接口
// ------------------------------------------------------------------

long MotorPIDCover::get_pos_pulse() {
  long val = encoder_->read();
  return reversed_ ? -val : val;
}

void MotorPIDCover::goto_open() {
  ESP_LOGI(TAG, "Command: auto open -> %d", full_open_);
  goto_pos_(full_open_);
}

void MotorPIDCover::goto_close() {
  ESP_LOGI(TAG, "Command: auto close -> %d", full_close_);
  goto_pos_(full_close_);
}

void MotorPIDCover::manual_open() {
  ESP_LOGI(TAG, "Command: manual open (CCW)");
  pid_active_ = false;
  integral_ = 0.0;
  moving_ = true;
  move_start_ms_ = millis();
  good_samples_ = 0;
  stable_last_pos_ = get_pos_pulse();
  stable_last_ms_ = millis();
  this->current_operation = cover::COVER_OPERATION_OPENING;
  this->publish_state();
  motor_backward_(PWM_RANGE);
}

void MotorPIDCover::manual_close() {
  ESP_LOGI(TAG, "Command: manual close (CW)");
  pid_active_ = false;
  integral_ = 0.0;
  moving_ = true;
  move_start_ms_ = millis();
  good_samples_ = 0;
  stable_last_pos_ = get_pos_pulse();
  stable_last_ms_ = millis();
  this->current_operation = cover::COVER_OPERATION_CLOSING;
  this->publish_state();
  motor_forward_(PWM_RANGE);
}

void MotorPIDCover::stop_motor() {
  ESP_LOGI(TAG, "Command: stop");
  finish_move_(true);
}

void MotorPIDCover::toggle_reverse() {
  reversed_ = !reversed_;
  ESP_LOGI(TAG, "Motor direction: %s", reversed_ ? "reversed" : "normal");
  save_state_();
}

void MotorPIDCover::set_reverse_persist(bool rev) {
  if (reversed_ == rev)
    return;
  reversed_ = rev;
  ESP_LOGI(TAG, "Motor direction: %s", reversed_ ? "reversed" : "normal");
  save_state_();
}

void MotorPIDCover::mark_open_here() {
  full_open_ = get_pos_pulse();
  ESP_LOGI(TAG, "Marked full open position: %d", full_open_);
  save_state_();
  publish_position_();
}

void MotorPIDCover::mark_close_here() {
  full_close_ = get_pos_pulse();
  ESP_LOGI(TAG, "Marked full close position: %d", full_close_);
  save_state_();
  publish_position_();
}

void MotorPIDCover::clear_calibration() {
  ESP_LOGI(TAG, "Clear calibration");
  stop_motor();
  full_open_ = 0;
  full_close_ = 0;
  encoder_->write(0);
  save_state_();
  publish_position_();
}

// ------------------------------------------------------------------
// 控制环（移植自 MotorService）
// ------------------------------------------------------------------

void MotorPIDCover::goto_pos_(long target) {
  if (is_close_((float) target, (float) get_pos_pulse())) {
    ESP_LOGI(TAG, "Already at target %ld, no movement", target);
    return;
  }
  setpoint_ = (double) target;
  integral_ = 0.0;  // 等价于 PID_v1 Initialize()（输出从零起步时 ITerm=0）
  last_pid_input_ = get_pos_pulse();
  pid_active_ = true;
  moving_ = true;
  move_start_ms_ = millis();
  last_pid_ms_ = millis();
  good_samples_ = 0;
  stable_last_pos_ = last_pid_input_;
  stable_last_ms_ = millis();
  // 方向语义（极性无关）：运动终点离哪个标定端点更近即为哪种操作——
  // 靠近 full_open -> OPENING，靠近 full_close -> CLOSING。
  // 不可用 target 与当前位置的差值正负判断：本机 full_open(-6862) 数值上
  // 小于 full_close(2295)，"开"是脉冲下降方向，差值法与 >full_close_ 比较法
  // 均会把 open 误报为 CLOSING（HA 图标/文案反转）。
  long d_open = labs(target - full_open_);
  long d_close = labs(target - full_close_);
  this->current_operation =
      (d_open <= d_close) ? cover::COVER_OPERATION_OPENING : cover::COVER_OPERATION_CLOSING;
  this->publish_state();
}

void MotorPIDCover::run_pid_(uint32_t now) {
  long pos = get_pos_pulse();
  double dt = sample_time_ms_ / 1000.0;

  // 与 PID_v1 同语义：ki 乘 dt、kd 除 dt，微分作用于测量值（避免设定点突变冲击）
  double ki_scaled = ki_ * dt;
  double kd_scaled = kd_ / dt;

  double error = setpoint_ - (double) pos;
  integral_ += ki_scaled * error;
  // 积分限幅（与 PID_v1 的 ITerm 钳制一致）
  if (integral_ > PWM_RANGE) integral_ = PWM_RANGE;
  if (integral_ < -PWM_RANGE) integral_ = -PWM_RANGE;

  double d_input = (double) (pos - last_pid_input_);
  last_pid_input_ = pos;

  double output = kp_ * error + integral_ - kd_scaled * d_input;
  if (output > PWM_RANGE) output = PWM_RANGE;
  if (output < -PWM_RANGE) output = -PWM_RANGE;

  motor_run_((int) output);

  ESP_LOGV(TAG, ">pwm: %d", (int) output);
}

void MotorPIDCover::check_stable_(uint32_t now) {
  if (!moving_ || now - stable_last_ms_ < stable_window_ms_)
    return;
  stable_last_ms_ = now;

  long pos = get_pos_pulse();
  if (is_close_((float) pos, (float) stable_last_pos_)) {
    good_samples_++;
  } else {
    stable_last_pos_ = pos;
    good_samples_ = 0;
  }

  if (good_samples_ > stable_samples_) {
    uint32_t stable_time = now - move_start_ms_;
    ESP_LOGI(TAG, "Reached stable: pos=%ld setpoint=%.0f samples=%d, stable time %ums", pos, setpoint_,
             good_samples_, stable_time);
    finish_move_(true);
  }
}

void MotorPIDCover::measure_speed_(uint32_t now) {
  long pos = get_pos_pulse();
  long diff = pos - last_speed_pos_;
  uint32_t dt = now - last_speed_ms_;

  // 位置变化超阈值（高速）或间隔超阈值（低速）时更新速度，与原版一致
  if (abs(diff) > 10 || dt > 50) {
    float speed = dt > 0 ? ((float) diff * 1000.0f / (float) dt) : 0.0f;
    last_speed_ms_ = now;

    if (diff == 0 && dt > 50) {
      speed_ema_ = 0.0f;
    } else {
      speed_ema_ = (1.0f - speed_alpha_) * speed_ema_ + speed_alpha_ * speed;
    }

    if (pos != last_speed_pos_) {
      ESP_LOGV(TAG, ">pos: %ld", pos);
      ESP_LOGV(TAG, ">speed: %.3f", speed_ema_);
    }
    last_speed_pos_ = pos;
  }
}

void MotorPIDCover::finish_move_(bool reached) {
  pid_active_ = false;
  moving_ = false;
  motor_brake_();

  long pos = get_pos_pulse();
  ESP_LOGI(TAG, "Motor stopped at %ld (%s)", pos, reached ? "reached" : "forced");

  // 持久化停机位置（原版仅在自动到位时保存，此处对一切停机持久化，更稳妥）
  save_state_();
  publish_position_();
}

// ------------------------------------------------------------------
// DRV8833 驱动（与原版引脚语义一致）
// ------------------------------------------------------------------

void MotorPIDCover::motor_forward_(int pwm) {
  digitalWrite(sleep_, HIGH);
  if (reversed_) {
    analogWrite(in1_, 0);
    analogWrite(in2_, pwm);
  } else {
    analogWrite(in1_, pwm);
    analogWrite(in2_, 0);
  }
}

void MotorPIDCover::motor_backward_(int pwm) {
  digitalWrite(sleep_, HIGH);
  if (reversed_) {
    analogWrite(in1_, pwm);
    analogWrite(in2_, 0);
  } else {
    analogWrite(in1_, 0);
    analogWrite(in2_, pwm);
  }
}

void MotorPIDCover::motor_run_(int pwm) {
  if (pwm > 0) {
    motor_forward_(pwm);
  } else if (pwm < 0) {
    motor_backward_(-pwm);
  } else {
    motor_brake_();
  }
}

void MotorPIDCover::motor_brake_() {
  analogWrite(in1_, 0);
  analogWrite(in2_, 0);
  digitalWrite(sleep_, LOW);  // 停机即休眠驱动模块
}

// ------------------------------------------------------------------
// 工具与持久化
// ------------------------------------------------------------------

bool MotorPIDCover::is_close_(float val, float dst) const {
  float err = fabsf(val - dst);
  float denom = fmaxf(fabsf(dst), 1.0f);
  return err <= abs_tol_ || (err / denom) <= rel_tol_;
}

void MotorPIDCover::save_state_() {
  MotorState st;
  st.magic = STATE_MAGIC;
  st.full_open = full_open_;
  st.full_close = full_close_;
  st.current = get_pos_pulse();
  st.reversed = reversed_ ? 1 : 0;
  if (pref_.save(&st)) {
    ESP_LOGD(TAG, "State saved: open=%d close=%d pos=%d rev=%d", st.full_open, st.full_close, st.current,
             st.reversed);
  } else {
    ESP_LOGW(TAG, "Failed to save state to flash");
  }
}

bool MotorPIDCover::load_state_(MotorState &st) {
  pref_ = global_preferences->make_preference<MotorState>(fnv1_hash("motor_pid_cover"));
  if (!pref_.load(&st))
    return false;
  if (st.magic != STATE_MAGIC)
    return false;
  return true;
}

}  // namespace motor_pid
}  // namespace esphome
