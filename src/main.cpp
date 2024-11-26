#include "service/ir.h"
#include "service/motor.h"
#include "service/wireless.h"
#include "service/ntp.h"
#include "service/logger.h"
#include "application.h"

#include <Arduino.h>

void setup()
{
  // 初始化串口
  Serial.begin(115200);
  LoggerService::println("\nReset reason: " + ESP.getResetReason());

  // 初始化 WiFi
  WirelessService *wireless_service = WirelessService::get_instance();
  wireless_service->begin();

  // 初始化 NTP 服务
  NTPService *ntp_service = NTPService::get_instance();
  ntp_service->begin();

  // 初始化日志远程访问服务(必须在 WiFi 服务和 NTP 服务之后初始化)
  LoggerService *logger_service = LoggerService::get_instance();
  logger_service->begin();

  // 初始化电机编码器
  MotorService *motor_service = MotorService::get_instance();
  motor_service->begin();

  // 初始化红外接收模块并启动红外遥控数据接收服务
  IRService *ir_service = IRService::get_instance();
  ir_service->begin();

  // 初始化应用程序
  Application *app = Application::get_instance();
  app->begin();
}

void loop()
{
  WirelessService *wireless_service = WirelessService::get_instance();
  NTPService *ntp_service = NTPService::get_instance();
  LoggerService *logger_service = LoggerService::get_instance();
  IRService *ir_service = IRService::get_instance();
  MotorService *motor_service = MotorService::get_instance();
  Application *app = Application::get_instance();

  // 更新服务状态
  wireless_service->update();
  ntp_service->update();
  logger_service->update();
  ir_service->update();
  motor_service->update();
  app->update();
}
