#include <REGX52.H>

// `▶|`表示LED，尾部是正极，头部是负极
// 查看原理图可知LED模块连接单片机核心的P20-P27接口(P2寄存器)，并且正极已经是高电平
// 当负极为低电平时LED即可点亮
void main()
{
  // 当需要点亮第一个LED时，P2寄存器二进制为1111 1110即0xFE
  P2 = 0xFE;
  // 由于单片机执行完所有程序后会重头重复执行，所以加上while死循环就可防止重复执行
  while (1)
  {
  }
}
