#include "service/wireless.h"
#include "service/motor.h"
#include "service/ir.h"
#include "service/logger.h"
#include "application.h"

#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>

Application *Application::m_instance = nullptr;

Application::Application() : m_cover_full_close_pos(0),
                             m_cover_full_open_pos(0),
                             m_cover_current_pos(0),
                             m_motor_reversed(false),
                             m_wifi_client(),
                             m_device(),
                             m_mqtt(m_wifi_client, m_device),
                             m_btn_open(Application::BTN_OPEN_NAME),
                             m_btn_close(Application::BTN_CLOSE_NAME),
                             m_btn_stop(Application::BTN_STOP_NAME),
                             m_sensor_motor(Application::SENSOR_MOTOR_NAME),
                             m_last_ir_key(KEY_UNKNOWN),
                             m_last_ir_key_pos(0)
{
}

Application::~Application()
{
}

void Application::begin()
{
    // 加载之前保存的电机标定位置及当前初始位置
    this->load_motor_conf_();

    // 设置红外遥控器事件处理函数
    IRService *ir_service = IRService::get_instance();
    ir_service->on_anykey(&Application::on_ir_key_);

    MotorService *ms = MotorService::get_instance();
    // 设置电机当前位置
    ms->set_motor_pos(this->m_cover_current_pos);
    ms->set_reverse(this->m_motor_reversed);
    ms->set_stop_callback(&Application::on_motor_stop_);

    // 获取 WiFi MAC 地址
    byte mac[WL_MAC_ADDR_LENGTH];
    WiFi.macAddress(mac);

    // 设置 HA 设备唯一标识
    m_device.setUniqueId(mac, sizeof(mac));
    m_device.setName("混沌猫升窗器");
    m_device.setSoftwareVersion("1.0.0");
    m_device.setManufacturer("混沌猫的窝");
    m_device.setModel("CBM-001");

    // 使用 LWT 特性，当设备离线时，HA 会自动更新设备状态
    m_device.enableSharedAvailability();
    m_device.enableLastWill();

    // 配置窗帘控制按钮
    m_btn_open.setName("打开");
    m_btn_open.setIcon("mdi:arrow-up");
    m_btn_open.setRetain(false);
    m_btn_open.onCommand(&Application::on_cover_command_);

    m_btn_close.setName("关闭");
    m_btn_close.setIcon("mdi:arrow-down");
    m_btn_open.setRetain(false);
    m_btn_close.onCommand(&Application::on_cover_command_);

    m_btn_stop.setName("停止");
    m_btn_stop.setIcon("mdi:stop");
    m_btn_open.setRetain(false);
    m_btn_stop.onCommand(&Application::on_cover_command_);

    m_sensor_motor.setName("电机状态");
    m_sensor_motor.setIcon("mdi:engine");
    m_sensor_motor.setValue("Stopped");

    // 连接 HA 的 MQTT 服务器
    WirelessService *ws = WirelessService::get_instance();
    m_mqtt.begin(ws->mqtt_server(), ws->mqtt_port(), ws->mqtt_user(), ws->mqtt_pass());

    // 启用软件看门狗
    ESP.wdtEnable(Application::WATCHDOG_INTERVAL_MS);
}

void Application::update()
{
    // MQTT 通信
    m_mqtt.loop();

    // 喂狗
    ESP.wdtFeed();
}

void Application::load_motor_conf_()
{
    if (LittleFS.begin())
    {
        if (LittleFS.exists(MOTOR_CONF_FILE))
        {
            File conf_file = LittleFS.open(MOTOR_CONF_FILE, "r");
            if (conf_file)
            {
                JsonDocument doc;
                auto err = deserializeJson(doc, conf_file);
                if (!err)
                {
                    this->m_cover_full_close_pos = doc["full_close_pos"];
                    this->m_cover_full_open_pos = doc["full_open_pos"];
                    this->m_cover_current_pos = doc["current_pos"];
                    // 未设置 reversed 键时默认电机转向为正向
                    this->m_motor_reversed = doc["reversed"] | false;
                }
                else
                {
                    LoggerService::printf("Failed to parse motor config file: %s\n", err.c_str());
                }
                conf_file.close();
            }
        }
        else
        {
            LoggerService::println("Motor config file " + String(MOTOR_CONF_FILE) + " not found, using default values.");
        }
    }
    else
    {
        LoggerService::println("Failed to open filesystem!");
    }
}

void Application::save_motor_conf_()
{
    if (LittleFS.begin())
    {
        JsonDocument doc;
        doc["full_close_pos"] = this->m_cover_full_close_pos;
        doc["full_open_pos"] = this->m_cover_full_open_pos;
        doc["current_pos"] = this->m_cover_current_pos;
        doc["reversed"] = this->m_motor_reversed;

        File conf_file = LittleFS.open(MOTOR_CONF_FILE, "w");
        if (!conf_file)
        {
            LoggerService::println("Failed to open motor config file for writing: " + String(MOTOR_CONF_FILE));
            return;
        }

        serializeJson(doc, conf_file);
        serializeJson(doc, Serial);
        LoggerService::println();

        conf_file.close();
    }
    else
    {
        LoggerService::println("Failed to open filesystem!");
    }
}

void Application::on_motor_stop_(long cur_pos)
{
    Application *app = Application::get_instance();

    LoggerService::println("Callback: Motor stopped at position: " + String(cur_pos));

    // 保存电机当前位置
    app->m_cover_current_pos = cur_pos;
    app->save_motor_conf_();

    // 设置电机传感器状态
    app->m_sensor_motor.setValue("Stopped");
}

