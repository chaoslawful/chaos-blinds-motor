import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import cover
from esphome.const import CONF_ID

motor_pid_ns = cg.esphome_ns.namespace("motor_pid")
MotorPIDCover = motor_pid_ns.class_("MotorPIDCover", cover.Cover, cg.Component)

CONF_IN1_PIN = "in1_pin"
CONF_IN2_PIN = "in2_pin"
CONF_SLEEP_PIN = "sleep_pin"
CONF_ENC_A_PIN = "encoder_a_pin"
CONF_ENC_B_PIN = "encoder_b_pin"
CONF_KP = "kp"
CONF_KI = "ki"
CONF_KD = "kd"
CONF_SAMPLE_TIME = "sample_time"
CONF_STABLE_SAMPLES = "stable_samples"
CONF_STABLE_WINDOW = "stable_window"
CONF_ABS_TOLERANCE = "abs_tolerance"
CONF_REL_TOLERANCE = "rel_tolerance"
CONF_MAX_RUN_TIME = "max_run_time"
CONF_PWM_FREQUENCY = "pwm_frequency"
CONF_REVERSE = "reverse"
CONF_SPEED_CUTOFF_HZ = "speed_cutoff_hz"

CONFIG_SCHEMA = cover.cover_schema(MotorPIDCover).extend(
    {
        cv.GenerateID(): cv.declare_id(MotorPIDCover),
        cv.Required(CONF_IN1_PIN): pins.gpio_output_pin_schema,
        cv.Required(CONF_IN2_PIN): pins.gpio_output_pin_schema,
        cv.Required(CONF_SLEEP_PIN): pins.gpio_output_pin_schema,
        cv.Required(CONF_ENC_A_PIN): pins.gpio_input_pin_schema,
        cv.Required(CONF_ENC_B_PIN): pins.gpio_input_pin_schema,
        cv.Optional(CONF_KP, default=2.0): cv.float_,
        cv.Optional(CONF_KI, default=0.2): cv.float_,
        cv.Optional(CONF_KD, default=0.12): cv.float_,
        cv.Optional(CONF_SAMPLE_TIME, default="5ms"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_STABLE_SAMPLES, default=20): cv.positive_int,
        cv.Optional(CONF_STABLE_WINDOW, default="50ms"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_ABS_TOLERANCE, default=80.0): cv.float_,
        cv.Optional(CONF_REL_TOLERANCE, default=0.01): cv.float_,
        # 新增安全特性：运动硬超时（原版无），到点无条件停机
        cv.Optional(CONF_MAX_RUN_TIME, default="60s"): cv.positive_time_period_milliseconds,
        # 原版为 128Hz（可闻啸叫），默认 20kHz 消音；如需完全一致可改回 128Hz
        cv.Optional(CONF_PWM_FREQUENCY, default="20kHz"): cv.frequency,
        cv.Optional(CONF_REVERSE, default=False): cv.boolean,
        cv.Optional(CONF_SPEED_CUTOFF_HZ, default=5.0): cv.float_,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await cover.register_cover(var, config)

    for conf_key, setter in (
        (CONF_IN1_PIN, "set_in1_pin"),
        (CONF_IN2_PIN, "set_in2_pin"),
        (CONF_SLEEP_PIN, "set_sleep_pin"),
        (CONF_ENC_A_PIN, "set_encoder_a_pin"),
        (CONF_ENC_B_PIN, "set_encoder_b_pin"),
    ):
        # 传入原始 GPIO 编号（与原版 Arduino 代码一致直接操作硬件层）
        cg.add(getattr(var, setter)(config[conf_key]["number"]))

    cg.add(var.set_pid(config[CONF_KP], config[CONF_KI], config[CONF_KD]))
    cg.add(var.set_sample_time_ms(config[CONF_SAMPLE_TIME].total_milliseconds))
    cg.add(var.set_stable_params(config[CONF_STABLE_SAMPLES], config[CONF_STABLE_WINDOW].total_milliseconds))
    cg.add(var.set_tolerance(config[CONF_ABS_TOLERANCE], config[CONF_REL_TOLERANCE]))
    cg.add(var.set_max_run_time_ms(config[CONF_MAX_RUN_TIME].total_milliseconds))
    cg.add(var.set_pwm_frequency(config[CONF_PWM_FREQUENCY]))
    cg.add(var.set_reverse(config[CONF_REVERSE]))
    cg.add(var.set_speed_cutoff_hz(config[CONF_SPEED_CUTOFF_HZ]))

    # 与原版一致的 PJRC 编码器库
    cg.add_library("paulstoffregen/Encoder", "^1.4.4")
