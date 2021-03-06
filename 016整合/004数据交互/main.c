#include <REGX52.H>
#include "UART.H"
#include "TIMER0.H"
#include "DS18B20.H"
#include "LCD1602.H"
#include "OneWire.H"
#include "DELAY.H"
#include "Key.H"
#include "AT24C02.h"

// 接收数据数组长度
#define UART_RECEIVE_SIZE 8
// 接收数据数组
unsigned char UART_RECEIVE_DATA[UART_RECEIVE_SIZE];
// 下一个接收数据存放下标
unsigned char UART_RECEIVE_INDEX = 0;
// 接收状态：0未接收；1接收中；2被定时器中断函数打断；
// 3接收完成(数据要在UART_RECEIVE_SIZE毫秒内发送完成，否则无效)
unsigned char UART_RECEIVE_STATUS = 0;

// 温度最大值(实际为125)
char TEMP_MAX = 99;
// 温度最小值(实际为-55)
char TEMP_MIN = -55;
// 温度转换间隔最大值(秒)
unsigned char TEMP_CONVERT_MAX = 60;
// 温度转换间隔最小值(秒)
unsigned char TEMP_CONVERT_MIN = 1;
// 温度转换间隔(秒)
unsigned char TEMP_CONVERT_TIME = 1;
// 温度串口发送最大值(秒)
unsigned char TEMP_SEND_MAX = 60;
// 温度串口发送最小值(秒)
unsigned char TEMP_SEND_MIN = 1;
// 温度串口发送间隔(秒)
unsigned char TEMP_SEND_TIME = 10;
// 温度转换状态：0不可转换；1可转换
unsigned char TEMP_CONVERT_STATUS = 1;
// 温度串口发送状态：0不可发送；1可发送
unsigned char TEMP_SEND_STATUS = 0;
// 温度*10000
long Temp;
// 温度整数部分(char)(Temp / 10000)
char TempInt;
// 高温报警
char TEMP_HIGH = 30;
// 低温报警
char TEMP_LOW = 20;
// 温度报警状态：0正常；1温度过高；2温度过低
unsigned char TEMP_ALARM_STATUS;

// 温度设置刷新状态：0不可刷新；1可刷新
unsigned char TEMP_SET_REFRESH_STATUS = 0;
// 温度设置模式：0关闭；1开启
unsigned char TEMP_SET_MODE = 0;
// 温度设置选项：0高温报警；1低温报警；2串口发送时间间隔；3温度读取时间间隔
char TEMP_SET_SELECT = -1;
// 温度设置闪烁状态：0亮；1灭
unsigned char TEMP_SET_FLASH_STATUS = 0;
// 温度设置调节按键序号：1切换调节；2数值+1；3数值-1；4保存调节
unsigned char KeyNum;

