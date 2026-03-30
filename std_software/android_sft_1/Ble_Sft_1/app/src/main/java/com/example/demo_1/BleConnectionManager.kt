package com.example.demo_1

import android.annotation.SuppressLint
import android.util.Log
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothProfile
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import java.util.Locale
import java.util.UUID

enum class BleDeviceConnectionStatus {
    Disconnected,
    Connecting,
    Connected
}

enum class BleSubscriptionToggleResult {
    Enabled,
    Disabled,
    Failed
}

data class ConnectedBleDeviceInfo(
    val name: String,
    val address: String,
    val rssi: Int
)

data class BleGattCharacteristicUi(
    val serviceUuid: String,
    val uuid: String,
    val name: String,
    val properties: Int,
    val canRead: Boolean,
    val canWrite: Boolean,
    val canSubscribe: Boolean,
    val isSubscribed: Boolean,
    val lastReadValueHex: String?
)

data class BleGattServiceUi(
    val uuid: String,
    val name: String,
    val characteristics: List<BleGattCharacteristicUi>
)

data class BleTerminalEntryUi(
    val id: Long,
    val direction: String,
    val characteristicUuid: String,
    val payloadText: String
)

object BleConnectionManager {
    private const val RSSI_REFRESH_INTERVAL_MS = 1500L
    private const val MAX_TERMINAL_ENTRIES = 200
    private val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    private const val AUTO_SUBSCRIBE_RETRY_DELAY_MS = 300L
    private const val AUTO_SUBSCRIBE_MAX_RETRY = 6

    private val mainHandler = Handler(Looper.getMainLooper())
    private var currentGatt: BluetoothGatt? = null
    private var currentAddress: String? = null
    private var rssiPollingRunnable: Runnable? = null
    private var nextTerminalEntryId = 0L

    private val characteristicSubscriptionStates = mutableMapOf<String, Boolean>()
    private val characteristicLastReadValues = mutableMapOf<String, String>()

    // 自动订阅状态（连接时自动启用 Notify/Indicate）
    private val autoSubscribedKeys   = mutableSetOf<String>()
    private val autoSubscribeRetries = mutableMapOf<String, Int>()
    private val autoSubscribeSkipped = mutableSetOf<String>()
    private val autoSubscribeRetryRunnable = Runnable { triggerAutoSubscribe() }

    val deviceStates = mutableStateMapOf<String, BleDeviceConnectionStatus>()

    var connectedDeviceInfo by mutableStateOf<ConnectedBleDeviceInfo?>(null)
        private set

    var discoveredServices by mutableStateOf<List<BleGattServiceUi>>(emptyList())
        private set

    val terminalEntries = mutableStateListOf<BleTerminalEntryUi>()

    fun getDeviceState(address: String): BleDeviceConnectionStatus {
        return deviceStates[address] ?: BleDeviceConnectionStatus.Disconnected
    }

    @SuppressLint("MissingPermission")
    fun connect(
        context: Context,
        device: BluetoothDevice,
        deviceName: String,
        rssi: Int
    ) {
        val address = device.address
        when (getDeviceState(address)) {
            BleDeviceConnectionStatus.Connected,
            BleDeviceConnectionStatus.Connecting -> return
            BleDeviceConnectionStatus.Disconnected -> Unit
        }

        disconnectCurrentInternal()

        currentAddress = address
        deviceStates[address] = BleDeviceConnectionStatus.Connecting
        connectedDeviceInfo = null
        discoveredServices = emptyList()
        clearTerminalEntries()

        val callback = object : BluetoothGattCallback() {
            override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
                mainHandler.post {
                    val isConnected =
                        status == BluetoothGatt.GATT_SUCCESS && newState == BluetoothProfile.STATE_CONNECTED
                    if (isConnected) {
                        currentGatt = gatt
                        currentAddress = address
                        deviceStates[address] = BleDeviceConnectionStatus.Connected
                        connectedDeviceInfo = ConnectedBleDeviceInfo(
                            name = deviceName,
                            address = address,
                            rssi = rssi
                        )
                        val discoverStarted = try {
                            gatt.discoverServices()
                        } catch (_: SecurityException) {
                            false
                        }
                        if (!discoverStarted) {
                            refreshDiscoveredServices(gatt)
                        }
                        startRssiPolling()
                        return@post
                    }
                    handleDisconnected(address = address, callbackGatt = gatt)
                }
            }

            override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
                mainHandler.post {
                    if (status == BluetoothGatt.GATT_SUCCESS) {
                        refreshDiscoveredServices(gatt)
                        triggerAutoSubscribe()
                    } else {
                        discoveredServices = emptyList()
                    }
                }
            }

