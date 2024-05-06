#include "service/wireless.h"

#include <FS.h>
#include <Arduino.h>
#include <ArduinoOTA.h>

#include <WiFiManager.h>
#include <EasyButton.h>
#include <ArduinoJson.h>

WirelessService *WirelessService::m_instance = nullptr;

WirelessService::WirelessService() : m_mqtt_server(),
                                     m_mqtt_port(),
                                     m_mqtt_user(),
                                     m_mqtt_pass(),
                                     m_should_save_config(false)
{
    strcpy(m_mqtt_server, DEF_MQTT_SERVER);
    strcpy(m_mqtt_port, DEF_MQTT_PORT);
}

WirelessService::~WirelessService()
{
}

void WirelessService::begin()
{
    this->load_conf_();

    // 如果是按 RESET 按钮重启, 则检查清除按钮状态
    if (ESP.getResetInfoPtr()->reason == REASON_EXT_SYS_RST)
    {
        this->wait_check_clear_btn_();
    }

    this->setup_wifi_();
    this->setup_ota_();
}

void WirelessService::update()
{
    ArduinoOTA.handle();
}

void WirelessService::clear_settings_and_restart()
{
    WiFiManager wm;
    wm.resetSettings();
    ESP.restart();
}

void WirelessService::setup_wifi_()
{
    // WiFi 初始化为 STAT 模式
    WiFi.mode(WIFI_STA);

    // 可配置的 MQTT 参数
    WiFiManagerParameter custom_mqtt_server("server", "MQTT server", this->m_mqtt_server, sizeof(this->m_mqtt_server));
    WiFiManagerParameter custom_mqtt_port("port", "MQTT port", this->m_mqtt_port, sizeof(this->m_mqtt_port));
    WiFiManagerParameter custom_mqtt_user("user", "MQTT user", this->m_mqtt_user, sizeof(this->m_mqtt_user));
    WiFiManagerParameter custom_mqtt_pass("pass", "MQTT pass", this->m_mqtt_pass, sizeof(this->m_mqtt_pass));

    // 初始化 WiFiManager
    WiFiManager wm;

    this->m_should_save_config = false;
    wm.setSaveConfigCallback(
        [=]()
        {
            Serial.println("Should save MQTT config");
            this->m_should_save_config = true;
        });
    wm.addParameter(&custom_mqtt_server);
    wm.addParameter(&custom_mqtt_port);
    wm.addParameter(&custom_mqtt_user);
    wm.addParameter(&custom_mqtt_pass);

    bool res = wm.autoConnect();
    if (!res)
    {
        Serial.println("Failed to connect to WiFi, rebooting...");
        delay(DEF_FAIL_REBOOT_DELAY);
        ESP.restart();
    }
    Serial.println("Connected to WiFi, local IP & MAC:");
    Serial.println(WiFi.localIP());
    Serial.println(WiFi.macAddress());

    strcpy(this->m_mqtt_server, custom_mqtt_server.getValue());
    strcpy(this->m_mqtt_port, custom_mqtt_port.getValue());
    strcpy(this->m_mqtt_user, custom_mqtt_user.getValue());
    strcpy(this->m_mqtt_pass, custom_mqtt_pass.getValue());

    Serial.println("MQTT info:");
    Serial.println("Server: " + String(this->m_mqtt_server));
    Serial.println("Port: " + String(this->m_mqtt_port));
    Serial.println("User: " + String(this->m_mqtt_user));
    Serial.println("Pass: " + String(this->m_mqtt_pass));

    if (this->m_should_save_config)
    {
        Serial.println("Saving MQTT config ...");
        this->save_conf_();
    }
}

