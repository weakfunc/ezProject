## 框架PROMPT更新说明

1. APPCOM中STM32 RX端的[2]，固定为APP控制系统是否使能标志位。同时，remoteInfo.remoteVar_RX[0]~remoteInfo.remoteVar_RX[3]均为系统配置预留字段。当前APPCOM架构，remoteInfo.remoteVar_TX，RX[0]~[4]均为系统配置预留，不可转发数据
2. 