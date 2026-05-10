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
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import com.example.demo_1.ui.theme.Demo_1Theme
import java.util.Locale
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

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

// === 修改：宠物智能饲喂系统投喂计划槽位 ===
private data class PlanSlot(val planId: Int, val frequencySec: Float, val weightG: Float)
// === END ===

// === 修改：APP运行期投喂历史记录 ===
private data class FeedHistoryRecord(
    val recordNo: Int,
    val feedCountToday: Int,
    val planId: Int,
    val weightAfterG: Float?,
    val systemSec: Int?
)
// === END ===

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

    // === 修改：宠物智能饲喂系统业务数据 ===
    val frame18 = BleProtocol.rxFrames[0x18]
    val frame19 = BleProtocol.rxFrames[0x19]
    val frame1A = BleProtocol.rxFrames[0x1A]

    val currentWeight = frame18?.let { Float.fromBits(it.var4b1) }
    val alarmThreshold = frame18?.let { Float.fromBits(it.var4b2) }
    val feedingState = frame18?.var1b1
    val alarmFlags = frame18?.var1b2 ?: 0

    val feedCountToday = frame19?.var4b1
    val systemSec = frame19?.var4b2
    val currentPlanId = frame19?.var1b1
    val planEnabled = frame19?.var1b2

    val planFreqSec = frame1A?.let { Float.fromBits(it.var4b1) } ?: 0f
    val planWeightG = frame1A?.let { Float.fromBits(it.var4b2) } ?: 0f
    val planSyncId = frame1A?.var1b1 ?: 0
    val planFrameCnt = frame1A?.cnt ?: 0

    var cmdSeq by remember { mutableIntStateOf(0) }
    var commandInFlight by remember { mutableStateOf(false) }
    val commandScope = rememberCoroutineScope()

    fun writeCommandFrames(frame21: ByteArray, frame22: ByteArray, onSent: () -> Unit = {}) {
        if (commandInFlight) return
        commandInFlight = true
        commandScope.launch {
            val sent21 = BleConnectionManager.writeCharacteristic(
                serviceUuid = UserConfig.esp32_service_1_uuid,
                characteristicUuid = UserConfig.esp32_service_1_characteristic_1_uuid,
                value = frame21
            )
            if (sent21) {
                BleConnectionManager.recordOutgoingMessage(
                    characteristicUuid = UserConfig.esp32_service_1_characteristic_1_uuid,
                    value = frame21
                )
            }

            delay(120L)

            val sent22 = BleConnectionManager.writeCharacteristic(
                serviceUuid = UserConfig.esp32_service_1_uuid,
                characteristicUuid = UserConfig.esp32_service_1_characteristic_1_uuid,
                value = frame22
            )
            if (sent22) {
                BleConnectionManager.recordOutgoingMessage(
                    characteristicUuid = UserConfig.esp32_service_1_characteristic_1_uuid,
                    value = frame22
                )
            }
            if (sent21 && sent22) {
                onSent()
            }
            commandInFlight = false
        }
    }

    fun sendCommand(
        cmdCode: Int,
        planId: Int = 0,
        freqSec: Float = 0f,
        weightG: Float = 0f,
        onSent: () -> Unit = {}
    ) {
        cmdSeq = (cmdSeq + 1) and 0xFF
        val f21 = BleProtocol.buildTxFrame(
            cmd = 0x21,
            var4_1 = freqSec.toRawBits(),
            var4_2 = weightG.toRawBits(),
            var1_1 = cmdCode,
            var1_2 = planId
        )
        val f22 = BleProtocol.buildTxFrame(
            cmd = 0x22,
            var4_1 = 0,
            var4_2 = 0,
            var1_1 = cmdSeq,
            var1_2 = 0
        )
        writeCommandFrames(f21, f22, onSent)
    }

    fun sendThreshold(thresholdG: Float) {
        cmdSeq = (cmdSeq + 1) and 0xFF
        val f21 = BleProtocol.buildTxFrame(
            cmd = 0x21,
            var4_1 = 0,
            var4_2 = 0,
            var1_1 = 8,
            var1_2 = 0
        )
        val f22 = BleProtocol.buildTxFrame(
            cmd = 0x22,
            var4_1 = thresholdG.toRawBits(),
            var4_2 = 0,
            var1_1 = cmdSeq,
            var1_2 = 0
        )
        writeCommandFrames(f21, f22)
    }

    val planCache = remember {
        mutableStateListOf(
            PlanSlot(0, 0f, 0f), PlanSlot(0, 0f, 0f), PlanSlot(0, 0f, 0f),
            PlanSlot(0, 0f, 0f), PlanSlot(0, 0f, 0f)
        )
    }

    LaunchedEffect(planFrameCnt, planSyncId, planFreqSec, planWeightG) {
        if (frame1A == null) return@LaunchedEffect
        if (planSyncId in 1..5) {
            planCache[planSyncId - 1] = PlanSlot(planSyncId, planFreqSec, planWeightG)
        }
    }

    var showPlanDialog  by remember { mutableStateOf(false) }
    var dialogPlanId    by remember { mutableIntStateOf(1) }
    var dialogFreqSec   by remember { mutableStateOf<Float?>(null) }
    var dialogWeightG   by remember { mutableStateOf<Float?>(null) }
    var showHistoryDialog by remember { mutableStateOf(false) }
    var lastFeedCountToday by remember { mutableStateOf<Int?>(null) }
    val feedHistory = remember { mutableStateListOf<FeedHistoryRecord>() }

    LaunchedEffect(feedCountToday, currentPlanId, currentWeight, systemSec) {
        val count = feedCountToday ?: return@LaunchedEffect
        val previous = lastFeedCountToday
        if (previous == null || count < previous) {
            lastFeedCountToday = count
            return@LaunchedEffect
        }
        if (count > previous) {
            feedHistory.add(
                0,
                FeedHistoryRecord(
                    recordNo = feedHistory.size + 1,
                    feedCountToday = count,
                    planId = currentPlanId ?: 0,
                    weightAfterG = currentWeight,
                    systemSec = systemSec
                )
            )
            lastFeedCountToday = count
        }
    }

    val isConnected = connectedDevice != null
    val canSend = isConnected && !commandInFlight
    // === END ===

    val pageContent: @Composable (Modifier) -> Unit = { containerModifier ->
        Column(
            modifier = containerModifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            // === 修改：区域0 标题区域 ===
            Column(
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(2.dp)
            ) {
                Text(
                    text = UserConfig.PROJECT_TITLE,
                    style = MaterialTheme.typography.titleLarge,
                    color = MaterialTheme.colorScheme.onSurface,
                    textAlign = TextAlign.Center,
                    modifier = Modifier.fillMaxWidth()
                )
                Text(
                    text = UserConfig.PROJECT_AUTHOR,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    textAlign = TextAlign.Center,
                    modifier = Modifier.fillMaxWidth()
                )
            }
            // === END ===

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
                        if (!canSend) {
                            Text(
                                text = "写入通道未就绪，控制按钮暂不可用",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                }
            }

            // === 修改：区域1 系统状态 ===
            HomeSection(title = "系统状态") {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 12.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                        Text("当前食槽余量", style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        Text(
                            text = if (currentWeight == null) "--" else "${"%.1f".format(currentWeight)} g",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                    }
                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                        Text("投喂状态", style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        Text(
                            text = if (feedingState == null) "--" else if (feedingState == 1) "投喂中" else "空闲",
                            style = MaterialTheme.typography.bodyMedium,
                            color = if (feedingState == 1) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface
                        )
                    }
                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                        Text("当前执行方案", style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        Text(
                            text = if (frame19 == null) "--" else if (currentPlanId == 0) "无" else "方案 $currentPlanId",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                    }
                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                        Text("计划执行开关", style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        Text(
                            text = if (frame19 == null) "--" else if (planEnabled == 1) "已启用" else "已禁用",
                            style = MaterialTheme.typography.bodyMedium,
                            color = if (planEnabled == 1) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface
                        )
                    }
                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                        Text("当日投喂次数", style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        Text(
                            text = if (feedCountToday == null) "--" else "$feedCountToday 次",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                    }
                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                        Text("RFID 宠物 ID", style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        Text(
                            text = if (frame19 == null) "--" else "未上送",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }
            // === END ===

            // === 修改：区域1-附加 APP运行期历史投喂数据 ===
            HomeSection(title = "历史投喂数据") {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 12.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Text(
                        text = "本次APP运行以来记录：${feedHistory.size} 条",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Button(
                        onClick = { showHistoryDialog = true },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text("查看历史投喂数据")
                    }
                }
            }
            // === END ===

            // === 修改：区域2 异常报警 ===
            HomeSection(
                title = "异常报警",
                cardContainerColor = if (frame18 != null && (alarmFlags and 0x07) != 0) {
                    MaterialTheme.colorScheme.errorContainer
                } else {
                    MaterialTheme.colorScheme.surface
                }
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 12.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    val lowFood  = frame18 != null && (alarmFlags and 0x01) != 0
                    val motorJam = frame18 != null && (alarmFlags and 0x02) != 0
                    val powerOff = frame18 != null && (alarmFlags and 0x04) != 0
                    Text(
                        text = if (frame18 == null) "--" else if (lowFood) "⚠ 食槽余量不足" else "✓ 余量正常",
                        style = MaterialTheme.typography.bodyMedium,
                        color = when {
                            frame18 == null -> MaterialTheme.colorScheme.onSurfaceVariant
                            lowFood         -> MaterialTheme.colorScheme.error
                            else            -> MaterialTheme.colorScheme.primary
                        }
                    )
                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                    Text(
                        text = if (frame18 == null) "--" else if (motorJam) "⚠ 电机卡堵故障" else "✓ 电机正常",
                        style = MaterialTheme.typography.bodyMedium,
                        color = when {
                            frame18 == null -> MaterialTheme.colorScheme.onSurfaceVariant
                            motorJam        -> MaterialTheme.colorScheme.error
                            else            -> MaterialTheme.colorScheme.primary
                        }
                    )
                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                    Text(
                        text = if (frame18 == null) "--" else if (powerOff) "⚠ 外部断电" else "✓ 供电正常",
                        style = MaterialTheme.typography.bodyMedium,
                        color = when {
                            frame18 == null -> MaterialTheme.colorScheme.onSurfaceVariant
                            powerOff        -> MaterialTheme.colorScheme.error
                            else            -> MaterialTheme.colorScheme.primary
                        }
                    )
                }
            }
            // === END ===

            // === 修改：区域3 计划执行控制 ===
            HomeSection(title = "计划执行控制") {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 12.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Text(
                        text = "当前状态：${if (frame19 == null) "--" else if (planEnabled == 1) "已启用" else "已禁用"}",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                        Button(
                            onClick = { sendCommand(cmdCode = 5) },
                            enabled = canSend && planEnabled == 0,
                            modifier = Modifier.weight(1f)
                        ) { Text("启用计划执行") }
                        Button(
                            onClick = { sendCommand(cmdCode = 6) },
                            enabled = canSend && planEnabled == 1,
                            modifier = Modifier.weight(1f)
                        ) { Text("禁用计划执行") }
                    }
                }
            }
            // === END ===

            // === 修改：区域4 手动控制 ===
            HomeSection(title = "手动控制（计划禁用时有效）") {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 12.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    val manualEnabled = canSend && planEnabled == 0
                    if (isConnected && planEnabled == null) {
                        Text(
                            text = "等待计划执行状态数据",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    if (isConnected && planEnabled == 1) {
                        Text(
                            text = "请先禁用计划执行",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                        Button(
                            onClick = { sendCommand(cmdCode = 3) },
                            enabled = manualEnabled,
                            modifier = Modifier.weight(1f)
                        ) { Text("手动开启食槽") }
                        Button(
                            onClick = { sendCommand(cmdCode = 4) },
                            enabled = manualEnabled,
                            modifier = Modifier.weight(1f)
                        ) { Text("手动关闭食槽") }
                    }
                }
            }
            // === END ===

            // === 修改：区域5 当前方案选择 ===
            HomeSection(title = "当前方案选择") {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 12.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Text(
                        text = "当前方案：${if (frame19 == null) "--" else if (currentPlanId == 0) "无" else "方案 $currentPlanId"}",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        for (n in 1..5) {
                            val isSelected = currentPlanId == n
                            if (isSelected) {
                                Button(
                                    onClick = { sendCommand(cmdCode = 7, planId = n) },
                                    enabled = canSend,
                                    modifier = Modifier.weight(1f)
                                ) { Text("方案$n") }
                            } else {
                                OutlinedButton(
                                    onClick = { sendCommand(cmdCode = 7, planId = n) },
                                    enabled = canSend,
                                    modifier = Modifier.weight(1f)
                                ) { Text("方案$n") }
                            }
                        }
                    }
                }
            }
            // === END ===

            // === 修改：区域6 报警阈值设置 ===
            HomeSection(title = "报警阈值设置") {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 12.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Text(
                        text = "当前阈值：${if (alarmThreshold == null) "--" else "${"%.1f".format(alarmThreshold)} g"}",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    val thresholds = listOf(10f, 20f, 30f, 50f, 100f, 200f)
                    val thresholdLabels = listOf("10g", "20g", "30g", "50g", "100g", "200g")
                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        for (i in 0..2) {
                            val t = thresholds[i]
                            val isSelected = alarmThreshold != null && alarmThreshold == t
                            if (isSelected) {
                                Button(
                                    onClick = { sendThreshold(t) },
                                    enabled = canSend,
                                    modifier = Modifier.weight(1f)
                                ) { Text(thresholdLabels[i]) }
                            } else {
                                OutlinedButton(
                                    onClick = { sendThreshold(t) },
                                    enabled = canSend,
                                    modifier = Modifier.weight(1f)
                                ) { Text(thresholdLabels[i]) }
                            }
                        }
                    }
                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        for (i in 3..5) {
                            val t = thresholds[i]
                            val isSelected = alarmThreshold != null && alarmThreshold == t
                            if (isSelected) {
                                Button(
                                    onClick = { sendThreshold(t) },
                                    enabled = canSend,
                                    modifier = Modifier.weight(1f)
                                ) { Text(thresholdLabels[i]) }
                            } else {
                                OutlinedButton(
                                    onClick = { sendThreshold(t) },
                                    enabled = canSend,
                                    modifier = Modifier.weight(1f)
                                ) { Text(thresholdLabels[i]) }
                            }
                        }
                    }
                }
            }
            // === END ===

            // === 修改：区域7 投喂计划管理 ===
            HomeSection(title = "投喂计划管理") {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 12.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    for (i in 0..4) {
                        val slot = planCache[i]
                        val planId = i + 1
                        if (slot.planId != 0) {
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Text(
                                    text = "方案 $planId：每 ${slot.frequencySec.toInt()}s 投喂 ${slot.weightG.toInt()}g",
                                    style = MaterialTheme.typography.bodyMedium,
                                    modifier = Modifier.weight(1f)
                                )
                                Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                                    OutlinedButton(
                                        onClick = {
                                            dialogPlanId  = planId
                                            dialogFreqSec = slot.frequencySec
                                            dialogWeightG = slot.weightG
                                            showPlanDialog = true
                                        },
                                        enabled = canSend
                                    ) { Text("修改") }
                                    Button(
                                        onClick = {
                                            sendCommand(cmdCode = 2, planId = planId) {
                                                planCache[i] = PlanSlot(0, 0f, 0f)
                                            }
                                        },
                                        enabled = canSend,
                                        colors = ButtonDefaults.buttonColors(
                                            containerColor = MaterialTheme.colorScheme.error
                                        )
                                    ) { Text("删除") }
                                }
                            }
                        } else {
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Text(
                                    text = "方案 $planId：(未配置)",
                                    style = MaterialTheme.typography.bodyMedium,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                    modifier = Modifier.weight(1f)
                                )
                                Button(
                                    onClick = {
                                        dialogPlanId  = planId
                                        dialogFreqSec = null
                                        dialogWeightG = null
                                        showPlanDialog = true
                                    },
                                    enabled = canSend
                                ) { Text("新建") }
                            }
                        }
                        if (i < 4) HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                    }
                }
            }
            // === END ===
        }
    }

    // === 修改：历史投喂数据查看对话框 ===
    if (showHistoryDialog) {
        AlertDialog(
            onDismissRequest = { showHistoryDialog = false },
            title = { Text("历史投喂数据") },
            text = {
                Column(
                    modifier = Modifier.verticalScroll(rememberScrollState()),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    if (feedHistory.isEmpty()) {
                        Text(
                            text = "本次APP运行以来暂无投喂记录",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    } else {
                        feedHistory.forEach { record ->
                            val planText = if (record.planId == 0) "方案 --" else "方案 ${record.planId}"
                            val weightText = record.weightAfterG?.let { "%.1f g".format(it) } ?: "--"
                            val timeText = record.systemSec?.let { "${it}s" } ?: "--"
                            Text(
                                text = "#${record.recordNo}  $planText  今日第${record.feedCountToday}次  余量$weightText  设备时间$timeText",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                        }
                    }
                }
            },
            confirmButton = {
                Button(onClick = { showHistoryDialog = false }) { Text("关闭") }
            }
        )
    }
    // === END ===

    // === 修改：新建/修改计划对话框 ===
    if (showPlanDialog) {
        val freqOptions = listOf(10f, 30f, 60f, 300f, 600f, 1800f, 3600f)
        val freqLabels  = listOf("10s", "30s", "60s", "300s", "600s", "1800s", "3600s")
        val wgtOptions  = listOf(10f, 20f, 30f, 50f, 100f)
        val wgtLabels   = listOf("10g", "20g", "30g", "50g", "100g")
        AlertDialog(
            onDismissRequest = { showPlanDialog = false },
            title = { Text("配置方案 $dialogPlanId") },
            text = {
                Column(
                    modifier = Modifier.verticalScroll(rememberScrollState()),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Text("选择频率：", style = MaterialTheme.typography.bodyMedium)
                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                        for (j in 0..3) {
                            val isSelected = dialogFreqSec == freqOptions[j]
                            if (isSelected) {
                                Button(onClick = { dialogFreqSec = freqOptions[j] }, modifier = Modifier.weight(1f)) {
                                    Text(freqLabels[j], style = MaterialTheme.typography.bodySmall)
                                }
                            } else {
                                OutlinedButton(onClick = { dialogFreqSec = freqOptions[j] }, modifier = Modifier.weight(1f)) {
                                    Text(freqLabels[j], style = MaterialTheme.typography.bodySmall)
                                }
                            }
                        }
                    }
                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                        for (j in 4..6) {
                            val isSelected = dialogFreqSec == freqOptions[j]
                            if (isSelected) {
                                Button(onClick = { dialogFreqSec = freqOptions[j] }, modifier = Modifier.weight(1f)) {
                                    Text(freqLabels[j], style = MaterialTheme.typography.bodySmall)
                                }
                            } else {
                                OutlinedButton(onClick = { dialogFreqSec = freqOptions[j] }, modifier = Modifier.weight(1f)) {
                                    Text(freqLabels[j], style = MaterialTheme.typography.bodySmall)
                                }
                            }
                        }
                    }
                    Text("选择重量：", style = MaterialTheme.typography.bodyMedium)
                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                        for (j in wgtOptions.indices) {
                            val isSelected = dialogWeightG == wgtOptions[j]
                            if (isSelected) {
                                Button(onClick = { dialogWeightG = wgtOptions[j] }, modifier = Modifier.weight(1f)) {
                                    Text(wgtLabels[j], style = MaterialTheme.typography.bodySmall)
                                }
                            } else {
                                OutlinedButton(onClick = { dialogWeightG = wgtOptions[j] }, modifier = Modifier.weight(1f)) {
                                    Text(wgtLabels[j], style = MaterialTheme.typography.bodySmall)
                                }
                            }
                        }
                    }
                }
            },
            confirmButton = {
                Button(
                    onClick = {
                        val freq   = dialogFreqSec
                        val weight = dialogWeightG
                        if (freq != null && weight != null) {
                            sendCommand(cmdCode = 1, planId = dialogPlanId, freqSec = freq, weightG = weight) {
                                planCache[dialogPlanId - 1] = PlanSlot(dialogPlanId, freq, weight)
                            }
                        }
                        showPlanDialog = false
                    },
                    enabled = dialogFreqSec != null && dialogWeightG != null
                ) { Text("确认") }
            },
            dismissButton = {
                OutlinedButton(onClick = { showPlanDialog = false }) { Text("取消") }
            }
        )
    }
    // === END ===

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