void WirelessService::setup_ota_()
{
    // 初始化 OTA
    ArduinoOTA.setPassword(DEF_OTA_PASSWORD);

    ArduinoOTA.onStart(
        []()
        {
            String type;
            if (ArduinoOTA.getCommand() == U_FLASH)
            {
                type = "sketch";
            }
            else
            { // U_SPIFFS
                type = "filesystem";
            }

            // 卸载关闭 SPIFFS 文件系统以避免 OTA 更新时造成数据损失
            SPIFFS.end();

            Serial.println("Start updating " + type);
        });

    ArduinoOTA.onEnd(
        []()
        {
            Serial.println("\nEnd");
        });
    ArduinoOTA.onProgress(
        [](unsigned int progress, unsigned int total)
        {
            Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
        });
    ArduinoOTA.onError(
        [](ota_error_t error)
        {
            Serial.printf("Error[%u]: ", error);
            if (error == OTA_AUTH_ERROR)
                Serial.println("Auth Failed");
            else if (error == OTA_BEGIN_ERROR)
                Serial.println("Begin Failed");
            else if (error == OTA_CONNECT_ERROR)
                Serial.println("Connect Failed");
            else if (error == OTA_RECEIVE_ERROR)
                Serial.println("Receive Failed");
            else if (error == OTA_END_ERROR)
                Serial.println("End Failed");
        });
    ArduinoOTA.begin();
}

void WirelessService::wait_check_clear_btn_()
{
    // 配置清除按钮
    EasyButton clear_btn(CLEAR_BTN_PIN);
    clear_btn.begin();
    clear_btn.onPressed(
        [=]()
        {
            Serial.println("Clear button activated, clear WiFi setting and rebooting...");
            this->clear_settings_and_restart();
        });

    Serial.printf("Press FLASH button to clear WiFi settings...\n");

    // 等待清除按钮按下并显示进度
    unsigned long clear_check_start = millis();
    while (millis() - clear_check_start < CLEAR_CHECK_DELAY)
    {
        clear_btn.read();
        delay(5);
        Serial.print(".");
    }
    Serial.println();
}

void WirelessService::load_conf_()
{
    Serial.println("Mounting filesystem ...");
    if (SPIFFS.begin())
    {
        Serial.println("Filesystem mounted.");

        if (SPIFFS.exists(MQTT_CONF_FILE))
        {
            // MQTT 配置文件已存在, 读入其数据
            Serial.println("Reading MQTT conf file " + String(MQTT_CONF_FILE) + " ...");
            File conf_file = SPIFFS.open(MQTT_CONF_FILE, "r");
            if (conf_file)
            {
                Serial.println("Opened config file");

                // 解析 JSON 数据
                JsonDocument doc;
                auto err = deserializeJson(doc, conf_file);
                if (!err)
                {
                    Serial.println("Parsed JSON data");
                    strcpy(this->m_mqtt_server, doc["mqtt_server"]);
                    strcpy(this->m_mqtt_port, doc["mqtt_port"]);
                    strcpy(this->m_mqtt_user, doc["mqtt_user"]);
                    strcpy(this->m_mqtt_pass, doc["mqtt_pass"]);
                }
                else
                {
                    Serial.println("Failed to load JSON config data:");
                    Serial.println(err.c_str());
                }

                conf_file.close();
            }
        }
    }
    else
    {
        Serial.println("Failed to mount filesystem.");
    }
}

void WirelessService::save_conf_()
{
    JsonDocument doc;
    doc["mqtt_server"] = this->m_mqtt_server;
    doc["mqtt_port"] = this->m_mqtt_port;
    doc["mqtt_user"] = this->m_mqtt_user;
    doc["mqtt_pass"] = this->m_mqtt_pass;

    if (SPIFFS.begin())
    {
        File conf_file = SPIFFS.open(MQTT_CONF_FILE, "w");
        if (!conf_file)
        {
            Serial.println("Failed to open config file for writing: " + String(MQTT_CONF_FILE));
            return;
        }

        serializeJson(doc, conf_file);
        serializeJson(doc, Serial);
        Serial.println();

        conf_file.close();
    }
    else
    {
        Serial.println("Failed to mount filesystem.");
    }
}
