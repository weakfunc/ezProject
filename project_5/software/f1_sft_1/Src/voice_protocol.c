#include "voice_protocol.h"
#include "voice_commands.h"
#include <string.h>

ProtocolResult find_command_by_type_and_id(uint8_t cmd_type, uint8_t cmd_id) {
    ProtocolResult result = {0};
    
    for (int i = 0; i < commands_count; i++) {
        // 匹配指令类型和指令ID
        if (commands[i].tx_data[2] == cmd_type && commands[i].tx_data[3] == cmd_id) {
            result.success = 1;
            result.cmd_id = commands[i].cmd_id;
            result.command = commands[i].command;
            result.response = commands[i].response;
            memcpy(result.tx_data, commands[i].tx_data, 5);
            memcpy(result.rx_data, commands[i].rx_data, 5);
            break;
        }
    }
    
    return result;
}

ProtocolResult find_command_by_semantic_id(uint8_t semantic_id) {
    ProtocolResult result = {0};
    
    for (int i = 0; i < commands_count; i++) {
        // 匹配语义标签ID
        if (commands[i].cmd_id == semantic_id) {
            result.success = 1;
            result.cmd_id = commands[i].cmd_id;
            result.command = commands[i].command;
            result.response = commands[i].response;
            memcpy(result.tx_data, commands[i].tx_data, 5);
            memcpy(result.rx_data, commands[i].rx_data, 5);
            break;
        }
    }
    
    return result;
}

ProtocolResult find_command_by_text(const char *command_text) {
    ProtocolResult result = {0};
    
    for (int i = 0; i < commands_count; i++) {
        if (strcmp(command_text, commands[i].command) == 0) {
            result.success = 1;
            result.cmd_id = commands[i].cmd_id;
            result.command = commands[i].command;
            result.response = commands[i].response;
            memcpy(result.tx_data, commands[i].tx_data, 5);
            memcpy(result.rx_data, commands[i].rx_data, 5);
            break;
        }
    }
    
    return result;
}

uint8_t parse_protocol_data(uint8_t *data, size_t len, uint8_t *cmd_type) {
    if (len < 5) {
        return 0xFF;  // 失败返回0xFF
    }
    
    if (data[0] == 0xAA && data[1] == 0x55 && data[len - 1] == 0xFB) {
        *cmd_type = data[2];  // 提取指令类型
        return data[3];       // 提取指令ID
    } else if (data[0] == 0xA5 && data[len - 1] == 0xFB) {
        if (len >= 8) {
            *cmd_type = data[2];  // 提取指令类型
            return data[4];       // 提取指令ID
        }
    }
    
    return 0xFF;  // 失败返回0xFF
}
