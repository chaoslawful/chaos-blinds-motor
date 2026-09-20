# chaos-blinds-motor 架构评估与 ESPHome 重构方案

> 评审日期：2026-09-20 · 评审对象：`src/`（Arduino/ESP8266 现行固件）与 `esphome/`（重构草稿）
> 目标平台：NodeMCU Lolin V3（ESP8266 @160MHz），DRV8833 + 370 蜗轮减速电机（霍尔编码器 12PPR）+ HX1838 红外接收

---

## 1. 现有代码核心逻辑与风险分析

### 1.1 架构总览

现行固件为典型的「单例服务 + 协作式轮询」Arduino 架构，分层清晰、职责划分合理：

```
main.cpp ── setup(): 顺序初始化，loop(): 逐服务 update()
├─ WirelessService   WiFiManager 配网 + MQTT 参数持久化 + ArduinoOTA
├─ NTPService        每小时对时（cn.pool.ntp.org, UTC+8）
├─ LoggerService     串口日志 + 4KB 环形缓冲 + 8080 端口 Web 日志
├─ MotorService      Encoder(PJRC) 测位 + PID_v1 位置环 + DRV8833 PWM 驱动
├─ IRService         TinyIRReceiver (NEC) + 按键去抖/重复 + 回调分发
└─ Application       ArduinoHA(MQTT) 三个按钮 + 电机状态传感器 + LittleFS 标定持久化
```

**核心控制逻辑（值得保留的设计）：**

1. **位置闭环**：`goto_pos()` 设 PID 目标 → `_poll_run_pid()` 每 5ms 采样计算 → 输出 ±255 PWM → `motor_run()` 按 `m_reverse_dir` 映射到 DRV8833 IN1/IN2。
2. **到位判定**：`_poll_check_stable()` 以 50ms 采样间隔连续 20 个样本位置不变（容差 ±80 脉冲或相对 1%）判稳 → 刹车、关 PID、驱动模块休眠 → 回调 `on_motor_stop_()` 把当前位置写入 LittleFS（`/motor_conf.json`）。该判稳逻辑同时**兼作堵转保护**（堵转即位置不变 → 1 秒后自动停机），是一个隐式但有效的安全机制。
3. **红外组合键协议**：以「`0` 键 + 位置未移动」为前提条件的二级命令（`0,#` 切方向 / `0,1` 标全开 / `0,3` 标全关 / `0,2` 清标定 / `0,4` 手动对时），有效防止误触。
4. **掉电位置恢复**：标定点与当前位置持久化于 LittleFS，重启后 `set_motor_pos()` 恢复编码器计数基准。

### 1.2 可靠性缺陷清单（按严重度排序）

