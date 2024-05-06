#include "service/ir.h"
#include "service/motor.h"
#include "service/battery.h"
#include "service/wireless.h"
#include "application.h"

#include <Arduino.h>

void setup()
{
  // 初始化串口
  Serial.begin(115200);
  Serial.println("\nReset reason: " + ESP.getResetReason());

  // 初始化 WiFi
  WirelessService *wireless_service = WirelessService::get_instance();
  wireless_service->begin();

  // 初始化电机编码器
  MotorService *motor_service = MotorService::get_instance();
  motor_service->begin();

  // 初始化红外接收模块并启动红外遥控数据接收服务
  IRService *ir_service = IRService::get_instance();
  ir_service->begin();

  // 初始化电池电量检测服务
  // BatteryService *battery_service = BatteryService::get_instance();
  // battery_service->begin();

  // 初始化应用程序
  Application *app = Application::get_instance();
  app->begin();
}

void loop()
{
  WirelessService *wireless_service = WirelessService::get_instance();
  // BatteryService *battery_service = BatteryService::get_instance();
  IRService *ir_service = IRService::get_instance();
  MotorService *motor_service = MotorService::get_instance();
  Application *app = Application::get_instance();

  // 更新服务状态
  wireless_service->update();
  // battery_service->update();
  ir_service->update();
  motor_service->update();
  app->update();
}