void Application::on_cover_command_(HAButton *sender)
{
    Application *app = Application::get_instance();
    MotorService *ms = MotorService::get_instance();

    if (sender == &(app->m_btn_open))
    {
        LoggerService::println("Command: Blinds auto open");
        ms->goto_pos(app->m_cover_full_open_pos);

        // 设置电机传感器状态
        app->m_sensor_motor.setValue("Opening");
    }
    else if (sender == &(app->m_btn_close))
    {
        LoggerService::println("Command: Blinds auto close");
        ms->goto_pos(app->m_cover_full_close_pos);

        // 设置电机传感器状态
        app->m_sensor_motor.setValue("Closing");
    }
    else if (sender == &(app->m_btn_stop))
    {
        LoggerService::println("Command: Blinds stop");
        ms->stop();

        // 设置电机传感器状态
        app->m_sensor_motor.setValue("Stopped");
    }
}

void Application::on_ir_key_(IRKey key)
{
    Application *app = Application::get_instance();
    MotorService *ms = MotorService::get_instance();
    long cur_pos = ms->get_pos_pulse();

    switch (key)
    {
    case KEY_LEFT: // 窗帘手动升起 (CCW)
        LoggerService::println("IR remote: Blinds manual open");
        ms->backward(MotorService::PWM_MIN_SPEED);

        // 设置电机传感器状态
        app->m_sensor_motor.setValue("Opening");
        break;
    case KEY_RIGHT: // 窗帘手动放下 (CW)
        LoggerService::println("IR remote: Blinds manual close");
        ms->forward(MotorService::PWM_MIN_SPEED);

        // 设置电机传感器状态
        app->m_sensor_motor.setValue("Closing");
        break;
    case KEY_OK: // 电机停止
        LoggerService::println("IR remote: Blinds stop");
        ms->stop();

        // 设置电机传感器状态
        app->m_sensor_motor.setValue("Stopped");
        break;
    case KEY_UP: // 电机运行至完全打开
        LoggerService::println("IR remote: Blinds auto open");
        ms->goto_pos(app->m_cover_full_open_pos);

        // 设置电机传感器状态
        app->m_sensor_motor.setValue("Opening");
        break;
    case KEY_DOWN: // 电机运行至完全关闭
        LoggerService::println("IR remote: Blinds auto close");
        ms->goto_pos(app->m_cover_full_close_pos);

        // 设置电机传感器状态
        app->m_sensor_motor.setValue("Closing");
        break;
    case KEY_POUND: // 切换电机转向
        if (app->m_last_ir_key == KEY_0 && app->m_last_ir_key_pos == cur_pos)
        {
            // 顺序按下 0、# 键，切换电机转向
            LoggerService::println("IR remote: Toggle motor direction");
            app->m_motor_reversed = !ms->get_reverse();
            LoggerService::println("Motor direction set to: " + String(app->m_motor_reversed ? "reversed" : "normal"));

            // 设置电机转向
            ms->set_reverse(app->m_motor_reversed);

            // 保存电机配置
            app->save_motor_conf_();
        }
        else
        {
            LoggerService::println("IR remote: Toggle motor direction failed, wrong key sequence");
        }
        break;
    case KEY_2: // 清除电机标定位置
        if (app->m_last_ir_key == KEY_0 && app->m_last_ir_key_pos == cur_pos)
        {
            // 顺序按下 0、2 键，清除电机标定位置
            LoggerService::println("IR remote: Blinds clear motor calibration");
            app->m_cover_full_close_pos = 0;
            app->m_cover_full_open_pos = 0;
            app->m_cover_current_pos = 0;

            // 重置电机编码器位置
            ms->set_motor_pos(0);

            // 保存电机标定位置
            app->save_motor_conf_();
        }
        else
        {
            LoggerService::println("IR remote: Blinds clear motor calibration failed, wrong key sequence");
        }
        break;
    case KEY_1: // 标记电机当前位置为打开点
        if (app->m_last_ir_key == KEY_0 && app->m_last_ir_key_pos == cur_pos)
        {
            // 顺序按下 0、1 键，标记电机当前位置为打开点
            LoggerService::println("IR remote: Blinds mark full open position");
            app->m_cover_full_open_pos = cur_pos;

            // 保存电机标定位置
            app->save_motor_conf_();
        }
        else
        {
            LoggerService::println("IR remote: Blinds mark full open position failed, wrong key sequence");
        }
        break;
    case KEY_3: // 标记电机当前位置为关闭点
        if (app->m_last_ir_key == KEY_0 && app->m_last_ir_key_pos == cur_pos)
        {
            // 顺序按下 0、3 键，标记电机当前位置为关闭点
            LoggerService::println("IR remote: Blinds mark full close position");
            app->m_cover_full_close_pos = cur_pos;

            // 保存电机标定位置
            app->save_motor_conf_();
        }
        else
        {
            LoggerService::println("IR remote: Blinds mark full close position failed, wrong key sequence");
        }
        break;
    case KEY_0: // 功能键序列开始
        LoggerService::println("IR remote: Blinds function key sequence start");
        break;
    default:
        LoggerService::println("IR remote: unknown key pressed");
        break;
    }

    app->m_last_ir_key = key;
    app->m_last_ir_key_pos = cur_pos;
}
