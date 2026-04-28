#ifndef VOICE_PROTOCOL_H
#define VOICE_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include "voice_commands.h"  // 包含指令表定义

// 协议处理结果
typedef struct {
    uint8_t success;
    uint8_t cmd_id;
    const char *command;
    const char *response;
    uint8_t tx_data[5];
    uint8_t rx_data[5];
} ProtocolResult;

// 函数声明
ProtocolResult find_command_by_type_and_id(uint8_t cmd_type, uint8_t cmd_id);
ProtocolResult find_command_by_semantic_id(uint8_t semantic_id);
ProtocolResult find_command_by_text(const char *command_text);
uint8_t parse_protocol_data(uint8_t *data, size_t len, uint8_t *cmd_type);

#endif
