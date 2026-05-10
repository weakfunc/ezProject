package com.example.demo_1

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

data class KnownBleServiceUuidConfig(
    val serviceUuid: String,
    val characteristicUuids: List<String>
)

object UserConfig {
    // Developer mode macro: can only be changed in code.
    const val DEVELOPER_MODE = false

    // === 修改：项目名称和作者 ===
    const val PROJECT_TITLE = "宠物智能饲喂系统设计与实现"
    const val PROJECT_AUTHOR = "韦孙麟"
    // === END ===

    var project_name by mutableStateOf("宠物智能饲喂系统设计与实现")
    var author_name by mutableStateOf("韦孙麟")
    var ble_scan_page_title by mutableStateOf("BLE 搜索")
    var settings_page_title by mutableStateOf("设置")
    var esp32_device_name by mutableStateOf("ESP32C3_FINDME")
    var esp32_device_mac by mutableStateOf<String?>("24:EC:4A:10:74:DA")
    var auto_filter_my_ble_devices by mutableStateOf(true)
    var auto_connect_my_ble_device by mutableStateOf(true)
    var auto_config_ble_device by mutableStateOf(true)
    var mcu_time by mutableStateOf(0L)
    var service_1_characteristic_1_flags by mutableStateOf(ByteArray(12))
    var rx_data_stream_1 by mutableStateOf(IntArray(4))
    var rx_data_stream_2 by mutableStateOf(IntArray(4))
    var rx_data_stream_3 by mutableStateOf(IntArray(4))

    // TX data stream interfaces (app -> ESP32), reserved for follow-up write logic.
    var tx_data_stream_1 by mutableStateOf(ByteArray(16))
    var tx_data_stream_2 by mutableStateOf(IntArray(4))
    var tx_data_stream_3 by mutableStateOf(IntArray(4))
    var tx_data_stream_4 by mutableStateOf(IntArray(4))

    // ESP32 known BLE UUIDs. Update these when firmware UUIDs change.

    var esp32_service_1_uuid by mutableStateOf("EE260001-EE26-EE26-EE26-EE26EE26EE26")
    var esp32_service_1_characteristic_1_uuid by mutableStateOf("EE260101-EE26-EE26-EE26-EE26EE26EE26")

    var esp32_service_2_uuid by mutableStateOf<String?>(null)
    var esp32_service_2_characteristic_1_uuid by mutableStateOf<String?>(null)
    var esp32_service_2_characteristic_2_uuid by mutableStateOf<String?>(null)
    var esp32_service_2_characteristic_3_uuid by mutableStateOf<String?>(null)

    val esp32_known_service_configs: List<KnownBleServiceUuidConfig>
        get() = listOfNotNull(
            KnownBleServiceUuidConfig(
                serviceUuid = esp32_service_1_uuid,
                characteristicUuids = listOf(esp32_service_1_characteristic_1_uuid)
                    .map { it.trim() }
                    .filter { it.isNotEmpty() }
            ),
            esp32_service_2_uuid?.let { svcUuid ->
                KnownBleServiceUuidConfig(
                    serviceUuid = svcUuid,
                    characteristicUuids = listOf(
                        esp32_service_2_characteristic_1_uuid,
                        esp32_service_2_characteristic_2_uuid,
                        esp32_service_2_characteristic_3_uuid
                    )
                        .mapNotNull { it?.trim() }
                        .filter { it.isNotEmpty() }
                )
            }
        )
}
