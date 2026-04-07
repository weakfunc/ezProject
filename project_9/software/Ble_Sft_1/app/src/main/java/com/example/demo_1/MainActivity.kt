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
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
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

    // ===== [修改开始] 液位测量系统 UI =====

    // 读取 BLE 帧数据
    val frame1 = BleProtocol.rxFrames[0x18]
    val frame2 = BleProtocol.rxFrames[0x19]
    val liquidLevel_mm   = Float.fromBits(frame1?.var4b1 ?: 0)   // 液位高度
    val liquidDelta_mm   = Float.fromBits(frame1?.var4b2 ?: 0)   // 变化量（带正负）
    val systemStatus     = frame1?.var1b1 ?: 0                   // 0=校准中，1=正常
    val distToLiquid_mm  = Float.fromBits(frame2?.var4b1 ?: 0)   // 距液面距离
    val alarmHighEcho_mm = Float.fromBits(frame2?.var4b2 ?: 0)   // 高限回显

    // 超限报警状态
    var showAlarmDialog by remember { mutableStateOf(false) }
    var alarmSnoozed    by remember { mutableStateOf(false) }
    var alarmCooldown   by remember { mutableStateOf(false) }
    val coroutineScope = rememberCoroutineScope()

    val alarmConditionMet = alarmHighEcho_mm > 0f && systemStatus != 0 && liquidLevel_mm > alarmHighEcho_mm

    // 高限回显变化时重置"不再提醒"
    LaunchedEffect(alarmHighEcho_mm) { alarmSnoozed = false }

    // 满足超限条件且不在冷却/已静音时弹出报警
    LaunchedEffect(alarmConditionMet, alarmCooldown, alarmSnoozed) {
        if (alarmConditionMet && !alarmSnoozed && !alarmCooldown && !showAlarmDialog) {
            showAlarmDialog = true
        }
    }

    if (showAlarmDialog) {
        AlertDialog(
            onDismissRequest = {},
            title = { Text("液位超限报警") },
            text  = {
                Text("当前液位 %.0f mm 已超过设定高限 %.0f mm！".format(liquidLevel_mm, alarmHighEcho_mm))
            },
            confirmButton = {
                TextButton(onClick = {
                    showAlarmDialog = false
                    alarmCooldown = true
                    coroutineScope.launch {
                        delay(10_000L)
                        alarmCooldown = false
                    }
                }) { Text("确认") }
            },
            dismissButton = {
                TextButton(onClick = {
                    showAlarmDialog = false
                    alarmSnoozed = true
                }) { Text("不再提醒") }
            }
        )
    }

    fun sendAlarmThreshold(highMm: Float, lowMm: Float) {
        val frame = BleProtocol.buildTxFrame(
            cmd    = 0x21,
            var4_1 = highMm.toBits(),
            var4_2 = lowMm.toBits(),
            var1_1 = 0,
            var1_2 = 0
        )
        val ok = BleConnectionManager.writeCharacteristic(
            serviceUuid        = UserConfig.esp32_service_1_uuid,
            characteristicUuid = UserConfig.esp32_service_1_characteristic_1_uuid,
            value              = frame
        )
        if (ok) BleConnectionManager.recordOutgoingMessage(
            characteristicUuid = UserConfig.esp32_service_1_characteristic_1_uuid,
            value              = frame
        )
    }

    val pageContent: @Composable (Modifier) -> Unit = { containerModifier ->
        Column(
            modifier = containerModifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            // 顶部标题区域
            Text(
                text = "液位高度测量系统",
                style = MaterialTheme.typography.headlineMedium,
                fontWeight = FontWeight.Bold,
                textAlign = TextAlign.Center,
                modifier = Modifier.fillMaxWidth()
            )
            Text(
                text = "刘皓然",
                style = MaterialTheme.typography.titleLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center,
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(bottom = 4.dp)
            )

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
                // 区域一：系统状态
                HomeSection(title = "系统状态") {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 16.dp, vertical = 12.dp)
                    ) {
                        val statusText  = if (systemStatus == 0) "正在校准..." else "正常工作"
                        val statusColor = if (systemStatus == 0) Color(0xFFFF9800) else Color(0xFF4CAF50)
                        Text(
                            text  = statusText,
                            color = statusColor,
                            style = MaterialTheme.typography.titleMedium
                        )
                    }
                }

                // 区域二：实时测量数据
                HomeSection(title = "实时测量数据") {
                    val isCalib = systemStatus == 0
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 16.dp, vertical = 12.dp),
                        verticalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        ProtoFieldRow(
                            label1 = "液位高度",
                            value1 = if (isCalib) "---" else "%.0f mm".format(liquidLevel_mm),
                            label2 = "液位变化量",
                            value2 = if (isCalib) "---" else "%+.0f mm".format(liquidDelta_mm),
                            value1Color = if (!isCalib && alarmConditionMet) Color.Red else null
                        )
                        ProtoFieldRow(
                            label1 = "距液面距离",
                            value1 = if (isCalib) "---" else "%.0f mm".format(distToLiquid_mm),
                            label2 = "当前高限",
                            value2 = if (isCalib) "---"
                                     else if (alarmHighEcho_mm > 0f) "%.0f mm".format(alarmHighEcho_mm)
                                     else "未设置"
                        )
                    }
                }

                // 区域三：报警阈值设置
                HomeSection(title = "报警阈值设置") {
                    var highInput by remember { mutableStateOf("") }
                    var lowInput  by remember { mutableStateOf("") }
                    val context = LocalContext.current

                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 16.dp, vertical = 12.dp)
                    ) {
                        OutlinedTextField(
                            value         = highInput,
                            onValueChange = { highInput = it },
                            label         = { Text("高报警阈值 (mm)") },
                            placeholder   = { Text("如：800") },
                            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                            modifier      = Modifier.fillMaxWidth()
                        )
                        Spacer(modifier = Modifier.height(8.dp))
                        OutlinedTextField(
                            value         = lowInput,
                            onValueChange = { lowInput = it },
                            label         = { Text("低报警阈值 (mm)") },
                            placeholder   = { Text("如：200") },
                            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                            modifier      = Modifier.fillMaxWidth()
                        )
                        Spacer(modifier = Modifier.height(12.dp))
                        Button(
                            onClick = {
                                val high = highInput.toFloatOrNull()
                                val low  = lowInput.toFloatOrNull()
                                when {
                                    high == null || low == null  -> Toast.makeText(context, "请输入有效数值", Toast.LENGTH_SHORT).show()
                                    high <= 0f || low <= 0f      -> Toast.makeText(context, "阈值须大于0", Toast.LENGTH_SHORT).show()
                                    high > 5000f || low > 5000f  -> Toast.makeText(context, "阈值超出范围（最大5000mm）", Toast.LENGTH_SHORT).show()
                                    high <= low                  -> Toast.makeText(context, "高限须大于低限", Toast.LENGTH_SHORT).show()
                                    else                         -> sendAlarmThreshold(high, low)
                                }
                            },
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text("设置阈值")
                        }
                    }
                }
            }
        }
    }

    // ===== [修改结束] =====

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

@Composable
private fun ProtoFieldRow(
    label1: String,
    value1: String,
    label2: String,
    value2: String,
    value1Color: Color? = null,
    value2Color: Color? = null
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text  = label1,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Text(
                text  = value1,
                style = MaterialTheme.typography.bodyLarge,
                fontWeight = FontWeight.Medium,
                color = value1Color ?: MaterialTheme.colorScheme.onSurface
            )
        }
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text  = label2,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Text(
                text  = value2,
                style = MaterialTheme.typography.bodyLarge,
                fontWeight = FontWeight.Medium,
                color = value2Color ?: MaterialTheme.colorScheme.onSurface
            )
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