| # | 级别 | 位置 | 缺陷与后果 |
|---|------|------|-----------|
| R1 | **高·安全** | `wireless.cpp:96-100` + `logger.cpp:14-18` | **MQTT 密码明文写入日志**，而 8080 端口的 Web 日志**无任何鉴权**，局域网内任何人 `GET http://<ip>:8080/` 即可读到 MQTT 凭据、WiFi MAC、IP 等敏感信息 |
| R2 | **高·内存破坏** | `wireless.cpp:207-210` | `strcpy(m_mqtt_server, doc["mqtt_server"])` 等 4 处**无长度检查**的字符串拷贝。若 `/mqtt_conf.json` 被异常写入超长值（OTA 刷入、LittleFS 数据残留），直接溢出 40/6 字节栈成员缓冲区，后果不可预测 |
| R3 | **高·引脚冲突** | `wireless.h:8` vs `pins.h:10` | `CLEAR_BTN_PIN = D3` 与 `DRV_IN1_PIN = D3` **同一个 GPIO 被两个服务占用**。RESET 重启后的 3 秒清配检测窗口内 D3 被 `EasyButton` 设为输入上拉，而电机侧认为它是 PWM 输出——电机上电自检/意外启动与该窗口叠加时行为未定义 |
| R4 | **高·数据丢失** | `application.cpp:131-151` | 标定配置写入**非原子**：`LittleFS.open("w")` 先截断再写，写入中途掉电 → 文件损坏 → 下次启动 JSON 解析失败 → **静默回退到全零标定**（`load_motor_conf_` 仅打日志），升窗器失去行程边界，全靠蜗轮蜗杆自锁兜底 |
| R5 | **高·失控风险** | `motor.cpp` 整体 | **无运动超时**。判稳逻辑能兜住「堵转」，但兜不住「编码器信号抖动导致位置永不满足稳定条件」——PID 将以满 PWM 持续驱动，直到机械限位或烧毁。缺独立的 `max_run_time` 兜底 |
| R6 | 中 | `wireless.cpp:42-45, 80-86` | `update()` 只跑 `ArduinoOTA.handle()`，**WiFi 断线后无重连逻辑**；`autoConnect()` 失败则 5 秒后重启——若路由器宕机，设备进入重启死循环且期间电机完全不可控 |
| R7 | 中 | `application.cpp:89` | `ESP.wdtEnable(60000)`：**ESP8266 Arduino 内核忽略该参数**，实际启用的是 ~1s 的软看门狗。常量名 `WATCHDOG_INTERVAL_MS=60000` 造成「有 60 秒余量」的错觉，真实语义与代码注释不符 |
| R8 | 中 | `logger.cpp:42-53` | 日志缓冲用 `String` 逐条拼接 + 超长时 `substring()` 截断：ESP8266 堆碎片化重灾区。叠加电机运行时 10ms 级的 Teleplot 输出，长期运行内存耗尽风险显著；`printf` 还用了 2KB 栈缓冲 |
| R9 | 中 | `motor.cpp:203-245` | 到位容差 `ABS_ERR_TOL=80` 脉冲。编码器 12PPR（电机轴），80 脉冲 ≈ **电机轴 6.7 转**的滞环。蜗轮减速后对应行程误差可能可接受，但该值与「精确位置控制」的设计目标相悖，且从未被标定验证流程约束 |
| R10 | 中 | `wireless.cpp:112` 等 | OTA 密码 `chaos123456` 硬编码进固件源码并提交进 Git 仓库；同一字符串复用为 WiFiManager AP 密码 |
| R11 | 低 | `ntp.cpp:55-80` | 每小时一次的 NTP 同步是**阻塞式 UDP 收发**（超时期间电机 PID 轮询停摆，最长可达 NTPClient 默认超时）；同理 3 秒清配检测窗口阻塞 `setup()` |
| R12 | 低 | `application.h:14` / `pins.h:3` | `SENSOR_BAT_NAME`、`BATTERY_PIN` 为**死代码**——电池电压检测声明了从未实现 |
| R13 | 低 | 全局 | 依赖 `wnatth3/WiFiManager@^2.0.16-rc.2`（release candidate 非稳定版）；MQTT 无 TLS（局域网内可接受，但应明示） |
| R14 | 低 | `motor.h:12` | PWM 频率 128Hz 落在人耳可闻频段，DRV8833 驱动下电机会有明显啸叫；不影响功能，影响体验 |

**总体评价**：分层与组合键、判稳保护等设计体现了良好的工程直觉；但 R1–R5 均触及「凭据安全、内存安全、数据完整性、失控保护」四条嵌入式底线，且这些问题全部属于**手写基础设施**（WiFi/OTA/日志/持久化）——恰好是成熟框架已经工业化解决的部分。

### 1.3 `esphome/` 目录现有重构草稿的评估

`esphome/chaos-blinds-motor.yaml` + `motor_pid_control.h` 方向正确，但**当前状态不可编译、不可部署**，属于半成品：

- **YAML 引用了不存在的能力**：`id(manual_control).execute_move("up")`、`id(auto_control).move_to_position(...)`——`script:` 不接受参数调用；自定义组件 `MotorPIDControl` 从未在 YAML 中以 `custom_component:`/`external_components:` 注册，两者根本没有接线。
- **使用了不存在的 ESPHome 配置项**：顶层 `pid: - platform: pid`（ESPHome 的 PID 只内嵌于 climate 组件）、`esp8266.framework.cpu_freq`（无此键）、`adc.attenuation`（ESP32 专用，ESP8266 非法）、动作 `time.sync`（不存在）。
- **语义错误**：`rotary_encoder.publish_state(0)` 只改上报值、**不清内部计数器**，清标定后位置跟踪立即失真；`manual_control_action` text_sensor 从未被赋值，手动控制脚本永远走不进分支。
- **结论**：草稿可作为功能映射的参考蓝本，但必须重写，不能修补续用。

---

## 2. 基于成熟框架改造的可行性与框架对比

### 2.1 候选框架对比

