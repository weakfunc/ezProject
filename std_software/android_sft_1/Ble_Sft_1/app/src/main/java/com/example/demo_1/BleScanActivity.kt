package com.example.demo_1

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowDownward
import androidx.compose.material.icons.filled.ArrowUpward
import androidx.compose.material.icons.filled.ChevronRight
import androidx.compose.material.icons.filled.ExpandMore
import androidx.compose.material.icons.filled.Notifications
import androidx.compose.material.icons.filled.NotificationsNone
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import com.example.demo_1.ui.theme.Demo_1Theme
import java.util.Locale

private data class BleDeviceUi(
    val name: String,
    val address: String,
    val rssi: Int
)

private data class WriteTarget(
    val serviceUuid: String,
    val characteristicUuid: String
)

class BleScanActivity : ComponentActivity() {
    private companion object {
        private const val AUTO_CONFIG_RETRY_DELAY_MS = 300L
        private const val AUTO_CONFIG_MAX_RETRY_PER_CHARACTERISTIC = 6
    }

    private val scannedDevices = mutableStateListOf<BleDeviceUi>()
    private var statusText by mutableStateOf("Ready to scan BLE devices")
    private var isScanning by mutableStateOf(false)
    private var isPullRefreshing by mutableStateOf(false)
    private var selectedWriteTarget by mutableStateOf<WriteTarget?>(null)
    private val autoConfiguredSubscriptionKeys = mutableSetOf<String>()
    private val autoConfigureRetryCounts = mutableMapOf<String, Int>()
    private val autoConfigureSkippedKeys = mutableSetOf<String>()
    private val uiHandler = Handler(Looper.getMainLooper())
    private val stopPullRefreshRunnable = Runnable { isPullRefreshing = false }
    private val autoConfigureRetryRunnable = Runnable {
        val connectedDeviceInfo = BleConnectionManager.connectedDeviceInfo ?: return@Runnable
        maybeAutoConfigureBleDevice(
            connectedDeviceInfo = connectedDeviceInfo,
            discoveredServices = BleConnectionManager.discoveredServices
        )
    }