            override fun onReadRemoteRssi(gatt: BluetoothGatt, rssi: Int, status: Int) {
                mainHandler.post {
                    if (
                        status == BluetoothGatt.GATT_SUCCESS &&
                        connectedDeviceInfo?.address == currentAddress &&
                        connectedDeviceInfo?.address == gatt.device.address
                    ) {
                        connectedDeviceInfo = connectedDeviceInfo?.copy(rssi = rssi)
                    }
                }
            }

            @Deprecated("Deprecated in API 33")
            override fun onCharacteristicRead(
                gatt: BluetoothGatt,
                characteristic: BluetoothGattCharacteristic,
                status: Int
            ) {
                mainHandler.post {
                    if (status == BluetoothGatt.GATT_SUCCESS) {
                        updateCharacteristicValue(
                            characteristic = characteristic,
                            value = characteristic.value,
                            fromNotify = false
                        )
                    }
                }
            }

            override fun onCharacteristicRead(
                gatt: BluetoothGatt,
                characteristic: BluetoothGattCharacteristic,
                value: ByteArray,
                status: Int
            ) {
                mainHandler.post {
                    if (status == BluetoothGatt.GATT_SUCCESS) {
                        updateCharacteristicValue(
                            characteristic = characteristic,
                            value = value,
                            fromNotify = false
                        )
                    }
                }
            }

            @Deprecated("Deprecated in API 33")
            override fun onCharacteristicChanged(
                gatt: BluetoothGatt,
                characteristic: BluetoothGattCharacteristic
            ) {
                mainHandler.post {
                    updateCharacteristicValue(
                        characteristic = characteristic,
                        value = characteristic.value,
                        fromNotify = true
                    )
                }
            }

            override fun onCharacteristicChanged(
                gatt: BluetoothGatt,
                characteristic: BluetoothGattCharacteristic,
                value: ByteArray
            ) {
                mainHandler.post {
                    updateCharacteristicValue(
                        characteristic = characteristic,
                        value = value,
                        fromNotify = true
                    )
                }
            }