| 维度 | **ESPHome + 外部组件** | Tasmota | ESPEasy | 维持 Arduino 自研 |
|------|----------------------|---------|---------|------------------|
| HA 集成 | 原生 API（加密、自动发现、Cover 实体） | MQTT，需手动配 discovery | MQTT/HTTP，需手动配 | 现 ArduinoHA(MQTT) |
| 位置闭环 PID | ❌ 无现成组件，**需自写外部组件**（~200 行 C++） | ❌ 无能力，规则引擎做不了 5ms 控制环 | ❌ 同上 | ✅ 已有 |
| 红外 NEC 接收 | ✅ `remote_receiver` 原生 | ✅ 有 | ✅ 有 | ✅ TinyIRReceiver |
| 编码器 | ✅ `rotary_encoder` 原生（中断驱动） | 部分 | 部分 | ✅ Encoder 库 |
| 持久化 | ✅ `preferences`（带 CRC、磨损均衡的 ESP32 式 flash 抽象） | ✅ | ✅ | ⚠️ 手写 JSON/LittleFS |
| OTA / 配网 / 看门狗 | ✅ 全部工业化内置 | ✅ | ✅ | ⚠️ 手写、即 R1/R3/R6/R7 出处 |
| ESP8266 支持 | ✅ 一等公民 | ✅ | ✅ | ✅ |
| 定制自由度 | 高（lambda + 外部组件可写任意 C++） | 低 | 低 | 完全自由 |
| 维护成本 | **低**（基础设施由框架维护） | 低 | 低 | 高（R1–R13 全需自行修复） |

**结论：ESPHome + 一个自定义外部组件是唯一切实可行的路线。** 理由：

1. 本项目的「灵魂」是 5ms 级位置 PID + 编码器判稳，任何纯配置框架（Tasmota/ESPEasy）都无法表达，终归要写 C++——那就应该选一个「基础设施全托管、只让我写电机控制」的框架，即 ESPHome external component 模式。
2. R1/R3/R6/R7/R8/R10 六条缺陷全部位于 WiFi/OTA/日志/看门狗层，ESPHome 将这些整体替换为久经考验的实现，**风险项直接消项**，而非修补。
3. HA 侧从 MQTT 三按钮升级为**原生 Cover 实体**（带位置百分比反馈），可用 HA 的 `cover.open/close/stop/set_position` 全套语义，体验显著优于现状。
4. 回退路径安全：ESPHome 与现固件各自独立刷写，互不依赖；现 Arduino 仓库保留即可随时回刷。

### 2.2 可行性中需要正视的两个代价

- **PID 控制环必须重写为 ESPHome Component**（不能完全复用 PID_v1，但控制律、判稳逻辑、参数可原样移植，工作量约 200 行）。
- **红外组合键状态机**需在 `remote_receiver.on_nec` 的 lambda 中重建，逻辑可逐行翻译，无技术障碍。

---

## 3. 完整改造方案

### 3.1 功能映射表（现行 → ESPHome）

| 现行实现 | ESPHome 落点 | 说明 |
|---------|-------------|------|
| WirelessService（WiFiManager 配网 + MQTT 参数） | `wifi:` + `ap:` + `captive_portal:` | 回退 AP 即配网门户，等价于 WiFiManager；MQTT 参数文件整体废弃 |
| ArduinoHA 三按钮（打开/关闭/停止） | **`cover:` 平台（custom/template）** | 升级为原生 Cover：open/close/stop + 位置百分比上报 |
| ArduinoOTA | `ota:` | 密码入 `secrets.yaml`，不再硬编码 |
| NTPService | `time: - platform: sntp`（或 `homeassistant`） | 非阻塞，自带重试 |
| LoggerService（串口+Web 日志） | `logger:` + `web_server:` | Web 服务器**带鉴权**，修复 R1；Teleplot 输出改为 DEBUG 级 |
| LittleFS `/motor_conf.json` | `preferences` + `number: restore_value: true` | flash 写入带 CRC 与原子性，修复 R4 |
| IRService（NEC + 去抖 + 组合键） | `remote_receiver: on_nec:` lambda 状态机 | 逐键映射，含 `0+x` 前提校验 |
| MotorService（编码器+PID+DRV8833+判稳+休眠） | **`external_components: motor_pid`（唯一需自写的 C++ 组件）** | 移植控制律与判稳逻辑 |
| 软件看门狗（语义错误） | ESPHome 内置看门狗管理 | 修复 R7 |
| FLASH 键清配（与 D3 冲突） | 废弃，改由 `captive_portal` 配网 | 修复 R3（D3 回归纯 PWM 用途） |
| 电池电压死代码 | 不移植（用户决策：A0 无分压电路，无用途） | 死代码直接删除 |

### 3.2 模块划分

```
esphome/
├─ chaos-blinds-motor.yaml      # 主配置：引脚、WiFi、API、OTA、传感器、IR 状态机
├─ secrets.yaml                 # wifi/api/ota/web 凭据（gitignore）
└─ components/
   └─ motor_pid/
      ├─ __init__.py            # ESPHome 代码生成：声明 Cover + 配置 schema
      ├─ motor_pid.h            # 组件声明
      └─ motor_pid.cpp          # 控制环实现
```

**`motor_pid` 外部组件设计**（对现行 `MotorService` 的等价移植）：

