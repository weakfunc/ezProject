package com.example.demo_1

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/** 每个 RX CMD 帧的解析结果 */
data class BleRxFrame(
    val var4b1: Int = 0,
    val var4b2: Int = 0,
    val var1b1: Int = 0,
    val var1b2: Int = 0,
    val cnt:    Int = 0
)

/**
 * BLE 帧协议解析与构建
 *
 * 帧格式（固定 16 字节）：
 * ┌──────┬──────┬──────┬──────────────────────┬──────┬──────┬──────┐
 * │ [0]  │ [1]  │ [2]  │   [3] ~ [12]         │ [13] │ [14] │ [15] │
 * │ 0x55 │ 0xAA │ CMD  │ 4B_1 | 4B_2 | 1B_1 | 1B_2 │ CRC8 │ CNT  │ 0xFF │
 * └──────┴──────┴──────┴──────────────────────┴──────┴──────┴──────┘
 *
 * 数据字段偏移（[3]~[12]，共 10 字节）：
 *   [3]~[6]  : 4byteVar_1（Big-Endian Int32）
 *   [7]~[10] : 4byteVar_2（Big-Endian Int32）
 *   [11]     : 1byteVar_1
 *   [12]     : 1byteVar_2
 *
 * CMD 范围：
 *   0x17~0x20 : ESP32 → App（RX，通过 Notify 推送）
 *   0x21~0x24 : App → ESP32（TX，通过 Write 发送）
 */
object BleProtocol {

    // ── 协议绑定配置 ──────────────────────────────────────────────────────────
    // 绑定目标在 userConfig.kt 中修改 esp32_service_1_uuid / esp32_service_1_characteristic_1_uuid
    private val PROTO_SERVICE_UUID        get() = UserConfig.esp32_service_1_uuid
    private val PROTO_CHARACTERISTIC_UUID get() = UserConfig.esp32_service_1_characteristic_1_uuid
    // ─────────────────────────────────────────────────────────────────────────

    // 帧结构常量
    const val FRAME_SIZE   = 16
    private const val HEADER_1 = 0x55
    private const val HEADER_2 = 0xAA
    private const val TAIL     = 0xFF

    // CMD 范围
    const val CMD_RX_MIN = 0x17
    const val CMD_RX_MAX = 0x20
    const val CMD_TX_MIN = 0x21
    const val CMD_TX_MAX = 0x24

    // ── RX 解析结果：按 CMD 独立存储（Compose State，收到有效 Notify 帧后自动更新）
    // key = CMD 值(0x17~0x20)，value = 该 CMD 最新一帧数据
    val rxFrames = mutableStateMapOf<Int, BleRxFrame>()

    private var txCnt = 0

    // ── 公开 API ──────────────────────────────────────────────────────────────

    /** 判断该 serviceUuid + characteristicUuid 组合是否绑定本协议 */
    fun isBound(serviceUuid: String, characteristicUuid: String): Boolean =
        serviceUuid.trim().equals(PROTO_SERVICE_UUID, ignoreCase = true) &&
        characteristicUuid.trim().equals(PROTO_CHARACTERISTIC_UUID, ignoreCase = true)

    /**
     * 解析 RX 帧并更新状态变量。
     * 校验：包头(0x55/0xAA)、包尾(0xFF)、CMD 范围(0x17~0x20)。
     * @return true 解析成功；false 帧无效
     */
    fun parseRxFrame(data: ByteArray): Boolean {
        if (data.size < FRAME_SIZE) return false
        if ((data[0].toInt() and 0xFF) != HEADER_1) return false
        if ((data[1].toInt() and 0xFF) != HEADER_2) return false
        if ((data[15].toInt() and 0xFF) != TAIL) return false

        val cmd = data[2].toInt() and 0xFF
        if (cmd < CMD_RX_MIN || cmd > CMD_RX_MAX) return false

        rxFrames[cmd] = BleRxFrame(
            var4b1 = parseInt32LE(data, 3),
            var4b2 = parseInt32LE(data, 7),
            var1b1 = data[11].toInt() and 0xFF,
            var1b2 = data[12].toInt() and 0xFF,
            cnt    = (rxFrames[cmd]?.cnt ?: 0) + 1
        )
        return true
    }

    /**
     * 构建 TX 帧（16 字节），CNT 自动递增。
     * cmd 应在 CMD_TX_MIN(0x21)..CMD_TX_MAX(0x24) 范围内。
     */
    fun buildTxFrame(
        cmd: Int,
        var4_1: Int = 0,
        var4_2: Int = 0,
        var1_1: Int = 0,
        var1_2: Int = 0
    ): ByteArray {
        val frame = ByteArray(FRAME_SIZE)
        frame[0]  = HEADER_1.toByte()
        frame[1]  = HEADER_2.toByte()
        frame[2]  = (cmd    and 0xFF).toByte()
        frame[3]  = ((var4_1 ushr 24) and 0xFF).toByte()
        frame[4]  = ((var4_1 ushr 16) and 0xFF).toByte()
        frame[5]  = ((var4_1 ushr  8) and 0xFF).toByte()
        frame[6]  = (var4_1           and 0xFF).toByte()
        frame[7]  = ((var4_2 ushr 24) and 0xFF).toByte()
        frame[8]  = ((var4_2 ushr 16) and 0xFF).toByte()
        frame[9]  = ((var4_2 ushr  8) and 0xFF).toByte()
        frame[10] = (var4_2           and 0xFF).toByte()
        frame[11] = (var1_1 and 0xFF).toByte()
        frame[12] = (var1_2 and 0xFF).toByte()
        frame[13] = 0x00                           // CRC8 预留，当前固定 0x00
        frame[14] = (txCnt  and 0xFF).toByte()
        frame[15] = TAIL.toByte()
        txCnt = (txCnt + 1) and 0xFF
        return frame
    }

    // ── 私有工具 ──────────────────────────────────────────────────────────────

    private fun parseInt32LE(bytes: ByteArray, offset: Int): Int =
         (bytes[offset    ].toInt() and 0xFF)        or
        ((bytes[offset + 1].toInt() and 0xFF) shl  8) or
        ((bytes[offset + 2].toInt() and 0xFF) shl 16) or
        ((bytes[offset + 3].toInt() and 0xFF) shl 24)
}