            override fun onCharacteristicWrite(
                gatt: BluetoothGatt,
                characteristic: BluetoothGattCharacteristic,
                status: Int
            ) {
                Log.d("BLE", "onCharacteristicWrite status=$status")
                mainHandler.post {
                    startRssiPolling()
                }
            }
        }

        val gatt = device.connectGatt(context.applicationContext, false, callback)
        if (gatt == null) {
            deviceStates[address] = BleDeviceConnectionStatus.Disconnected
            if (currentAddress == address) {
                currentAddress = null
            }
        } else {
            currentGatt = gatt
        }
    }

    @SuppressLint("MissingPermission")
    fun readCharacteristic(
        serviceUuid: String,
        characteristicUuid: String
    ): Boolean {
        val gatt = currentGatt ?: return false
        val characteristic = findCharacteristic(serviceUuid, characteristicUuid) ?: return false
        if (!characteristicCanRead(characteristic.properties)) return false
        return try {
            gatt.readCharacteristic(characteristic)
        } catch (_: SecurityException) {
            false
        }
    }

    fun canWriteCharacteristic(
        serviceUuid: String,
        characteristicUuid: String
    ): Boolean {
        val characteristic = findCharacteristic(serviceUuid, characteristicUuid) ?: return false
        return characteristicCanWrite(characteristic.properties)
    }

    @SuppressLint("MissingPermission")
    fun writeCharacteristic(
        serviceUuid: String,
        characteristicUuid: String,
        value: ByteArray
    ): Boolean {
        val gatt = currentGatt ?: return false
        val characteristic = findCharacteristic(serviceUuid, characteristicUuid) ?: return false
        if (!characteristicCanWrite(characteristic.properties)) return false
        stopRssiPolling()
        return writeCharacteristicCompat(gatt, characteristic, value)
    }

    @SuppressLint("MissingPermission")
    fun toggleCharacteristicSubscription(
        serviceUuid: String,
        characteristicUuid: String
    ): BleSubscriptionToggleResult {
        val gatt = currentGatt ?: return BleSubscriptionToggleResult.Failed
        val characteristic = findCharacteristic(serviceUuid, characteristicUuid) ?: return BleSubscriptionToggleResult.Failed
        if (!characteristicCanSubscribe(characteristic.properties)) {
            return BleSubscriptionToggleResult.Failed
        }

        val key = characteristicKey(serviceUuid, characteristicUuid)
        val enable = !(characteristicSubscriptionStates[key] ?: false)
        val notificationSet = try {
            gatt.setCharacteristicNotification(characteristic, enable)
        } catch (_: SecurityException) {
            false
        }
        if (!notificationSet) {
            return BleSubscriptionToggleResult.Failed
        }

        val cccd = characteristic.getDescriptor(CCCD_UUID)
        if (cccd == null) {
            characteristicSubscriptionStates[key] = enable
            refreshDiscoveredServices(gatt)
            return if (enable) {
                BleSubscriptionToggleResult.Enabled
            } else {
                BleSubscriptionToggleResult.Disabled
            }
        }

        val descriptorValue = when {
            !enable -> BluetoothGattDescriptor.DISABLE_NOTIFICATION_VALUE
            characteristicHasProperty(characteristic.properties, BluetoothGattCharacteristic.PROPERTY_INDICATE) &&
                !characteristicHasProperty(characteristic.properties, BluetoothGattCharacteristic.PROPERTY_NOTIFY) ->
                BluetoothGattDescriptor.ENABLE_INDICATION_VALUE
            else -> BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
        }

        val writeQueued = writeDescriptorCompat(gatt, cccd, descriptorValue)
        if (!writeQueued) {
            try {
                gatt.setCharacteristicNotification(characteristic, !enable)
            } catch (_: SecurityException) {
            }
            return BleSubscriptionToggleResult.Failed
        }

        characteristicSubscriptionStates[key] = enable
        refreshDiscoveredServices(gatt)
        return if (enable) {
            BleSubscriptionToggleResult.Enabled
        } else {
            BleSubscriptionToggleResult.Disabled
        }
    }

    @SuppressLint("MissingPermission")
    fun disconnectCurrent() {
        disconnectCurrentInternal()
    }

    @SuppressLint("MissingPermission")
    private fun disconnectCurrentInternal() {
        stopRssiPolling()
        clearAutoSubscribeState()
        val address = currentAddress
        if (address != null) {
            deviceStates[address] = BleDeviceConnectionStatus.Disconnected
        }
        currentAddress = null
        connectedDeviceInfo = null
        discoveredServices = emptyList()
        characteristicSubscriptionStates.clear()
        characteristicLastReadValues.clear()
        clearTerminalEntries()

        currentGatt?.let { gatt ->
            try {
                gatt.disconnect()
            } catch (_: SecurityException) {
            } finally {
                gatt.close()
            }
        }
        currentGatt = null
    }

    private fun startRssiPolling() {
        stopRssiPolling()
        val runnable = object : Runnable {
            override fun run() {
                val gatt = currentGatt ?: return
                try {
                    gatt.readRemoteRssi()
                } catch (_: SecurityException) {
                    handleDisconnected(currentAddress, gatt)
                    return
                }
                if (currentGatt == gatt && currentAddress != null) {
                    mainHandler.postDelayed(this, RSSI_REFRESH_INTERVAL_MS)
                }
            }
        }
        rssiPollingRunnable = runnable
        mainHandler.post(runnable)
    }

    private fun stopRssiPolling() {
        rssiPollingRunnable?.let { runnable ->
            mainHandler.removeCallbacks(runnable)
        }
        rssiPollingRunnable = null
    }

    /**
     * 服务发现完成后自动启用所有目标特征的 Notify/Indicate 订阅。
     * 支持重试，最多 [AUTO_SUBSCRIBE_MAX_RETRY] 次，订阅成功后继续处理下一个目标。
     */
    @SuppressLint("MissingPermission")
    private fun triggerAutoSubscribe() {
        if (!UserConfig.auto_config_ble_device) return
        val gatt = currentGatt ?: return
        val info = connectedDeviceInfo ?: return

        val targetPairs = UserConfig.esp32_known_service_configs
            .flatMap { svcConfig ->
                val svcUuid = svcConfig.serviceUuid.trim()
                if (svcUuid.isEmpty()) emptyList()
                else svcConfig.characteristicUuids
                    .map { it.trim() }.filter { it.isNotEmpty() }
                    .map { svcUuid to it }
            }
        if (targetPairs.isEmpty()) return

        val pending = targetPairs.mapNotNull { (svcUuid, chrUuid) ->
            val key = "${info.address.lowercase()}|${svcUuid.lowercase()}|${chrUuid.lowercase()}"
            if (key in autoSubscribedKeys || key in autoSubscribeSkipped) return@mapNotNull null

            val chr = gatt.services.orEmpty()
                .firstOrNull { it.uuid.toString().equals(svcUuid, ignoreCase = true) }
                ?.characteristics?.firstOrNull { it.uuid.toString().equals(chrUuid, ignoreCase = true) }
                ?: return@mapNotNull null

            val stateKey = characteristicKey(svcUuid, chrUuid)
            if (characteristicSubscriptionStates[stateKey] == true) {
                autoSubscribedKeys.add(key)
                return@mapNotNull null
            }
            if (!characteristicCanSubscribe(chr.properties)) return@mapNotNull null

            Triple(svcUuid, chrUuid, key)
        }
        if (pending.isEmpty()) return

        val (svcUuid, chrUuid, key) = pending.first()
        when (toggleCharacteristicSubscription(svcUuid, chrUuid)) {
            BleSubscriptionToggleResult.Enabled -> {
                autoSubscribedKeys.add(key)
                autoSubscribeRetries.remove(key)
                autoSubscribeSkipped.remove(key)
                if (pending.size > 1) {
                    mainHandler.postDelayed(autoSubscribeRetryRunnable, AUTO_SUBSCRIBE_RETRY_DELAY_MS)
                }
            }
            BleSubscriptionToggleResult.Disabled,
            BleSubscriptionToggleResult.Failed -> {
                val retries = (autoSubscribeRetries[key] ?: 0) + 1
                autoSubscribeRetries[key] = retries
                if (retries >= AUTO_SUBSCRIBE_MAX_RETRY) {
                    autoSubscribeSkipped.add(key)
                } else {
                    mainHandler.postDelayed(autoSubscribeRetryRunnable, AUTO_SUBSCRIBE_RETRY_DELAY_MS)
                }
            }
        }
    }

    private fun clearAutoSubscribeState() {
        mainHandler.removeCallbacks(autoSubscribeRetryRunnable)
        autoSubscribedKeys.clear()
        autoSubscribeRetries.clear()
        autoSubscribeSkipped.clear()
    }

    private fun handleDisconnected(address: String?, callbackGatt: BluetoothGatt) {
        val disconnectedAddress = address ?: callbackGatt.device.address
        stopRssiPolling()
        clearAutoSubscribeState()
        deviceStates[disconnectedAddress] = BleDeviceConnectionStatus.Disconnected
        if (connectedDeviceInfo?.address == disconnectedAddress) {
            connectedDeviceInfo = null
        }
        if (currentAddress == disconnectedAddress) {
            currentAddress = null
        }
        discoveredServices = emptyList()
        characteristicSubscriptionStates.clear()
        characteristicLastReadValues.clear()
        clearTerminalEntries()
        if (currentGatt == callbackGatt) {
            currentGatt = null
        }
        callbackGatt.close()
    }

    fun recordOutgoingMessage(
        characteristicUuid: String,
        value: ByteArray
    ) {
        addTerminalEntry(
            direction = "TX",
            characteristicUuid = characteristicUuid,
            payloadText = formatPayloadText(value)
        )
    }

    private fun updateCharacteristicValue(
        characteristic: BluetoothGattCharacteristic,
        value: ByteArray,
        fromNotify: Boolean
    ) {
        val serviceUuid = characteristic.service?.uuid?.toString() ?: return
        val characteristicUuid = characteristic.uuid.toString()
        val key = characteristicKey(serviceUuid, characteristicUuid)
        characteristicLastReadValues[key] = value.toHexString()
        parseIncomingFrame(
            serviceUuid = serviceUuid,
            characteristicUuid = characteristicUuid,
            value = value
        )
        if (fromNotify) {
            addTerminalEntry(
                direction = "RX",
                characteristicUuid = characteristicUuid,
                payloadText = formatPayloadText(value)
            )
        }
        refreshDiscoveredServices(currentGatt)
    }

    private fun addTerminalEntry(
        direction: String,
        characteristicUuid: String,
        payloadText: String
    ) {
        nextTerminalEntryId += 1
        terminalEntries.add(
            BleTerminalEntryUi(
                id = nextTerminalEntryId,
                direction = direction,
                characteristicUuid = characteristicUuid,
                payloadText = payloadText
            )
        )
        if (terminalEntries.size > MAX_TERMINAL_ENTRIES) {
            terminalEntries.removeAt(0)
        }
    }

    private fun clearTerminalEntries() {
        terminalEntries.clear()
    }

    private fun formatPayloadText(value: ByteArray): String {
        if (value.isEmpty()) return "<empty>"
        val text = value.decodeToString().trimEnd('\u0000')
        val isReadable = text.isNotEmpty() && text.none { char ->
            char.code < 32 && char != '\n' && char != '\r' && char != '\t'
        }
        return if (isReadable) {
            text
        } else {
            value.toHexString()
        }
    }

    private fun parseIncomingFrame(
        serviceUuid: String,
        characteristicUuid: String,
        value: ByteArray
    ) {
        if (value.size < 16) return

        when {
            BleProtocol.isBound(serviceUuid, characteristicUuid) -> {
                BleProtocol.parseRxFrame(value)
            }

            isConfiguredUuidMatch(serviceUuid, UserConfig.esp32_service_2_uuid) &&
                isConfiguredUuidMatch(characteristicUuid, UserConfig.esp32_service_2_characteristic_1_uuid) -> {
                UserConfig.rx_data_stream_1 = parseInt32ArrayFrom16Bytes(value)
            }

            isConfiguredUuidMatch(serviceUuid, UserConfig.esp32_service_2_uuid) &&
                isConfiguredUuidMatch(characteristicUuid, UserConfig.esp32_service_2_characteristic_2_uuid) -> {
                UserConfig.rx_data_stream_2 = parseInt32ArrayFrom16Bytes(value)
            }

            isConfiguredUuidMatch(serviceUuid, UserConfig.esp32_service_2_uuid) &&
                isConfiguredUuidMatch(characteristicUuid, UserConfig.esp32_service_2_characteristic_3_uuid) -> {
                UserConfig.rx_data_stream_3 = parseInt32ArrayFrom16Bytes(value)
            }
        }
    }

    private fun isConfiguredUuidMatch(actualUuid: String, configuredUuid: String?): Boolean {
        val targetUuid = configuredUuid?.trim().orEmpty()
        if (targetUuid.isEmpty()) return false
        return actualUuid.trim().equals(targetUuid, ignoreCase = true)
    }

    private fun parseUInt32BigEndian(bytes: ByteArray, offset: Int): Long {
        return ((bytes[offset].toLong() and 0xFFL) shl 24) or
            ((bytes[offset + 1].toLong() and 0xFFL) shl 16) or
            ((bytes[offset + 2].toLong() and 0xFFL) shl 8) or
            (bytes[offset + 3].toLong() and 0xFFL)
    }

    private fun parseInt32ArrayFrom16Bytes(bytes: ByteArray): IntArray {
        return intArrayOf(
            parseInt32BigEndian(bytes, 0),
            parseInt32BigEndian(bytes, 4),
            parseInt32BigEndian(bytes, 8),
            parseInt32BigEndian(bytes, 12)
        )
    }

    private fun parseInt32BigEndian(bytes: ByteArray, offset: Int): Int {
        return ((bytes[offset].toInt() and 0xFF) shl 24) or
            ((bytes[offset + 1].toInt() and 0xFF) shl 16) or
            ((bytes[offset + 2].toInt() and 0xFF) shl 8) or
            (bytes[offset + 3].toInt() and 0xFF)
    }

    private fun refreshDiscoveredServices(gatt: BluetoothGatt?) {
        val safeGatt = gatt ?: run {
            discoveredServices = emptyList()
            return
        }

        val services = safeGatt.services.orEmpty().map { service ->
            val serviceUuid = service.uuid.toString()
            val characteristics = service.characteristics.orEmpty().map { characteristic ->
                val characteristicUuid = characteristic.uuid.toString()
                val key = characteristicKey(serviceUuid, characteristicUuid)
                BleGattCharacteristicUi(
                    serviceUuid = serviceUuid,
                    uuid = characteristicUuid,
                    name = characteristicDisplayName(characteristic.uuid),
                    properties = characteristic.properties,
                    canRead = characteristicCanRead(characteristic.properties),
                    canWrite = characteristicCanWrite(characteristic.properties),
                    canSubscribe = characteristicCanSubscribe(characteristic.properties),
                    isSubscribed = characteristicSubscriptionStates[key] == true,
                    lastReadValueHex = characteristicLastReadValues[key]
                )
            }
            BleGattServiceUi(
                uuid = serviceUuid,
                name = serviceDisplayName(service.uuid),
                characteristics = characteristics
            )
        }
        discoveredServices = services
    }

    private fun findCharacteristic(
        serviceUuid: String,
        characteristicUuid: String
    ): BluetoothGattCharacteristic? {
        val gatt = currentGatt ?: return null
        val service = gatt.services.orEmpty().firstOrNull { service ->
            service.uuid.toString().equals(serviceUuid, ignoreCase = true)
        } ?: return null
        return service.characteristics.orEmpty().firstOrNull { characteristic ->
            characteristic.uuid.toString().equals(characteristicUuid, ignoreCase = true)
        }
    }

    @SuppressLint("MissingPermission")
    private fun writeCharacteristicCompat(
        gatt: BluetoothGatt,
        characteristic: BluetoothGattCharacteristic,
        value: ByteArray
    ): Boolean {
        return try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                gatt.writeCharacteristic(
                    characteristic,
                    value,
                    BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                ) == BluetoothGatt.GATT_SUCCESS
            } else {
                characteristic.value = value
                gatt.writeCharacteristic(characteristic)
            }
        } catch (_: SecurityException) {
            false
        }
    }

    @SuppressLint("MissingPermission")
    private fun writeDescriptorCompat(
        gatt: BluetoothGatt,
        descriptor: BluetoothGattDescriptor,
        value: ByteArray
    ): Boolean {
        return try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                gatt.writeDescriptor(descriptor, value) == BluetoothGatt.GATT_SUCCESS
            } else {
                descriptor.value = value
                gatt.writeDescriptor(descriptor)
            }
        } catch (_: SecurityException) {
            false
        }
    }

    private fun characteristicCanRead(properties: Int): Boolean {
        return characteristicHasProperty(properties, BluetoothGattCharacteristic.PROPERTY_READ)
    }

    private fun characteristicCanWrite(properties: Int): Boolean {
        return characteristicHasProperty(properties, BluetoothGattCharacteristic.PROPERTY_WRITE) ||
            characteristicHasProperty(properties, BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE) ||
            characteristicHasProperty(properties, BluetoothGattCharacteristic.PROPERTY_SIGNED_WRITE)
    }

    private fun characteristicCanSubscribe(properties: Int): Boolean {
        return characteristicHasProperty(properties, BluetoothGattCharacteristic.PROPERTY_NOTIFY) ||
            characteristicHasProperty(properties, BluetoothGattCharacteristic.PROPERTY_INDICATE)
    }

    private fun characteristicHasProperty(properties: Int, property: Int): Boolean {
        return properties and property != 0
    }

    private fun characteristicKey(serviceUuid: String, characteristicUuid: String): String {
        val normalizedServiceUuid = serviceUuid.trim().lowercase(Locale.US)
        val normalizedCharacteristicUuid = characteristicUuid.trim().lowercase(Locale.US)
        return "$normalizedServiceUuid|$normalizedCharacteristicUuid"
    }

    private fun serviceDisplayName(uuid: UUID): String {
        return when (uuid.toString().lowercase(Locale.US)) {
            "00001800-0000-1000-8000-00805f9b34fb" -> "Generic Access"
            "00001801-0000-1000-8000-00805f9b34fb" -> "Generic Attribute"
            "0000180a-0000-1000-8000-00805f9b34fb" -> "Device Information"
            "0000180f-0000-1000-8000-00805f9b34fb" -> "Battery Service"
            "00001811-0000-1000-8000-00805f9b34fb" -> "Alert Notification Service"
            else -> "Unknown Service"
        }
    }

    private fun characteristicDisplayName(uuid: UUID): String {
        return when (uuid.toString().lowercase(Locale.US)) {
            "00002a00-0000-1000-8000-00805f9b34fb" -> "Device Name"
            "00002a01-0000-1000-8000-00805f9b34fb" -> "Appearance"
            "00002a05-0000-1000-8000-00805f9b34fb" -> "Service Changed"
            "00002a19-0000-1000-8000-00805f9b34fb" -> "Battery Level"
            else -> "Unknown Characteristic"
        }
    }

    private fun ByteArray.toHexString(): String {
        if (isEmpty()) return ""
        return joinToString(separator = " ") { byte ->
            "%02X".format(Locale.US, byte)
        }
    }
}