```cpp
class MotorPIDCover : public Component, public cover::Cover {
  // 配置项（YAML 可配）：
  //   pin_in1/pin_in2/pin_sleep, pin_enc_a/pin_enc_b,
  //   kp/ki/kd, sample_time_ms(5), stable_samples(20), stable_window_ms(50),
  //   pos_tolerance(80), max_run_time_s(★新增), pwm_frequency(★建议提到 ≥20kHz 消啸叫)
  //
  // loop(): 5ms 调度 → 读 Encoder → PID → motor_run() → 判稳/超时双判定
  // 判稳或超时 → 刹车 + driver_sleep + 持久化当前位置(preferences) + 回调 HA
  //
  // CoverTraits: SUPPORT_OPEN | SUPPORT_CLOSE | SUPPORT_STOP | SUPPORT_SET_POSITION
  // 位置 ↔ 百分比: pos% = (cur - full_close) / (full_open - full_close)
  //
  // ★ 相对现行版的新增安全特性：
  //   1. max_run_time_s 硬超时（默认 60s），到点无条件停机 → 修复 R5
  //   2. 持久化原子写（preferences 框架保证）→ 修复 R4
  //   3. 位置持久化加「上次正常关机」标志，异常上电时标记位置不可信
};
```

**红外状态机**：在 YAML `on_nec:` lambda 中重建，变量 `prev_key`/`prev_key_pos` 用 `id(...)` 全局量或 lambda static，语义与现行 `on_ir_key_()` 一一对应（含 `0,1/0,2/0,3/0,#/0,4` 五个组合）。

### 3.3 配置策略

1. **引脚全部走 `substitutions:`**，与 `pins.h` 一一对应（D2/D3/D4/D5/D6/D7/D8/D1），改板型只改一处。
2. **标定量（全开位/全关位/方向反转）用 `number:`/`switch:` + `restore_value`**——HA 界面与红外组合键双通道均可标定，且掉电安全。
3. **PID 参数暴露为 HA `number` 实体**（set_action 调组件 `set_pid_tunings`），在线调参免重编译——这是相对现行版的能力增量。
4. **分层日志**：正常运行 INFO；Teleplot 位置/速度/PWM 数据归入 DEBUG 并可用开关关闭，避免 R8 的内存压力。
5. **凭据全部入 `secrets.yaml`**，YAML 主文件可安全提交 Git；Web 日志门户启用 `auth:`。
6. **`safe_mode` 与 `reboot_timeout` 开启**：固件异常启动循环 10 次自动进安全模式（纯 OTA 恢复通道），替代现行「连不上 WiFi 就重启」的粗暴策略（修复 R6）。
7. **刷写迁移步骤**：① 保留现固件备份（`esptool read_flash`）；② 首次 USB 刷 ESPHome；③ 红外 `0,2` 清标定 → `0,1/0,3` 重新标定行程；④ HA 中验证 Cover 实体；⑤ 观察 48 小时无异常后，Arduino 仓库打 tag 归档。

### 3.4 风险消项核对表

| 风险 | 改造后状态 |
|------|-----------|
| R1 凭据泄露 | ✅ web_server 鉴权 + 日志不落凭据 |
| R2 strcpy 溢出 | ✅ 该代码整体删除 |
| R3 D3 引脚冲突 | ✅ 清配逻辑废弃，D3 独占 PWM |
| R4 配置非原子写 | ✅ preferences 框架（CRC + 原子） |
| R5 无运动超时 | ✅ max_run_time_s 硬超时（新设计） |
| R6 WiFi 无重连 | ✅ ESPHome wifi 自动重连 + safe_mode |
| R7 看门狗语义 | ✅ 框架托管 |
| R8 String 碎片 | ✅ logger 组件环形缓冲，无动态拼接 |
| R9 容差过大 | ⚠️ 参数保留但暴露为可配置项，标定流程中验证 |
| R10 硬编码密码 | ✅ secrets.yaml |
| R11 阻塞 NTP | ✅ sntp 非阻塞 |
| R12 电池死代码 | ⚠️ 不移植（A0 无分压电路，用户确认无用途，无需纳入新版） |
| R13 RC 依赖 | ✅ 依赖收敛到 ESPHome 单一定期升级点 |
| R14 128Hz 啸叫 | ✅ pwm_frequency 可配，建议 20kHz |

---

## 4. 结论

1. 现行 Arduino 固件架构分层合理、控制逻辑（PID + 判稳 + 组合键）设计成熟，**值得移植而非重写**；但基础设施层存在 5 条高危缺陷，全部源于手写轮子。
2. ESPHome 重构**完全可行且为最优路线**：基础设施整体托管消项 11 条风险，仅位置闭环需以 ~200 行外部组件移植；HA 侧还可从三按钮升级为原生 Cover，属于净增益。
3. `esphome/` 现有草稿不可编译，需推倒重写——但其功能映射思路可直接继承，本方案的模块划分即为重写蓝本。
