#pragma once

#define BATTERY_PIN A0 // 电池电压检测引脚

#define IR_RECEIVE_PIN D5 // 红外接收头数据引脚
#define IR_RECEIVE_PWR D1 // 红外接收头电源引脚

#define DRV_EEP_PIN D2 // 电机驱动模块使能引脚，高电平启用驱动模块, 低电平驱动模块休眠
#define DRV_IN1_PIN D3 // 电机驱动模块输入1引脚
#define DRV_IN2_PIN D4 // 电机驱动模块输入2引脚

#define ENCODER_A_PIN D6 // 电机霍尔编码器A相引脚
#define ENCODER_B_PIN D7 // 电机霍尔编码器B相引脚
#define ENCODER_PWR D8   // 电机霍尔编码器电源引脚
