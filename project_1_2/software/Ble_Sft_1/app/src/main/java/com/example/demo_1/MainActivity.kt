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
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import com.example.demo_1.ui.theme.Demo_1Theme
import java.util.Locale

@OptIn(ExperimentalMaterial3Api::class)
class MainActivity : ComponentActivity() {
    private companion object {
        private const val HIDDEN_MODE_AUTO_CONFIG_RETRY_DELAY_MS = 300L
        private const val HIDDEN_MODE_AUTO_CONFIG_MAX_RETRY_PER_CHARACTERISTIC = 6
        private const val HIDDEN_MODE_MANUAL_REFRESH_SCAN_WINDOW_MS = 5000L
    }

    private var isBackgroundScanning = false
    private var isHomePullRefreshing by mutableStateOf(false)
    private var pendingManualRefreshSearch = false
    private var isManualRefreshScanInProgress = false
    private val mainHandler = Handler(Looper.getMainLooper())
    private val hiddenModeConfiguredSubscriptionKeys = mutableSetOf<String>()
    private val hiddenModeRetryCounts = mutableMapOf<String, Int>()
    private val hiddenModeSkippedKeys = mutableSetOf<String>()

    private val bluetoothAdapter: BluetoothAdapter? by lazy {
        val manager = getSystemService(BluetoothManager::class.java)
        manager?.adapter
    }

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) {
        if (hasAllRequiredPermissions()) {
            if (pendingManualRefreshSearch) {
                tryStartManualRefreshSearch()
            } else {
                startHiddenModeBleFlowIfNeeded()
            }
        } else if (pendingManualRefreshSearch) {
            pendingManualRefreshSearch = false
            isHomePullRefreshing = false
        }
    }

    private val enableBluetoothLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) {
        if (bluetoothAdapter?.isEnabled == true) {
            if (pendingManualRefreshSearch) {
                tryStartManualRefreshSearch()
            } else {
                startHiddenModeBleFlowIfNeeded()
            }
        } else if (pendingManualRefreshSearch) {
            pendingManualRefreshSearch = false
            isHomePullRefreshing = false
        }
    }

    private val hiddenModeAutoConfigureRetryRunnable = Runnable {
        if (UserConfig.DEVELOPER_MODE) return@Runnable
        val connectedDeviceInfo = BleConnectionManager.connectedDeviceInfo ?: return@Runnable
        maybeAutoConfigureBleDeviceInHiddenMode(
            connectedDeviceInfo = connectedDeviceInfo,
            discoveredServices = BleConnectionManager.discoveredServices
        )
    }

    private val stopManualRefreshScanRunnable = Runnable {
        finishManualRefreshScan()
    }

    private val backgroundScanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            maybeAutoConnectConfiguredDevice(result)
        }

        override fun onBatchScanResults(results: MutableList<ScanResult>) {
            results.forEach { maybeAutoConnectConfiguredDevice(it) }
        }

        override fun onScanFailed(errorCode: Int) {
            isBackgroundScanning = false
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        val previousTab = previousTabFromIntent()
        setContent {
            Demo_1Theme {
                Scaffold(
                    modifier = Modifier.fillMaxSize(),
                    topBar = {
                        TopAppBar(
                            title = { Text(text = UserConfig.project_name) }
                        )
                    },
                    bottomBar = {
                        if (UserConfig.DEVELOPER_MODE) {
                            AppBottomNavigation(
                                currentTab = AppTab.Home,
                                previousTab = previousTab,
                                onTabSelected = { tab ->
                                    navigateToTab(AppTab.Home, tab)
                                }
                            )
                        }
                    }
                ) { innerPadding ->
                    HomeScreen(
                        modifier = Modifier.padding(innerPadding),
                        onAutoConfigureRequested = ::maybeAutoConfigureBleDeviceInHiddenMode,
                        isPullRefreshing = isHomePullRefreshing,
                        onRefreshRequested = ::requestManualRefreshSearchAndConnect
                    )
                }
            }
        }
        startHiddenModeBleFlowIfNeeded()
    }

    override fun onStart() {
        super.onStart()
        startHiddenModeBleFlowIfNeeded()
    }

    override fun onStop() {
        mainHandler.removeCallbacks(hiddenModeAutoConfigureRetryRunnable)
        mainHandler.removeCallbacks(stopManualRefreshScanRunnable)
        pendingManualRefreshSearch = false
        isManualRefreshScanInProgress = false
        isHomePullRefreshing = false
        stopBackgroundScan()
        super.onStop()
    }

    private fun startHiddenModeBleFlowIfNeeded() {
        if (UserConfig.DEVELOPER_MODE) return
        if (isManualRefreshScanInProgress) return
        if (!UserConfig.auto_connect_my_ble_device) {
            stopBackgroundScan()
            return
        }
        if (normalizeMacAddress(UserConfig.esp32_device_mac) == null) {
            stopBackgroundScan()
            return
        }
        if (BleConnectionManager.connectedDeviceInfo != null) {
            stopBackgroundScan()
            return
        }
        if (!packageManager.hasSystemFeature(PackageManager.FEATURE_BLUETOOTH_LE)) return
        if (bluetoothAdapter == null) return
        if (!hasAllRequiredPermissions()) {
            permissionLauncher.launch(requiredPermissions().toTypedArray())
            return
        }
        ensureBluetoothEnabledThenStartBackgroundScan()
    }

    private fun requestManualRefreshSearchAndConnect() {
        if (UserConfig.DEVELOPER_MODE) return
        isHomePullRefreshing = true
        pendingManualRefreshSearch = true
        tryStartManualRefreshSearch()
    }

    private fun tryStartManualRefreshSearch() {
        if (!pendingManualRefreshSearch) return
        if (!packageManager.hasSystemFeature(PackageManager.FEATURE_BLUETOOTH_LE)) {
            pendingManualRefreshSearch = false
            isHomePullRefreshing = false
            return
        }
        if (bluetoothAdapter == null) {
            pendingManualRefreshSearch = false
            isHomePullRefreshing = false
            return
        }
        if (normalizeMacAddress(UserConfig.esp32_device_mac) == null) {
            pendingManualRefreshSearch = false
            isHomePullRefreshing = false
            return
        }
        if (BleConnectionManager.connectedDeviceInfo != null) {
            pendingManualRefreshSearch = false
            isHomePullRefreshing = false
            return
        }
        if (!hasAllRequiredPermissions()) {
            permissionLauncher.launch(requiredPermissions().toTypedArray())
            return
        }

        val adapter = bluetoothAdapter ?: run {
            pendingManualRefreshSearch = false
            isHomePullRefreshing = false
            return
        }
        if (!adapter.isEnabled) {
            enableBluetoothLauncher.launch(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
            return
        }

        pendingManualRefreshSearch = false
        startManualRefreshScan()
    }

    private fun startManualRefreshScan() {
        mainHandler.removeCallbacks(stopManualRefreshScanRunnable)
        isManualRefreshScanInProgress = true
        stopBackgroundScan()
        startBackgroundScan()
        mainHandler.postDelayed(stopManualRefreshScanRunnable, HIDDEN_MODE_MANUAL_REFRESH_SCAN_WINDOW_MS)
    }

    private fun finishManualRefreshScan() {
        mainHandler.removeCallbacks(stopManualRefreshScanRunnable)
        isManualRefreshScanInProgress = false
        isHomePullRefreshing = false
        startHiddenModeBleFlowIfNeeded()
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
        return ContextCompat.checkSelfPermission(
            this,
            Manifest.permission.BLUETOOTH_CONNECT
        ) == PackageManager.PERMISSION_GRANTED
    }

    private fun ensureBluetoothEnabledThenStartBackgroundScan() {
        val adapter = bluetoothAdapter ?: return
        if (adapter.isEnabled) {
            startBackgroundScan()
            return
        }
        enableBluetoothLauncher.launch(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
    }

    @SuppressLint("MissingPermission")
    private fun startBackgroundScan() {
        if (isBackgroundScanning) return
        val scanner = bluetoothAdapter?.bluetoothLeScanner ?: return
        scanner.startScan(backgroundScanCallback)
        isBackgroundScanning = true
    }

    @SuppressLint("MissingPermission")
    private fun stopBackgroundScan() {
        if (!isBackgroundScanning) return
        bluetoothAdapter?.bluetoothLeScanner?.stopScan(backgroundScanCallback)
        isBackgroundScanning = false
    }

    @SuppressLint("MissingPermission")
    private fun maybeAutoConnectConfiguredDevice(result: ScanResult) {
        if (UserConfig.DEVELOPER_MODE) return
        val shouldTryConnect = UserConfig.auto_connect_my_ble_device || isManualRefreshScanInProgress
        if (!shouldTryConnect) return

        val targetMac = normalizeMacAddress(UserConfig.esp32_device_mac) ?: return
        val foundMac = normalizeMacAddress(result.device.address) ?: return
        if (targetMac != foundMac) return
        if (!ensureConnectPermission()) return

        val address = result.device.address
        when (BleConnectionManager.getDeviceState(address)) {
            BleDeviceConnectionStatus.Connected -> {
                stopBackgroundScan()
                if (isManualRefreshScanInProgress) {
                    isManualRefreshScanInProgress = false
                    isHomePullRefreshing = false
                    mainHandler.removeCallbacks(stopManualRefreshScanRunnable)
                }
                return
            }
            BleDeviceConnectionStatus.Connecting -> return
            BleDeviceConnectionStatus.Disconnected -> Unit
        }

        stopBackgroundScan()
        if (isManualRefreshScanInProgress) {
            isManualRefreshScanInProgress = false
            isHomePullRefreshing = false
            mainHandler.removeCallbacks(stopManualRefreshScanRunnable)
        }
        BleConnectionManager.connect(
            context = this,
            device = result.device,
            deviceName = resolveDeviceName(result),
            rssi = result.rssi
        )
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

    private fun normalizeMacAddress(mac: String?): String? {
        val trimmed = mac?.trim().orEmpty()
        if (trimmed.isEmpty()) return null
        return trimmed.replace("-", ":").uppercase(Locale.US)
    }

    private fun maybeAutoConfigureBleDeviceInHiddenMode(
        connectedDeviceInfo: ConnectedBleDeviceInfo,
        discoveredServices: List<BleGattServiceUi>
    ) {
        if (UserConfig.DEVELOPER_MODE) return
        mainHandler.removeCallbacks(hiddenModeAutoConfigureRetryRunnable)

        if (!UserConfig.auto_config_ble_device) {
            hiddenModeConfiguredSubscriptionKeys.clear()
            hiddenModeRetryCounts.clear()
            hiddenModeSkippedKeys.clear()
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
            if (targetKey in hiddenModeConfiguredSubscriptionKeys || targetKey in hiddenModeSkippedKeys) {
                return@mapNotNull null
            }

            val targetCharacteristic = discoveredServices.firstOrNull { service ->
                service.uuid.equals(serviceUuid, ignoreCase = true)
            }?.characteristics?.firstOrNull { characteristic ->
                characteristic.uuid.equals(characteristicUuid, ignoreCase = true)
            } ?: return@mapNotNull null

            if (targetCharacteristic.isSubscribed) {
                hiddenModeConfiguredSubscriptionKeys.add(targetKey)
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
                hiddenModeConfiguredSubscriptionKeys.add(targetKey)
                hiddenModeRetryCounts.remove(targetKey)
                hiddenModeSkippedKeys.remove(targetKey)
            }
            BleSubscriptionToggleResult.Disabled,
            BleSubscriptionToggleResult.Failed -> {
                val retryCount = (hiddenModeRetryCounts[targetKey] ?: 0) + 1
                hiddenModeRetryCounts[targetKey] = retryCount
                if (retryCount >= HIDDEN_MODE_AUTO_CONFIG_MAX_RETRY_PER_CHARACTERISTIC) {
                    hiddenModeSkippedKeys.add(targetKey)
                } else {
                    mainHandler.postDelayed(
                        hiddenModeAutoConfigureRetryRunnable,
                        HIDDEN_MODE_AUTO_CONFIG_RETRY_DELAY_MS
                    )
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
fun HomeScreen(
    modifier: Modifier = Modifier,
    onAutoConfigureRequested: (ConnectedBleDeviceInfo, List<BleGattServiceUi>) -> Unit = { _, _ -> },
    isPullRefreshing: Boolean = false,
    onRefreshRequested: () -> Unit = {}
) {
    val connectedDevice = BleConnectionManager.connectedDeviceInfo
    val discoveredServices = BleConnectionManager.discoveredServices
    // === HomeScreen 修改区域开始：读取数据帧 ===
    val frame18 = BleProtocol.rxFrames[0x18]
    val frame19 = BleProtocol.rxFrames[0x19]
    // === HomeScreen 修改区域结束：读取数据帧 ===

    if (!UserConfig.DEVELOPER_MODE && connectedDevice != null) {
        LaunchedEffect(
            connectedDevice.address,
            connectedDevice.name,
            discoveredServices,
            UserConfig.auto_config_ble_device,
            UserConfig.esp32_device_name,
            UserConfig.esp32_service_1_uuid,
            UserConfig.esp32_service_1_characteristic_1_uuid,
            UserConfig.esp32_service_2_uuid,
            UserConfig.esp32_service_2_characteristic_1_uuid,
            UserConfig.esp32_service_2_characteristic_2_uuid,
            UserConfig.esp32_service_2_characteristic_3_uuid
        ) {
            onAutoConfigureRequested(connectedDevice, discoveredServices)
        }
    }

    val pageContent: @Composable (Modifier) -> Unit = { containerModifier ->
        Column(
            modifier = containerModifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            // === HomeScreen 修改区域开始：顶部标题区域 ===
            Text(
                text = "2200810815李家毅",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center,
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(bottom = 4.dp)
            )
            // === HomeScreen 修改区域结束：顶部标题区域 ===

            // BLE 连接状态
            HomeSection(
                title = "BLE 连接状态",
                cardContainerColor = MaterialTheme.colorScheme.surfaceVariant
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 12.dp),
                    verticalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    if (connectedDevice == null) {
                        Text(
                            text = "未连接",
                            style = MaterialTheme.typography.titleMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Text(
                            text = "下拉刷新可重新搜索设备",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    } else {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Text(
                                text = "已连接",
                                style = MaterialTheme.typography.titleMedium,
                                color = MaterialTheme.colorScheme.primary
                            )
                            Text(
                                text = "${connectedDevice.rssi} dBm",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                        }
                        Text(
                            text = "设备：${connectedDevice.name}",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                        Text(
                            text = "MAC：${connectedDevice.address}",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }

            if (connectedDevice != null) {
                // === HomeScreen 修改区域开始：传送带控制 ===
                HomeSection(title = "传送带控制") {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 16.dp, vertical = 14.dp),
                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        Button(
                            onClick = {
                                val frame = BleProtocol.buildTxFrame(cmd = 0x21, var4_1 = 1)
                                val ok = BleConnectionManager.writeCharacteristic(
                                    serviceUuid = UserConfig.esp32_service_1_uuid,
                                    characteristicUuid = UserConfig.esp32_service_1_characteristic_1_uuid,
                                    value = frame
                                )
                                if (ok) BleConnectionManager.recordOutgoingMessage(
                                    characteristicUuid = UserConfig.esp32_service_1_characteristic_1_uuid,
                                    value = frame
                                )
                            },
                            modifier = Modifier.weight(1f)
                        ) {
                            Text(text = "启动传送带")
                        }
                        Button(
                            onClick = {
                                val frame = BleProtocol.buildTxFrame(cmd = 0x21, var4_1 = 0)
                                val ok = BleConnectionManager.writeCharacteristic(
                                    serviceUuid = UserConfig.esp32_service_1_uuid,
                                    characteristicUuid = UserConfig.esp32_service_1_characteristic_1_uuid,
                                    value = frame
                                )
                                if (ok) BleConnectionManager.recordOutgoingMessage(
                                    characteristicUuid = UserConfig.esp32_service_1_characteristic_1_uuid,
                                    value = frame
                                )
                            },
                            colors = ButtonDefaults.buttonColors(
                                containerColor = MaterialTheme.colorScheme.error
                            ),
                            modifier = Modifier.weight(1f)
                        ) {
                            Text(text = "停止传送带")
                        }
                    }
                }

                // 当前分拣信息
                HomeSection(title = "当前分拣信息") {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 16.dp, vertical = 12.dp),
                        verticalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        val packageCount = frame18?.var4b1 ?: 0
                        val servoId      = frame18?.var1b1 ?: 0
                        val packageColor = frame18?.var1b2 ?: 0
                        val packageShape  = frame19?.var4b1 ?: 0
                        val systemStatus = frame19?.var4b2 ?: 0

                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Text(
                                text = "包裹总数",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                            Text(
                                text = "$packageCount",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                        }
                        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Text(
                                text = "控制舵机",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                            Text(
                                text = "$servoId",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                        }
                        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Text(
                                text = "包裹颜色",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                            Text(
                                text = when (packageColor) {
                                    0x01 -> "白色"
                                    0x02 -> "红色"
                                    0x03 -> "黑色"
                                    else -> "未知"
                                },
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                        }
                        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Text(
                                text = "包裹形状",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                            Text(
                                text = when (packageShape) {
                                    0x00 -> "矩形"
                                    0x01 -> "圆形"
                                    else -> "未知"
                                },
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                        }
                        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Text(
                                text = "系统状态",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                            Text(
                                text = if (systemStatus == 1) "启动" else "未启动",
                                style = MaterialTheme.typography.bodyMedium,
                                color = if (systemStatus == 1) MaterialTheme.colorScheme.primary
                                        else MaterialTheme.colorScheme.onSurface
                            )
                        }
                    }
                }
                // === HomeScreen 修改区域结束：当前分拣信息 ===
            }
        }
    }

    if (UserConfig.DEVELOPER_MODE) {
        pageContent(modifier)
    } else if (connectedDevice == null) {
        PullToRefreshBox(
            isRefreshing = isPullRefreshing,
            onRefresh = onRefreshRequested,
            modifier = modifier.fillMaxSize()
        ) {
            pageContent(Modifier)
        }
    } else {
        pageContent(modifier)
    }
}


@Composable
private fun HomeSection(
    title: String,
    cardContainerColor: Color = MaterialTheme.colorScheme.surface,
    content: @Composable () -> Unit
) {
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text(
            text = title,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(horizontal = 4.dp)
        )
        Card(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(22.dp),
            colors = CardDefaults.cardColors(
                containerColor = cardContainerColor
            )
        ) {
            content()
        }
    }
}

@Preview(showBackground = true)
@Composable
fun HomeScreenPreview() {
    Demo_1Theme {
        HomeScreen()
    }
}
