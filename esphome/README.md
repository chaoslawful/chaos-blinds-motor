# 混沌猫升窗器 · ESPHome 版

Arduino 版（`src/`）的 ESPHome 重构实现，功能完全对齐，基础设施（WiFi/OTA/日志/持久化/看门狗）全部由 ESPHome 托管。重构动机与风险分析见 `../doc/架构评估与ESPHome重构方案.md`。

## 目录结构

```
esphome/
├─ chaos-blinds-motor.yaml    # 主配置（引脚/WiFi/API/OTA/红外状态机/HA 实体）
├─ secrets.yaml               # 凭据（首次使用前填写）
└─ components/
   └─ motor_pid/              # 自写外部组件：位置闭环 PID 电机 Cover
      ├─ __init__.py          # ESPHome 代码生成与配置校验
      ├─ motor_pid.h
      └─ motor_pid.cpp        # 控制环（移植自原版 MotorService）
```

## 快速开始

```bash
pip install esphome
cd esphome

# 1. 填写 secrets.yaml（api_encryption_key 用 openssl rand -base64 32 生成）

# 2. 校验配置
esphome config chaos-blinds-motor.yaml

# 3. 首次 USB 烧录（之后即可 OTA）
esphome run chaos-blinds-motor.yaml
```

烧录前建议先用 `esptool.py read_flash` 备份原版固件，以便随时回退（Arduino 版仓库保留即可）。

## 标定流程（与原版一致）

红外遥控器操作（所有标定键需先按 `0` 且电机位置不动）：

| 按键 | 功能 |
|------|------|
| ← / → | 手动升起 / 放下（全速） |
| OK | 停止 |
| ↑ / ↓ | 自动运行至全开 / 全关 |
| 0,1 | 标定当前位置为全开点 |
| 0,3 | 标定当前位置为全关点 |
| 0,2 | 清除标定并归零编码器 |
| 0,# | 切换电机转向 |
| 0,4 | 手动对时（本版 SNTP 自动对时，此键仅打日志） |

Home Assistant 侧提供等价能力：原生 **Cover 实体**（开/关/停/任意位置百分比 + 位置反馈）、标定按钮三个、转向反转开关、WiFi 信号/运行时长传感器。

## 与原版差异（有意为之的改动）

| 项 | 原版 | 本版 | 原因 |
|----|------|------|------|
| HA 通道 | MQTT + ArduinoHA 三按钮 | 原生 API（加密）+ Cover 实体 | 修复凭据泄露（R1），体验升级 |
| 清 WiFi 配 | RESET 后按 FLASH 键（占用 D3） | 回退 AP + captive_portal | 修复 D3 引脚冲突（R3） |
| 配置持久化 | LittleFS 裸 JSON 写 | ESPHome preferences（原子 + CRC） | 修复掉电丢标定（R4） |
| 运动超时 | 无 | `max_run_time` 默认 60s 硬停机 | 新增失控兜底（R5） |
| 手动停止后 | 不保存位置 | 保存位置 | 更稳妥 |
| PWM 频率 | 128Hz（可闻啸叫） | 默认 20kHz（YAML 可改回） | 体验改进（R14） |
| 主频 | 160MHz 超频 | 80MHz | ESPHome 不支持该配置；5ms 环裕量充足 |
| Web 日志 | 8080 无鉴权、含明文密码 | 80 端口带鉴权 | 安全 |
| 电池电压 | pins.h 预留死代码 | 不移植（A0 无分压电路，无实际用途） | 用户确认移除 |

## PID 调试

把 YAML 中 `logger.level` 临时改为 `VERBOSE`，串口日志即输出 Teleplot 格式的 `>pos:` / `>speed:` / `>pwm:` 数据流，与原版调试方式一致。
