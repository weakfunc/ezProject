#include "serial_interface.h"
#include "i2c_interface.h"
#include "voice_protocol.h"
#include "main.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// 全局变量声明
extern uint8_t lastReceivedCmdId;
extern UART_HandleTypeDef huart1;
extern uint8_t uart1_rx_buffer[];
extern uint8_t uart1_rx_index;

// 简单的字符串比较函数
int strcmp_simple(const char* s1, const char* s2) {
    while(*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

// 简单的字符串转整数函数
int atoi_simple(const char* str) {
    int result = 0;
    int sign = 1;
    
    // 处理符号
    if (*str == '-') {
        sign = -1;
        str++;
    }
    
    // 转换数字
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return sign * result;
}

//========================================================================
// 函数: void processSerialCommands()
// 功能: 处理串口接收到的命令并执行相应操作
//========================================================================
void processSerialCommands(void) {
    static char serial_buffer[128];
    static int buffer_index = 0;
    
    // 检查是否有数据从USART1接收
    if (uart1_rx_index > 0) {
        // 处理所有接收到的字符
        for (int i = 0; i < uart1_rx_index; i++) {
            char c = uart1_rx_buffer[i];
            
            // 回车换行符
            if (c == '\r' || c == '\n') {
                if (buffer_index > 0) {
                    serial_buffer[buffer_index] = '\0'; // 字符串终止
                    
                    printf("\r\n接收到的命令: %s\r\n", serial_buffer);
                    
                    // 处理 play 命令
                    if (strncmp(serial_buffer, "play", 4) == 0) {
                        int cmdId = atoi_simple(serial_buffer + 5);
                        executePlayCommand(cmdId);
                    }
                    // 处理其他命令
                    else if (strcmp_simple(serial_buffer, "help") == 0) {
                        printHelp();
                    }
                    else if (strcmp_simple(serial_buffer, "last") == 0) {
                        printLastCommand();
                    }
                    else if (strcmp_simple(serial_buffer, "list") == 0) {
                        printCommandList();
                    }
                    else {
                        printf("未知命令，输入 'help' 查看所有命令\r\n");
                    }
                    
                    buffer_index = 0; // 重置缓冲区
                }
            } 
            // 退格键
            else if (c == '\b' || c == 127) {
                if (buffer_index > 0) {
                    buffer_index--;
                    printf("\b \b"); // 回显退格
                }
            }
            // 普通字符
            else if (buffer_index < sizeof(serial_buffer) - 1) {
                serial_buffer[buffer_index++] = c;
                printf("%c", c); // 回显字符
            }
        }
        
        // 处理完成后重置接收索引
        uart1_rx_index = 0;
    }
}

//========================================================================
// 函数: void printHelp()
// 功能: 打印帮助信息
//========================================================================
void printHelp(void) {
    printf("可用命令:\r\n");
    printf("play <ID> - 执行指定ID的指令\r\n");
    printf("last - 显示最后收到的指令\r\n");
    printf("list - 显示所有可用指令\r\n");
    printf("help - 显示此帮助信息\r\n");
}

//========================================================================
// 函数: void printLastCommand()
// 功能: 显示最后收到的指令
//========================================================================
void printLastCommand(void) {
    if (lastReceivedCmdId > 0) {
        ProtocolResult result = find_command_by_semantic_id(lastReceivedCmdId);
        if (result.success) {
            printf("最后收到的指令: %s (ID: %d)\r\n", result.command, result.cmd_id);
        }
    } else {
        printf("尚未收到任何指令\r\n");
    }
}

//========================================================================
// 函数: void printCommandList()
// 功能: 显示指令列表
//========================================================================
void printCommandList(void) {
    printf("可用指令列表:\r\n");
    for (int i = 0; i < commands_count; i++) {
        printf("ID: %d - %s\r\n", commands[i].cmd_id, commands[i].command);
    }
}

//========================================================================
// 函数: void executePlayCommand(int cmdId)
// 功能: 执行play命令
//========================================================================
void executePlayCommand(int cmdId) {
    if (cmdId > 0) {
        ProtocolResult result = find_command_by_semantic_id(cmdId);
        if (result.success) {
            printf("执行指令: %s\r\n", result.command);
            printf("响应: %s\r\n", result.response);
            
            // 构造写入数据，从1寄存器开始写入5字节指令 AA 55 +类型+ID+FB
            uint8_t write_data[5] = {
                0xAA,                    // AA
                0x55,                    // 55  
                result.tx_data[2],       // 类型，从tx_data中获取
                result.tx_data[3],       // ID，从tx_data中获取
                0xFB                     // FB
            };
            
            printf("I2C写入数据，从1寄存器开始: ");
            for (int i = 0; i < 5; i++) {
                printf("0x%02X", write_data[i]);
                if (i < 4) printf(", ");
            }
            printf("\r\n");
            
            // 使用mem方式从1寄存器开始写入5字节数据
            if (i2c_mem_write(1, write_data, 5) == 0) {
                printf("I2C写入成功\r\n");
            } else {
                printf("I2C写入失败\r\n");
            }
        } else {
            printf("未找到对应指令ID\r\n");
        }
    } else {
        printf("无效指令ID\r\n");
    }
}
