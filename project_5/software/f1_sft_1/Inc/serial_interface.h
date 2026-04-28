#ifndef SERIAL_INTERFACE_H
#define SERIAL_INTERFACE_H

#include "voice_protocol.h"
#include <stdint.h>

// 串口命令处理函数
void processSerialCommands(void);
void printHelp(void);
void printLastCommand(void);
void printCommandList(void);
void executePlayCommand(int cmdId);

// 全局变量声明
extern uint8_t lastReceivedCmdId;

#endif