void main()
{
  // 存储器里面的温度设置
  char THigh, TLow, TSend, TConvert;
  Timer0_Init();
  UartInit();
  LCD_Init();
  // Temp:+123.4567℃
  //\xDF是℃左上角圆圈，\x43代表字母C
  LCD_ShowString(1, 1, "Temp:    .    \xDF\x43");
  // H+12L-34E10R01 H
  // H高温报警 L低温报警 C串口发送时间间隔 F温度读取时间间隔
  // 最后一个H代表温度过高，L代表温度过低，不显示代表温度正常
  LCD_ShowString(2, 1, "H   L   C  F");
  // 读取存储器里面的温度设置
  THigh = AT24C02_ReadByte(0);
  TLow = AT24C02_ReadByte(1);
  TSend = AT24C02_ReadByte(2);
  TConvert = AT24C02_ReadByte(3);
  // 存储器里的数据正确，才保存
  if (THigh <= TEMP_MAX && TLow >= TEMP_MIN && THigh > TLow                 //
      && TSend > TEMP_SEND_MIN - 1 && TSend < TEMP_SEND_MAX + 1             //
      && TConvert > TEMP_CONVERT_MIN - 1 && TConvert < TEMP_CONVERT_MAX + 1 //
      && TSend >= TConvert)
  {
    TEMP_HIGH = THigh;
    TEMP_LOW = TLow;
    TEMP_SEND_TIME = TSend;
    TEMP_CONVERT_TIME = TConvert;
  }
  // 上电先转换一次温度，防止第一次读数据错误
  DS18B20_ConvertT();
  // 等待转换完成
  delayMs(500);
  while (1)
  {
    // 数据交互格式：
    // 电脑-->单片机-->电脑：0x01-0x9F
    //  设置：0x0101-0x01FE
    //    ■设置温度配置：0x0101+(4x8bit) H高温报警 L低温报警 C串口发送时间间隔 F温度读取时间间隔
    //      返回：0x010100失败；0x010101成功
    //  读取：0x0201-0x02FE
    //    ■读取温度：0x0201
    //      返回：0x0201+(4x8bit) 温度x10000
    //    ■读取温度配置：0x0202
    //      返回：0x0202+(4x8bit) H高温报警 L低温报警 C串口发送时间间隔 F温度读取时间间隔
    // 单片机-->电脑：0xA0-0xFE
    //  自动发送：0xA001-0xA0FE
    //    ■自动发送温度：0xA001+(4x8bit) 温度x10000
    //  报警：0xA101-0xA1FE
    //    ■温度过高报警：0xA101+(4x8bit) 温度x10000
    //    ■温度过低报警：0xA102+(4x8bit) 温度x10000
    /* 数据接收完成 */
    if (UART_RECEIVE_STATUS == 3)
    {
      // 设置：0x0101-0x01FE
      if (UART_RECEIVE_DATA[0] == 0x01)
      {
        // 设置温度配置：0x0101+(4x8bit) H高温报警 L低温报警 C串口发送时间间隔 F温度读取时间间隔
        if (UART_RECEIVE_DATA[1] == 0x01)
        {
          // 读取数据
          THigh = UART_RECEIVE_DATA[2];
          TLow = UART_RECEIVE_DATA[3];
          TSend = UART_RECEIVE_DATA[4];
          TConvert = UART_RECEIVE_DATA[5];
          // 数据正确
          if (THigh <= TEMP_MAX && TLow >= TEMP_MIN && THigh > TLow                 //
              && TSend > TEMP_SEND_MIN - 1 && TSend < TEMP_SEND_MAX + 1             //
              && TConvert > TEMP_CONVERT_MIN - 1 && TConvert < TEMP_CONVERT_MAX + 1 //
              && TSend >= TConvert)
          {
            TEMP_HIGH = THigh;
            TEMP_LOW = TLow;
            TEMP_SEND_TIME = TSend;
            TEMP_CONVERT_TIME = TConvert;
            // 写入数据到存储器
            AT24C02_WriteByte(0, TEMP_HIGH);
            delayMs(5);
            AT24C02_WriteByte(1, TEMP_LOW);
            delayMs(5);
            AT24C02_WriteByte(2, TEMP_SEND_TIME);
            delayMs(5);
            AT24C02_WriteByte(3, TEMP_CONVERT_TIME);
            // 0x010101成功
            UartSendString("\x01\x01\x01\x01");
          }
          // 数据错误
          else
          {
            // 0x010100失败
            UartSendString("\x01\x01\x01\x00");
          }
        }
      }
      // 读取：0x0201-0x02FE
      else if (UART_RECEIVE_DATA[0] == 0x02)
      {
        // 读取温度：0x0201
        if (UART_RECEIVE_DATA[1] == 0x01)
        {
          // 0x0201+(4x8bit) 温度x10000
          UartSendString("\x02\x01");
          UartSendLong(Temp);
        }
        // 读取温度配置：0x0202
        else if (UART_RECEIVE_DATA[1] == 0x02)
        {
          // 0x0202+(4x8bit) H高温报警 L低温报警 C串口发送时间间隔 F温度读取时间间隔
          UartSendString("\x02\x02");
          UartSendByte(TEMP_HIGH);
          UartSendByte(TEMP_LOW);
          UartSendByte(TEMP_SEND_TIME);
          UartSendByte(TEMP_CONVERT_TIME);
        }
      }
      // 重置状态和下标
      UART_RECEIVE_INDEX = 0;
      UART_RECEIVE_STATUS = 0;
    }
    /* 温度串口发送时间到 */
    if (TEMP_SEND_STATUS == 1)
    {
      // 发送温度：0xA001+(4x8bit) 温度x10000
      UartSendString("\xA0\x01");
      UartSendLong(Temp);
      TEMP_SEND_STATUS = 0;
    }
    /* 温度转换时间到 */
    if (TEMP_CONVERT_STATUS == 1)
    {
      // 温度转换
      DS18B20_ConvertT();
      Temp = DS18B20_ReadT();
      // 显示温度
      TempInt = (char)(Temp / 10000);
      LCD_ShowSignedNum(1, 6, TempInt, 3);                 // 整数部分
      LCD_ShowNum(1, 11, (unsigned int)(Temp % 10000), 4); // 小数部分
      // 显示高温还是低温
      if (TempInt >= TEMP_HIGH)
      {
        LCD_ShowChar(2, 16, 'H');
        if (TEMP_ALARM_STATUS != 1)
        {
          // 温度过高报警：0xA101+(4x8bit) 温度x10000
          UartSendString("\xA1\x01");
          UartSendLong(Temp);
          TEMP_ALARM_STATUS = 1;
        }
      }
      else if (TempInt < TEMP_LOW)
      {
        LCD_ShowChar(2, 16, 'L');
        if (TEMP_ALARM_STATUS != 2)
        {
          // 温度过低报警：0xA102+(4x8bit) 温度x10000
          UartSendString("\xA1\x02");
          UartSendLong(Temp);
          TEMP_ALARM_STATUS = 2;
        }
      }
      else
      {
        TEMP_ALARM_STATUS = 0;
        LCD_ShowChar(2, 16, ' ');
      }
      TEMP_CONVERT_STATUS = 0;
    }
    /* 温度设置刷新时间到 */
    if (TEMP_SET_MODE == 0 && TEMP_SET_REFRESH_STATUS == 1)
    {
      // H+12L-34C10F01 H
      LCD_ShowSignedNum(2, 2, TEMP_HIGH, 2);
      LCD_ShowSignedNum(2, 6, TEMP_LOW, 2);
      LCD_ShowNum(2, 10, TEMP_SEND_TIME, 2);
      LCD_ShowNum(2, 13, TEMP_CONVERT_TIME, 2);
      TEMP_SET_REFRESH_STATUS = 0;
    }
    /* 温度设置按键被按下 */
    // P3_1和P3_0对应按键1和2被串口通信占用，串口有数据通过时，可以会有干扰
    KeyNum = Key();
    if (KeyNum != 0)
    {
      // 按键1：温度设置调节按键序号：1切换调节；2数值+1；3数值-1；4保存调节
      if (KeyNum == 1)
      {
        TEMP_SET_MODE = 1;
        // 温度设置选项：0高温报警；1低温报警；2串口发送时间间隔；3温度读取时间间隔
        TEMP_SET_SELECT = (TEMP_SET_SELECT + 1) % 4;
      }
      // 当为设置模式时，2,3,4按键才有效
      if (TEMP_SET_MODE == 1)
      {
        // 按键2：+1
        if (KeyNum == 2)
        {
          // 高温报警
          if (TEMP_SET_SELECT == 0)
          {
            if (TEMP_HIGH < TEMP_MAX)
            {
              TEMP_HIGH++;
            }
          }
          // 低温报警
          else if (TEMP_SET_SELECT == 1)
          {
            if (TEMP_LOW < TEMP_HIGH - 1)
            {
              TEMP_LOW++;
            }
          }
          // 串口发送时间间隔
          else if (TEMP_SET_SELECT == 2)
          {
            if (TEMP_SEND_TIME < TEMP_SEND_MAX)
            {
              TEMP_SEND_TIME++;
            }
          }
          // 温度读取时间间隔
          else
          {
            if (TEMP_CONVERT_TIME < TEMP_CONVERT_MAX && TEMP_SEND_TIME > TEMP_CONVERT_TIME)
            {
              TEMP_CONVERT_TIME++;
            }
          }
        }
        // 按键3：-1
        else if (KeyNum == 3)
        {
          // 高温报警
          if (TEMP_SET_SELECT == 0)
          {
            if (TEMP_HIGH > TEMP_LOW + 1)
            {
              TEMP_HIGH--;
            }
          }
          // 低温报警
          else if (TEMP_SET_SELECT == 1)
          {
            if (TEMP_LOW > TEMP_MIN)
            {
              TEMP_LOW--;
            }
          }
          // 串口发送时间间隔
          else if (TEMP_SET_SELECT == 2)
          {
            if (TEMP_SEND_TIME > TEMP_SEND_MIN && TEMP_SEND_TIME > TEMP_CONVERT_TIME)
            {
              TEMP_SEND_TIME--;
            }
          }
          // 温度读取时间间隔
          else
          {
            if (TEMP_CONVERT_TIME > TEMP_CONVERT_MIN)
            {
              TEMP_CONVERT_TIME--;
            }
          }
        }
        // 按键4：保存
        else if (KeyNum == 4)
        {
          TEMP_SET_MODE = 0;
          TEMP_SET_SELECT = -1;
          // 写入数据到存储器
          AT24C02_WriteByte(0, TEMP_HIGH);
          delayMs(5);
          AT24C02_WriteByte(1, TEMP_LOW);
          delayMs(5);
          AT24C02_WriteByte(2, TEMP_SEND_TIME);
          delayMs(5);
          AT24C02_WriteByte(3, TEMP_CONVERT_TIME);
        }
      }
    }
    /* 温度设置的时候闪烁 */
    if (TEMP_SET_MODE == 1)
    {
      // H+12L-34C10F01 H
      // 亮
      if (TEMP_SET_FLASH_STATUS == 0)
      {
        if (TEMP_SET_SELECT == 0)
        {
          LCD_ShowSignedNum(2, 2, TEMP_HIGH, 2);
        }
        else if (TEMP_SET_SELECT == 1)
        {
          LCD_ShowSignedNum(2, 6, TEMP_LOW, 2);
        }
        else if (TEMP_SET_SELECT == 2)
        {
          LCD_ShowNum(2, 10, TEMP_SEND_TIME, 2);
        }
        else
        {
          LCD_ShowNum(2, 13, TEMP_CONVERT_TIME, 2);
        }
      }
      // 灭
      else
      {
        if (TEMP_SET_SELECT == 0)
        {
          LCD_ShowString(2, 2, "   ");
          LCD_ShowSignedNum(2, 6, TEMP_LOW, 2);
          LCD_ShowNum(2, 10, TEMP_SEND_TIME, 2);
          LCD_ShowNum(2, 13, TEMP_CONVERT_TIME, 2);
        }
        else if (TEMP_SET_SELECT == 1)
        {
          LCD_ShowString(2, 6, "   ");
          LCD_ShowSignedNum(2, 2, TEMP_HIGH, 2);
          LCD_ShowNum(2, 10, TEMP_SEND_TIME, 2);
          LCD_ShowNum(2, 13, TEMP_CONVERT_TIME, 2);
        }
        else if (TEMP_SET_SELECT == 2)
        {
          LCD_ShowString(2, 10, "  ");
          LCD_ShowSignedNum(2, 2, TEMP_HIGH, 2);
          LCD_ShowSignedNum(2, 6, TEMP_LOW, 2);
          LCD_ShowNum(2, 13, TEMP_CONVERT_TIME, 2);
        }
        else
        {
          LCD_ShowString(2, 13, "  ");
          LCD_ShowSignedNum(2, 2, TEMP_HIGH, 2);
          LCD_ShowSignedNum(2, 6, TEMP_LOW, 2);
          LCD_ShowNum(2, 10, TEMP_SEND_TIME, 2);
        }
      }
    }
  }
}