    private val bluetoothAdapter: BluetoothAdapter? by lazy {
        val manager = getSystemService(BluetoothManager::class.java)
        manager?.adapter
    }

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) {
        if (hasAllRequiredPermissions()) {
            ensureBluetoothEnabledThenScan()
        } else {
            statusText = "Bluetooth permission denied"
        }
    }

    private val enableBluetoothLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) {
        if (bluetoothAdapter?.isEnabled == true) {
            startScan()
        } else {
            statusText = "Bluetooth is off"
        }
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            addOrUpdateDevice(result)
        }

        override fun onBatchScanResults(results: MutableList<ScanResult>) {
            results.forEach { addOrUpdateDevice(it) }
        }

        override fun onScanFailed(errorCode: Int) {
            runOnUiThread {
                isScanning = false
                statusText = "Scan failed. Error code: $errorCode"
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        if (!UserConfig.DEVELOPER_MODE) {
            navigateToTab(AppTab.BleScan, AppTab.Home)
            return
        }
        val previousTab = previousTabFromIntent()
        setContent {
            Demo_1Theme {
                BleScanScreen(
                    devices = scannedDevices,
                    statusText = statusText,
                    isScanning = isScanning,
                    isPullRefreshing = isPullRefreshing,
                    connectedDeviceInfo = BleConnectionManager.connectedDeviceInfo,
                    discoveredServices = BleConnectionManager.discoveredServices,
                    terminalEntries = BleConnectionManager.terminalEntries,
                    previousTab = previousTab,
                    onConnectRequested = ::connectToDevice,
                    onDisconnectRequested = ::disconnectCurrentDevice,
                    onReadRequested = ::readCharacteristic,
                    onWriteRequested = ::writeCharacteristic,
                    onSendRequested = ::sendTextMessage,
                    onSubscribeRequested = ::toggleSubscribeCharacteristic,
                    onAutoConfigureRequested = ::maybeAutoConfigureBleDevice,
                    onRefreshRequested = ::refreshScan,
                    onTabSelected = { tab ->
                        navigateToTab(AppTab.BleScan, tab)
                    }
                )
            }
        }
        startBleFlow()
    }

    override fun onStart() {
        super.onStart()
        if (BleConnectionManager.connectedDeviceInfo != null) {
            stopScan()
            return
        }
        if (hasAllRequiredPermissions() && bluetoothAdapter?.isEnabled == true) {
            startScan()
        }
    }

    override fun onStop() {
        uiHandler.removeCallbacks(stopPullRefreshRunnable)
        uiHandler.removeCallbacks(autoConfigureRetryRunnable)
        isPullRefreshing = false
        stopScan()
        super.onStop()
    }

    private fun startBleFlow() {
        if (!packageManager.hasSystemFeature(PackageManager.FEATURE_BLUETOOTH_LE)) {
            statusText = "BLE is not supported on this device"
            return
        }

        if (bluetoothAdapter == null) {
            statusText = "Bluetooth is not supported on this device"
            return
        }

        if (!hasAllRequiredPermissions()) {
            statusText = "Requesting Bluetooth permissions..."
            permissionLauncher.launch(requiredPermissions().toTypedArray())
            return
        }

        ensureBluetoothEnabledThenScan()
    }

    private fun ensureBluetoothEnabledThenScan() {
        val adapter = bluetoothAdapter ?: return
        if (adapter.isEnabled) {
            startScan()
            return
        }
        statusText = "Requesting to enable Bluetooth..."
        enableBluetoothLauncher.launch(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
    }

    private fun requiredPermissions(): List<String> {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            listOf(
                Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_CONNECT
            )
        } else {
            listOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }
    }

    private fun hasAllRequiredPermissions(): Boolean {
        return requiredPermissions().all { permission ->
            ContextCompat.checkSelfPermission(
                this,
                permission
            ) == PackageManager.PERMISSION_GRANTED
        }
    }

    private fun ensureConnectPermission(): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) return true
        val granted = ContextCompat.checkSelfPermission(
            this,
            Manifest.permission.BLUETOOTH_CONNECT
        ) == PackageManager.PERMISSION_GRANTED
        if (!granted) {
            statusText = "Bluetooth connect permission denied"
        }
        return granted
    }

    @SuppressLint("MissingPermission")
    private fun startScan() {
        if (isScanning) return
        val scanner = bluetoothAdapter?.bluetoothLeScanner
        if (scanner == null) {
            statusText = "Unable to access BLE scanner"
            return
        }
        scannedDevices.clear()
        scanner.startScan(scanCallback)
        isScanning = true
        statusText = "Scanning..."
    }

    @SuppressLint("MissingPermission")
    private fun stopScan() {
        if (!isScanning) return
        bluetoothAdapter?.bluetoothLeScanner?.stopScan(scanCallback)
        isScanning = false
    }

    private fun refreshScan() {
        if (BleConnectionManager.connectedDeviceInfo != null) return
        isPullRefreshing = true
        uiHandler.removeCallbacks(stopPullRefreshRunnable)

        if (!hasAllRequiredPermissions()) {
            statusText = "Requesting Bluetooth permissions..."
            permissionLauncher.launch(requiredPermissions().toTypedArray())
            uiHandler.postDelayed(stopPullRefreshRunnable, 900L)
            return
        }

        val adapter = bluetoothAdapter
        if (adapter == null) {
            statusText = "Bluetooth is not supported on this device"
            uiHandler.postDelayed(stopPullRefreshRunnable, 500L)
            return
        }
        if (!adapter.isEnabled) {
            statusText = "Requesting to enable Bluetooth..."
            enableBluetoothLauncher.launch(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
            uiHandler.postDelayed(stopPullRefreshRunnable, 900L)
            return
        }

        stopScan()
        startScan()
        uiHandler.postDelayed(stopPullRefreshRunnable, 900L)
    }

    private fun addOrUpdateDevice(result: ScanResult) {
        runOnUiThread {
            val item = BleDeviceUi(
                name = resolveDeviceName(result),
                address = result.device.address,
                rssi = result.rssi
            )
            val autoConnectHandled = maybeAutoConnectConfiguredDevice(item)
            if (!shouldDisplayDevice(item.name)) {
                scannedDevices.removeAll { it.address == item.address }
                if (!autoConnectHandled) {
                    statusText = "Found ${scannedDevices.size} devices"
                }
                return@runOnUiThread
            }
            val index = scannedDevices.indexOfFirst { it.address == item.address }
            if (index >= 0) {
                scannedDevices[index] = item
            } else {
                scannedDevices.add(item)
            }
            if (!autoConnectHandled) {
                statusText = "Found ${scannedDevices.size} devices"
            }
        }
    }

    private fun shouldDisplayDevice(deviceName: String): Boolean {
        if (!UserConfig.auto_filter_my_ble_devices) {
            return true
        }
        val keyword = UserConfig.esp32_device_name.trim()
        if (keyword.isEmpty()) {
            return true
        }
        return deviceName.contains(keyword, ignoreCase = true)
    }

    private fun resolveDeviceName(result: ScanResult): String {
        val nameFromRecord = result.scanRecord?.deviceName?.trim()
        if (!nameFromRecord.isNullOrEmpty()) {
            return nameFromRecord
        }
        return try {
            result.device.name?.trim().takeUnless { it.isNullOrEmpty() } ?: "Unnamed device"
        } catch (_: SecurityException) {
            "Unnamed device"
        }
    }

    @SuppressLint("MissingPermission")
    private fun connectToDevice(address: String) {
        val selected = scannedDevices.firstOrNull { it.address == address } ?: return
        connectToScannedDevice(selected)
    }

    @SuppressLint("MissingPermission")
    private fun connectToScannedDevice(selected: BleDeviceUi) {
        if (!ensureConnectPermission()) return

        val address = selected.address
        when (BleConnectionManager.getDeviceState(address)) {
            BleDeviceConnectionStatus.Connected -> {
                statusText = "Already connected: $address"
                return
            }

            BleDeviceConnectionStatus.Connecting -> {
                statusText = "Connecting to $address..."
                return
            }

            BleDeviceConnectionStatus.Disconnected -> Unit
        }

        val bluetoothDevice = try {
            bluetoothAdapter?.getRemoteDevice(address)
        } catch (_: IllegalArgumentException) {
            null
        }

        if (bluetoothDevice == null) {
            statusText = "Device unavailable: $address"
            return
        }

        selectedWriteTarget = null
        autoConfiguredSubscriptionKeys.clear()
        autoConfigureRetryCounts.clear()
        autoConfigureSkippedKeys.clear()
        uiHandler.removeCallbacks(autoConfigureRetryRunnable)
        stopScan()
        BleConnectionManager.connect(
            context = this,
            device = bluetoothDevice,
            deviceName = selected.name,
            rssi = selected.rssi
        )
        statusText = "Connecting to $address..."
    }

    @SuppressLint("MissingPermission")
    private fun maybeAutoConnectConfiguredDevice(device: BleDeviceUi): Boolean {
        if (!UserConfig.auto_connect_my_ble_device) return false
        val targetMac = normalizeMacAddress(UserConfig.esp32_device_mac) ?: return false
        val foundMac = normalizeMacAddress(device.address) ?: return false
        if (targetMac != foundMac) return false

        connectToScannedDevice(device)
        return true
    }

    private fun normalizeMacAddress(mac: String?): String? {
        val trimmed = mac?.trim().orEmpty()
        if (trimmed.isEmpty()) return null
        return trimmed.replace("-", ":").uppercase(Locale.US)
    }

    private fun disconnectCurrentDevice() {
        val lastAddress = BleConnectionManager.connectedDeviceInfo?.address
        BleConnectionManager.disconnectCurrent()
        selectedWriteTarget = null
        autoConfiguredSubscriptionKeys.clear()
        autoConfigureRetryCounts.clear()
        autoConfigureSkippedKeys.clear()
        uiHandler.removeCallbacks(autoConfigureRetryRunnable)
        statusText = if (lastAddress.isNullOrEmpty()) {
            "Disconnected"
        } else {
            "Disconnected: $lastAddress"
        }
        if (hasAllRequiredPermissions() && bluetoothAdapter?.isEnabled == true) {
            startScan()
        }
    }

    private fun readCharacteristic(serviceUuid: String, characteristicUuid: String) {
        if (!ensureConnectPermission()) return
        val started = BleConnectionManager.readCharacteristic(serviceUuid, characteristicUuid)
        statusText = if (started) {
            "Reading characteristic..."
        } else {
            "Read operation unavailable"
        }
    }

    private fun writeCharacteristic(serviceUuid: String, characteristicUuid: String) {
        if (!ensureConnectPermission()) return
        val canWrite = BleConnectionManager.canWriteCharacteristic(serviceUuid, characteristicUuid)
        statusText = if (canWrite) {
            selectedWriteTarget = WriteTarget(
                serviceUuid = serviceUuid,
                characteristicUuid = characteristicUuid
            )
            "Write target UUID: $characteristicUuid. Enter text and tap Send."
        } else {
            "Write operation unavailable"
        }
    }

    private fun resolveWritableTarget(): WriteTarget? {
        val currentTarget = selectedWriteTarget
        if (currentTarget != null) {
            val stillAvailable = BleConnectionManager.discoveredServices.any { service ->
                service.uuid.equals(currentTarget.serviceUuid, ignoreCase = true) &&
                    service.characteristics.any { characteristic ->
                        characteristic.uuid.equals(
                            currentTarget.characteristicUuid,
                            ignoreCase = true
                        ) && characteristic.canWrite
                    }
            }
            if (stillAvailable) {
                return currentTarget
            }
        }

        val fallbackTarget = BleConnectionManager.discoveredServices.firstNotNullOfOrNull { service ->
            service.characteristics.firstOrNull { it.canWrite }?.let { characteristic ->
                WriteTarget(
                    serviceUuid = service.uuid,
                    characteristicUuid = characteristic.uuid
                )
            }
        }
        selectedWriteTarget = fallbackTarget
        return fallbackTarget
    }

    private fun sendTextMessage(message: String) {
        if (!ensureConnectPermission()) return
        if (message.isBlank()) {
            statusText = "Please enter text to send"
            return
        }
        val target = resolveWritableTarget()
        if (target == null) {
            statusText = "No writable characteristic available"
            return
        }
        val payload = message.toByteArray(Charsets.UTF_8)
        val sent = BleConnectionManager.writeCharacteristic(
            serviceUuid = target.serviceUuid,
            characteristicUuid = target.characteristicUuid,
            value = payload
        )
        statusText = if (sent) {
            BleConnectionManager.recordOutgoingMessage(
                characteristicUuid = target.characteristicUuid,
                value = payload
            )
            "Sent ${payload.size} bytes to ${target.characteristicUuid}"
        } else {
            "Send operation unavailable"
        }
    }

    private fun toggleSubscribeCharacteristic(serviceUuid: String, characteristicUuid: String) {
        if (!ensureConnectPermission()) return
        statusText = when (
            BleConnectionManager.toggleCharacteristicSubscription(
                serviceUuid = serviceUuid,
                characteristicUuid = characteristicUuid
            )
        ) {
            BleSubscriptionToggleResult.Enabled -> "Subscribed"
            BleSubscriptionToggleResult.Disabled -> "Unsubscribed"
            BleSubscriptionToggleResult.Failed -> "Subscribe operation failed"
        }
    }

    private fun maybeAutoConfigureBleDevice(
        connectedDeviceInfo: ConnectedBleDeviceInfo,
        discoveredServices: List<BleGattServiceUi>
    ) {
        uiHandler.removeCallbacks(autoConfigureRetryRunnable)

        if (!UserConfig.auto_config_ble_device) {
            autoConfiguredSubscriptionKeys.clear()
            autoConfigureRetryCounts.clear()
            autoConfigureSkippedKeys.clear()
            return
        }
        if (!isConfiguredEsp32Device(connectedDeviceInfo.name)) return

        val targetPairs = UserConfig.esp32_known_service_configs
            .flatMap { serviceConfig ->
                val serviceUuid = serviceConfig.serviceUuid.trim()
                if (serviceUuid.isEmpty()) {
                    emptyList()
                } else {
                    serviceConfig.characteristicUuids
                        .map { it.trim() }
                        .filter { it.isNotEmpty() }
                        .map { characteristicUuid ->
                            serviceUuid to characteristicUuid
                        }
                }
            }
        if (targetPairs.isEmpty()) return

        val pendingTargets = targetPairs.mapNotNull { (serviceUuid, characteristicUuid) ->
            val targetKey = buildAutoSubscribeKey(
                address = connectedDeviceInfo.address,
                serviceUuid = serviceUuid,
                characteristicUuid = characteristicUuid
            )
            if (targetKey in autoConfiguredSubscriptionKeys || targetKey in autoConfigureSkippedKeys) {
                return@mapNotNull null
            }

            val targetCharacteristic = discoveredServices.firstOrNull { service ->
                service.uuid.equals(serviceUuid, ignoreCase = true)
            }?.characteristics?.firstOrNull { characteristic ->
                characteristic.uuid.equals(characteristicUuid, ignoreCase = true)
            } ?: return@mapNotNull null

            if (targetCharacteristic.isSubscribed) {
                autoConfiguredSubscriptionKeys.add(targetKey)
                return@mapNotNull null
            }
            if (!targetCharacteristic.canSubscribe) {
                return@mapNotNull null
            }

            Triple(serviceUuid, characteristicUuid, targetKey)
        }

        if (pendingTargets.isEmpty()) return
        if (!ensureConnectPermission()) return

        val (serviceUuid, characteristicUuid, targetKey) = pendingTargets.first()
        when (
            BleConnectionManager.toggleCharacteristicSubscription(
                serviceUuid = serviceUuid,
                characteristicUuid = characteristicUuid
            )
        ) {
            BleSubscriptionToggleResult.Enabled -> {
                autoConfiguredSubscriptionKeys.add(targetKey)
                autoConfigureRetryCounts.remove(targetKey)
                autoConfigureSkippedKeys.remove(targetKey)
                statusText = "Auto subscribed: $characteristicUuid"
            }
            BleSubscriptionToggleResult.Disabled,
            BleSubscriptionToggleResult.Failed -> {
                val retryCount = (autoConfigureRetryCounts[targetKey] ?: 0) + 1
                autoConfigureRetryCounts[targetKey] = retryCount
                if (retryCount >= AUTO_CONFIG_MAX_RETRY_PER_CHARACTERISTIC) {
                    autoConfigureSkippedKeys.add(targetKey)
                    statusText = "Auto subscribe skipped after $retryCount retries: $characteristicUuid"
                } else {
                    statusText = "Auto subscribe retry $retryCount: $characteristicUuid"
                    uiHandler.postDelayed(autoConfigureRetryRunnable, AUTO_CONFIG_RETRY_DELAY_MS)
                }
            }
        }
    }

    private fun isConfiguredEsp32Device(deviceName: String): Boolean {
        val configuredName = UserConfig.esp32_device_name.trim()
        if (configuredName.isEmpty()) return false
        return deviceName.contains(configuredName, ignoreCase = true)
    }

    private fun buildAutoSubscribeKey(
        address: String,
        serviceUuid: String,
        characteristicUuid: String
    ): String {
        return listOf(
            address.lowercase(Locale.US),
            serviceUuid.lowercase(Locale.US),
            characteristicUuid.lowercase(Locale.US)
        ).joinToString(separator = "|")
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun BleScanScreen(
    devices: List<BleDeviceUi>,
    statusText: String,
    isScanning: Boolean,
    isPullRefreshing: Boolean,
    connectedDeviceInfo: ConnectedBleDeviceInfo?,
    discoveredServices: List<BleGattServiceUi>,
    terminalEntries: List<BleTerminalEntryUi>,
    previousTab: AppTab?,
    onConnectRequested: (String) -> Unit,
    onDisconnectRequested: () -> Unit,
    onReadRequested: (String, String) -> Unit,
    onWriteRequested: (String, String) -> Unit,
    onSendRequested: (String) -> Unit,
    onSubscribeRequested: (String, String) -> Unit,
    onAutoConfigureRequested: (ConnectedBleDeviceInfo, List<BleGattServiceUi>) -> Unit,
    onRefreshRequested: () -> Unit,
    onTabSelected: (AppTab) -> Unit
) {
    Scaffold(
        modifier = Modifier.fillMaxSize(),
        topBar = {
            TopAppBar(
                title = { Text(text = UserConfig.ble_scan_page_title) }
            )
        },
        bottomBar = {
            AppBottomNavigation(
                currentTab = AppTab.BleScan,
                previousTab = previousTab,
                onTabSelected = onTabSelected
            )
        }
    ) { innerPadding ->
        if (connectedDeviceInfo == null) {
            ScanDeviceListSection(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(innerPadding)
                    .padding(horizontal = 16.dp, vertical = 12.dp),
                devices = devices,
                statusText = statusText,
                isScanning = isScanning,
                isPullRefreshing = isPullRefreshing,
                onRefreshRequested = onRefreshRequested,
                onConnectRequested = onConnectRequested
            )
        } else {
            ConnectedGattSection(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(innerPadding)
                    .padding(horizontal = 16.dp, vertical = 12.dp),
                connectedDeviceInfo = connectedDeviceInfo,
                statusText = statusText,
                discoveredServices = discoveredServices,
                terminalEntries = terminalEntries,
                onReadRequested = onReadRequested,
                onWriteRequested = onWriteRequested,
                onSendRequested = onSendRequested,
                onSubscribeRequested = onSubscribeRequested,
                onAutoConfigureRequested = onAutoConfigureRequested,
                onDisconnectRequested = onDisconnectRequested
            )
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun ScanDeviceListSection(
    modifier: Modifier,
    devices: List<BleDeviceUi>,
    statusText: String,
    isScanning: Boolean,
    isPullRefreshing: Boolean,
    onRefreshRequested: () -> Unit,
    onConnectRequested: (String) -> Unit
) {
    PullToRefreshBox(
        isRefreshing = isPullRefreshing,
        onRefresh = onRefreshRequested,
        modifier = modifier
    ) {
        Column(modifier = Modifier.fillMaxSize()) {
            Text(
                text = if (isScanning) {
                    "Scanning, $statusText"
                } else {
                    statusText
                },
                style = MaterialTheme.typography.bodyMedium
            )
            Spacer(modifier = Modifier.height(12.dp))
            LazyColumn(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                if (devices.isEmpty()) {
                    item {
                        val emptyTip = if (UserConfig.auto_filter_my_ble_devices) {
                            "No devices found（过滤: ${UserConfig.esp32_device_name}），下拉刷新重试"
                        } else {
                            "No devices found，下拉刷新重试"
                        }
                        Text(
                            text = emptyTip,
                            style = MaterialTheme.typography.bodyMedium
                        )
                    }
                } else {
                    items(
                        items = devices,
                        key = { it.address }
                    ) { device ->
                        val connectionState = BleConnectionManager.getDeviceState(device.address)
                        Card(modifier = Modifier.fillMaxWidth()) {
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(12.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Column(modifier = Modifier.weight(1f)) {
                                    Text(
                                        text = device.name,
                                        style = MaterialTheme.typography.titleMedium
                                    )
                                    Text(
                                        text = device.address,
                                        style = MaterialTheme.typography.bodyMedium
                                    )
                                    Text(
                                        text = "RSSI: ${device.rssi}",
                                        style = MaterialTheme.typography.bodySmall
                                    )
                                }
                                Button(
                                    onClick = { onConnectRequested(device.address) },
                                    enabled = connectionState == BleDeviceConnectionStatus.Disconnected
                                ) {
                                    Text(
                                        text = when (connectionState) {
                                            BleDeviceConnectionStatus.Disconnected -> "\u8fde\u63a5"
                                            BleDeviceConnectionStatus.Connecting -> "\u8fde\u63a5\u4e2d"
                                            BleDeviceConnectionStatus.Connected -> "\u5df2\u8fde\u63a5"
                                        }
                                    )
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun ConnectedGattSection(
    modifier: Modifier,
    connectedDeviceInfo: ConnectedBleDeviceInfo,
    statusText: String,
    discoveredServices: List<BleGattServiceUi>,
    terminalEntries: List<BleTerminalEntryUi>,
    onReadRequested: (String, String) -> Unit,
    onWriteRequested: (String, String) -> Unit,
    onSendRequested: (String) -> Unit,
    onSubscribeRequested: (String, String) -> Unit,
    onAutoConfigureRequested: (ConnectedBleDeviceInfo, List<BleGattServiceUi>) -> Unit,
    onDisconnectRequested: () -> Unit
) {
    val expandedServiceStates = remember { mutableStateMapOf<String, Boolean>() }
    var outgoingText by remember(connectedDeviceInfo.address) { mutableStateOf("") }
    LaunchedEffect(discoveredServices) {
        val currentIds = discoveredServices.map { it.uuid }.toSet()
        expandedServiceStates.keys.toList().forEach { key ->
            if (key !in currentIds) {
                expandedServiceStates.remove(key)
            }
        }
        discoveredServices.forEach { service ->
            if (expandedServiceStates[service.uuid] == null) {
                expandedServiceStates[service.uuid] = false
            }
        }
    }
    LaunchedEffect(
        connectedDeviceInfo.address,
        connectedDeviceInfo.name,
        discoveredServices,
        UserConfig.auto_config_ble_device,
        UserConfig.esp32_device_name,
        UserConfig.esp32_service_1_uuid,
        UserConfig.esp32_service_1_characteristic_1_uuid
    ) {
        onAutoConfigureRequested(connectedDeviceInfo, discoveredServices)
    }

    Column(modifier = modifier) {
        Card(modifier = Modifier.fillMaxWidth()) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(12.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = connectedDeviceInfo.name,
                        style = MaterialTheme.typography.titleMedium
                    )
                    Text(
                        text = connectedDeviceInfo.address,
                        style = MaterialTheme.typography.bodyMedium
                    )
                    Text(
                        text = "RSSI: ${connectedDeviceInfo.rssi}",
                        style = MaterialTheme.typography.bodySmall
                    )
                }
                Spacer(modifier = Modifier.width(12.dp))
                Button(onClick = onDisconnectRequested) {
                    Text(text = "\u65ad\u5f00\u8fde\u63a5")
                }
            }
        }
        Spacer(modifier = Modifier.height(8.dp))
        Text(
            text = statusText,
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Spacer(modifier = Modifier.height(12.dp))
        if (discoveredServices.isEmpty()) {
            Text(
                text = "Discovering GATT services...",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Spacer(modifier = Modifier.weight(1f))
        } else {
            LazyColumn(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                items(
                    items = discoveredServices,
                    key = { it.uuid }
                ) { service ->
                    val expanded = expandedServiceStates[service.uuid] == true
                    ServiceItemCard(
                        service = service,
                        expanded = expanded,
                        onServiceClick = {
                            expandedServiceStates[service.uuid] = !expanded
                        },
                        onReadRequested = onReadRequested,
                        onWriteRequested = onWriteRequested,
                        onSubscribeRequested = onSubscribeRequested
                    )
                }
            }
        }
        Spacer(modifier = Modifier.height(12.dp))
        TerminalOutputWindow(
            entries = terminalEntries,
            modifier = Modifier
                .fillMaxWidth()
                .height(120.dp)
        )
        Spacer(modifier = Modifier.height(12.dp))
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            TextField(
                value = outgoingText,
                onValueChange = { outgoingText = it },
                modifier = Modifier.weight(1f),
                singleLine = true,
                label = { Text(text = "\u8f93\u5165\u53d1\u9001\u5185\u5bb9") }
            )
            Spacer(modifier = Modifier.width(8.dp))
            Button(
                onClick = { onSendRequested(outgoingText) },
                enabled = outgoingText.isNotBlank()
            ) {
                Text(text = "\u53d1\u9001")
            }
        }
    }
}

@Composable
private fun TerminalOutputWindow(
    entries: List<BleTerminalEntryUi>,
    modifier: Modifier = Modifier
) {
    val listState = rememberLazyListState()
    LaunchedEffect(entries.size) {
        if (entries.isNotEmpty()) {
            listState.scrollToItem(index = entries.lastIndex)
        }
    }

    Card(modifier = modifier) {
        if (entries.isEmpty()) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(12.dp)
            ) {
                Text(
                    text = "Terminal output: sent data and notify data will appear here.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        } else {
            LazyColumn(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(horizontal = 12.dp, vertical = 10.dp),
                state = listState,
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                items(
                    items = entries,
                    key = { it.id }
                ) { entry ->
                    val shortUuid = entry.characteristicUuid.take(16)
                    val lineText = buildAnnotatedString {
                        withStyle(style = SpanStyle(color = Color(0xFF1565C0))) {
                            append(entry.direction)
                        }
                        append("  ")
                        withStyle(style = SpanStyle(color = Color(0xFF2E7D32))) {
                            append(shortUuid)
                        }
                        append("  ")
                        withStyle(style = SpanStyle(color = Color(0xFF6D4C41))) {
                            append(entry.payloadText)
                        }
                    }
                    Text(
                        text = lineText,
                        style = MaterialTheme.typography.bodySmall
                    )
                }
            }
        }
    }
}

@Composable
private fun ServiceItemCard(
    service: BleGattServiceUi,
    expanded: Boolean,
    onServiceClick: () -> Unit,
    onReadRequested: (String, String) -> Unit,
    onWriteRequested: (String, String) -> Unit,
    onSubscribeRequested: (String, String) -> Unit
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable(onClick = onServiceClick)
                    .padding(horizontal = 12.dp, vertical = 10.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Icon(
                    imageVector = if (expanded) Icons.Filled.ExpandMore else Icons.Filled.ChevronRight,
                    contentDescription = if (expanded) "Collapse service" else "Expand service",
                    tint = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Column(
                    modifier = Modifier
                        .weight(1f)
                        .padding(start = 4.dp)
                ) {
                    Text(
                        text = service.name,
                        style = MaterialTheme.typography.titleMedium
                    )
                    Text(
                        text = "UUID: ${service.uuid}",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
            if (expanded) {
                service.characteristics.forEachIndexed { index, characteristic ->
                    if (index == 0) {
                        HorizontalDivider()
                    }
                    CharacteristicItemRow(
                        characteristic = characteristic,
                        onReadRequested = onReadRequested,
                        onWriteRequested = onWriteRequested,
                        onSubscribeRequested = onSubscribeRequested
                    )
                    HorizontalDivider()
                }
            }
        }
    }
}

@Composable
private fun CharacteristicItemRow(
    characteristic: BleGattCharacteristicUi,
    onReadRequested: (String, String) -> Unit,
    onWriteRequested: (String, String) -> Unit,
    onSubscribeRequested: (String, String) -> Unit
) {
    val operations = remember(characteristic) {
        buildList {
            if (characteristic.canRead) add("\u8bfb")
            if (characteristic.canWrite) add("\u5199")
            if (characteristic.canSubscribe) add("\u8ba2\u9605")
        }
    }

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp, vertical = 10.dp),
        verticalAlignment = Alignment.Top
    ) {
        Column(
            modifier = Modifier
                .weight(1f)
                .padding(end = 8.dp)
        ) {
            Text(
                text = characteristic.name,
                style = MaterialTheme.typography.titleMedium
            )
            Text(
                text = "UUID: ${characteristic.uuid}",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Text(
                text = "Properties: ${operations.joinToString(separator = ", ")}",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            if (!characteristic.lastReadValueHex.isNullOrEmpty()) {
                Text(
                    text = "Value: ${characteristic.lastReadValueHex}",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.primary
                )
            }
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            if (characteristic.canRead) {
                IconButton(
                    onClick = {
                        onReadRequested(characteristic.serviceUuid, characteristic.uuid)
                    }
                ) {
                    Icon(
                        imageVector = Icons.Filled.ArrowDownward,
                        contentDescription = "\u8bfb\u53d6",
                        tint = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
            if (characteristic.canWrite) {
                IconButton(
                    onClick = {
                        onWriteRequested(characteristic.serviceUuid, characteristic.uuid)
                    }
                ) {
                    Icon(
                        imageVector = Icons.Filled.ArrowUpward,
                        contentDescription = "\u5199\u5165",
                        tint = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
            if (characteristic.canSubscribe) {
                IconButton(
                    onClick = {
                        onSubscribeRequested(characteristic.serviceUuid, characteristic.uuid)
                    }
                ) {
                    Icon(
                        imageVector = if (characteristic.isSubscribed) {
                            Icons.Filled.Notifications
                        } else {
                            Icons.Filled.NotificationsNone
                        },
                        contentDescription = "\u8ba2\u9605",
                        tint = if (characteristic.isSubscribed) {
                            MaterialTheme.colorScheme.primary
                        } else {
                            MaterialTheme.colorScheme.onSurfaceVariant
                        }
                    )
                }
            }
        }
    }
}