/**
* 串口中断
*/
void UART_Routine() interrupt 4
{
  /* 接收中断 */
  if (RI == 1)
  {
    // 放满了，重头开始
    if (UART_RECEIVE_INDEX == UART_RECEIVE_SIZE)
    {
      // 重置状态和下标
      UART_RECEIVE_INDEX = 0;
      UART_RECEIVE_STATUS = 0;
    }
    // 放入数据
    UART_RECEIVE_DATA[UART_RECEIVE_INDEX++] = SBUF;
    // 接收中
    UART_RECEIVE_STATUS = 1;
    // 重置
    RI = 0;
  }
}

/**
* 定时器0中断回调函数(每1ms)
*/
void Timer0_Routine() interrupt 1
{
  // 静态，防止被销毁
  static unsigned int UART_Count, Key_Count, //
      Temp_Convert_Count, Temp_Send_Count,   //
      Temp_Set_Refresh_Count, Temp_Set_Flash_Count;
  // 复位
  TL0 = 0x66;
  TH0 = 0xFC;
  /* 串口接收 每隔UART_RECEIVE_SIZE毫秒 */
  if ((++UART_Count) >= UART_RECEIVE_SIZE)
  {
    UART_Count = 0;
    // UART_RECEIVE被定时器中断函数打断
    if (UART_RECEIVE_STATUS == 1)
    {
      // 接收中，置为打断
      UART_RECEIVE_STATUS = 2;
    }
    else if (UART_RECEIVE_STATUS == 2)
    {
      // 打断，置为接收完成
      UART_RECEIVE_STATUS = 3;
    }
  }
  /* 按键驱动 每隔20毫秒 */
  if ((++Key_Count) >= 20)
  {
    Key_Count = 0;
    Key_Loop();
  }
  /* 温度转换 每隔TEMP_SEND_TIME秒 */
  if ((++Temp_Convert_Count) >= 1000 * TEMP_CONVERT_TIME)
  {
    Temp_Convert_Count = 0;
    TEMP_CONVERT_STATUS = 1;
  }
  /* 温度串口发送 每隔TEMP_SEND_TIME秒 */
  if ((++Temp_Send_Count) >= 1000 * TEMP_SEND_TIME)
  {
    Temp_Send_Count = 0;
    TEMP_SEND_STATUS = 1;
  }
  /* 温度设置刷新 每隔100毫秒 */
  if ((++Temp_Set_Refresh_Count) >= 100)
  {
    Temp_Set_Refresh_Count = 0;
    TEMP_SET_REFRESH_STATUS = 1;
  }
  /* 温度设置闪烁 每隔500毫秒 */
  if ((++Temp_Set_Flash_Count) >= 500)
  {
    Temp_Set_Flash_Count = 0;
    TEMP_SET_FLASH_STATUS = !TEMP_SET_FLASH_STATUS;
  }
}
