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
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import com.example.demo_1.ui.theme.Demo_1Theme
import java.util.Locale
import kotlin.random.Random

// === Non-Developer Mode UI Color Palette ===
private val StudentModeScaffoldBackground = Color(0xFFF0F4FF)
private val StudentModeSurface = Color(0xFFFFFFFF)
private val StudentModeHeaderBg = Color(0xFF1E3A5F)
private val StudentModeHeaderText = Color(0xFFFFFFFF)
private val StudentModeTitle = Color(0xFF1E3A5F)
private val StudentModeSubtitle = Color(0xFF607D8B)
private val StudentModeBorder = Color(0xFFDDE4F0)
private val StudentModeAccent = Color(0xFF2979FF)
private val StudentModeConnectionCardBg = Color(0xFFF8FAFF)
private val StudentModeOnlineChipBg = Color(0xFFE6FFF4)
private val StudentModeOnlineChipText = Color(0xFF00875A)
private val StudentModeOfflineChipBg = Color(0xFFFFF3E8)
private val StudentModeOfflineChipText = Color(0xFFCC5500)
private val StudentModeSignalChipBg = Color(0xFFE8F0FE)
private val StudentModeSignalChipText = Color(0xFF1A56DB)
private val StudentModeConnectedColor = Color(0xFF00C853)
private val StudentModeInfoBg = Color(0xFFF0F4FF)

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
                val useStudentStyle = !UserConfig.DEVELOPER_MODE
                Scaffold(
                    modifier = Modifier.fillMaxSize(),
                    containerColor = if (useStudentStyle) {
                        StudentModeScaffoldBackground
                    } else {
                        MaterialTheme.colorScheme.background
                    },
                    topBar = {
                        TopAppBar(
                            title = {
                                if (useStudentStyle) {
                                    Column(verticalArrangement = Arrangement.spacedBy(1.dp)) {
                                        Text(
                                            text = UserConfig.project_name,
                                            style = MaterialTheme.typography.titleMedium,
                                            fontWeight = FontWeight.Bold,
                                            color = StudentModeHeaderText
                                        )
                                        Text(
                                            text = UserConfig.author_name,
                                            style = MaterialTheme.typography.labelSmall,
                                            color = StudentModeHeaderText.copy(alpha = 0.55f)
                                        )
                                    }
                                } else {
                                    Text(text = UserConfig.project_name)
                                }
                            },
                            colors = if (useStudentStyle) {
                                TopAppBarDefaults.topAppBarColors(
                                    containerColor = StudentModeHeaderBg,
                                    titleContentColor = StudentModeHeaderText
                                )
                            } else {
                                TopAppBarDefaults.topAppBarColors()
                            }
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
                        } else {
                            Surface(
                                color = StudentModeHeaderBg,
                                tonalElevation = 0.dp
                            ) {
                                Text(
                                    text = UserConfig.author_name,
                                    style = MaterialTheme.typography.labelSmall,
                                    color = StudentModeHeaderText.copy(alpha = 0.45f),
                                    textAlign = TextAlign.Center,
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .padding(vertical = 8.dp)
                                )
                            }
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
    val mcuTimeSeconds = UserConfig.mcu_time
    var txWriteStatus by remember { mutableStateOf("等待发送") }
    val useStudentStyle = !UserConfig.DEVELOPER_MODE
    val actionButtonShape = if (useStudentStyle) {
        RoundedCornerShape(12.dp)
    } else {
        ButtonDefaults.shape
    }
    val actionButtonColors = if (useStudentStyle) {
        ButtonDefaults.buttonColors(
            containerColor = StudentModeAccent,
            contentColor = Color.White,
            disabledContainerColor = Color(0xFFCDD5E0),
            disabledContentColor = Color(0xFF8896A7)
        )
    } else {
        ButtonDefaults.buttonColors()
    }
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
                .padding(
                    horizontal = if (useStudentStyle) 18.dp else 16.dp,
                    vertical = if (useStudentStyle) 20.dp else 12.dp
                ),
            verticalArrangement = Arrangement.spacedBy(if (useStudentStyle) 20.dp else 16.dp)
        ) {
            // ── Section 1: Connection Status ──────────────────────────────────
            HomeSection(
                title = if (useStudentStyle) "连接状态" else "BLE连接状态",
                cardContainerColor = if (useStudentStyle) {
                    StudentModeConnectionCardBg
                } else {
                    MaterialTheme.colorScheme.surfaceVariant
                },
                studentStyleEnabled = useStudentStyle
            ) {
                if (useStudentStyle) {
                    // Non-dev mode: accent strip + structured content
                    Column {
                        // Top accent color strip
                        Box(
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(5.dp)
                                .background(
                                    if (connectedDevice != null) StudentModeConnectedColor
                                    else StudentModeOfflineChipText
                                )
                        )
                        Column(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(horizontal = 18.dp, vertical = 16.dp),
                            verticalArrangement = Arrangement.spacedBy(12.dp)
                        ) {
                            if (connectedDevice == null) {
                                // Disconnected state
                                Row(
                                    verticalAlignment = Alignment.CenterVertically,
                                    horizontalArrangement = Arrangement.spacedBy(14.dp)
                                ) {
                                    // Status circle indicator
                                    Box(
                                        modifier = Modifier
                                            .size(48.dp)
                                            .background(StudentModeOfflineChipBg, CircleShape),
                                        contentAlignment = Alignment.Center
                                    ) {
                                        Box(
                                            modifier = Modifier
                                                .size(14.dp)
                                                .background(StudentModeOfflineChipText, CircleShape)
                                        )
                                    }
                                    Column(verticalArrangement = Arrangement.spacedBy(3.dp)) {
                                        Text(
                                            text = "未连接",
                                            style = MaterialTheme.typography.titleLarge,
                                            fontWeight = FontWeight.Bold,
                                            color = StudentModeOfflineChipText
                                        )
                                        Text(
                                            text = "暂无已连接的 BLE 设备",
                                            style = MaterialTheme.typography.bodySmall,
                                            color = StudentModeSubtitle
                                        )
                                    }
                                }
                                Surface(
                                    shape = RoundedCornerShape(10.dp),
                                    color = StudentModeInfoBg
                                ) {
                                    Text(
                                        text = "下拉页面可触发扫描，并自动尝试连接已配置设备。",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = StudentModeSubtitle,
                                        modifier = Modifier
                                            .fillMaxWidth()
                                            .padding(horizontal = 14.dp, vertical = 10.dp)
                                    )
                                }
                            } else {
                                // Connected state
                                Row(
                                    modifier = Modifier.fillMaxWidth(),
                                    horizontalArrangement = Arrangement.SpaceBetween,
                                    verticalAlignment = Alignment.CenterVertically
                                ) {
                                    Row(
                                        verticalAlignment = Alignment.CenterVertically,
                                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                                    ) {
                                        Box(
                                            modifier = Modifier
                                                .size(10.dp)
                                                .background(StudentModeConnectedColor, CircleShape)
                                        )
                                        Text(
                                            text = "设备在线",
                                            style = MaterialTheme.typography.titleSmall,
                                            fontWeight = FontWeight.Bold,
                                            color = StudentModeOnlineChipText
                                        )
                                    }
                                    Surface(
                                        shape = RoundedCornerShape(50),
                                        color = StudentModeSignalChipBg,
                                        border = BorderStroke(1.dp, Color(0xFFD0E0FF))
                                    ) {
                                        Text(
                                            text = "${connectedDevice.rssi} dBm",
                                            style = MaterialTheme.typography.labelMedium,
                                            color = StudentModeSignalChipText,
                                            modifier = Modifier.padding(horizontal = 10.dp, vertical = 5.dp)
                                        )
                                    }
                                }
                                // Divider
                                Box(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .height(1.dp)
                                        .background(StudentModeBorder)
                                )
                                Text(
                                    text = connectedDevice.name,
                                    style = MaterialTheme.typography.titleLarge,
                                    fontWeight = FontWeight.Bold,
                                    color = StudentModeTitle
                                )
                                Row(
                                    modifier = Modifier.fillMaxWidth(),
                                    horizontalArrangement = Arrangement.SpaceBetween
                                ) {
                                    Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                                        Text(
                                            text = "MAC 地址",
                                            style = MaterialTheme.typography.labelSmall,
                                            color = StudentModeSubtitle
                                        )
                                        Text(
                                            text = connectedDevice.address,
                                            style = MaterialTheme.typography.bodySmall,
                                            color = StudentModeTitle
                                        )
                                    }
                                    Column(
                                        verticalArrangement = Arrangement.spacedBy(2.dp),
                                        horizontalAlignment = Alignment.End
                                    ) {
                                        Text(
                                            text = "运行时长",
                                            style = MaterialTheme.typography.labelSmall,
                                            color = StudentModeSubtitle
                                        )
                                        Text(
                                            text = "${mcuTimeSeconds} s",
                                            style = MaterialTheme.typography.bodySmall,
                                            color = StudentModeTitle
                                        )
                                    }
                                }
                                Surface(
                                    shape = RoundedCornerShape(8.dp),
                                    color = StudentModeOnlineChipBg
                                ) {
                                    Text(
                                        text = "连接稳定，可在下方进行数据发送操作。",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = StudentModeOnlineChipText,
                                        modifier = Modifier
                                            .fillMaxWidth()
                                            .padding(horizontal = 14.dp, vertical = 8.dp)
                                    )
                                }
                            }
                        }
                    }
                } else {
                    // Dev mode: original layout
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 18.dp, vertical = 16.dp),
                        verticalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        if (connectedDevice == null) {
                            Text(
                                text = "未连接",
                                style = MaterialTheme.typography.titleMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                            Text(
                                text = "当前没有连接设备，请前往 BLE 搜索页面连接设备。",
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
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween
                            ) {
                                Text(
                                    text = "MAC：${connectedDevice.address}",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                                Text(
                                    text = "${mcuTimeSeconds}s",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                        }
                    }
                }
            }

            // ── Section 2: TX Controls ────────────────────────────────────────
            HomeSection(
                title = if (useStudentStyle) "数据发送" else "更多功能",
                studentStyleEnabled = useStudentStyle
            ) {
                if (useStudentStyle) {
                    // Non-dev mode: 2×2 grid layout
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 18.dp, vertical = 16.dp),
                        verticalArrangement = Arrangement.spacedBy(10.dp)
                    ) {
                        Text(
                            text = "TX 写入验证",
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.Bold,
                            color = StudentModeTitle
                        )
                        Text(
                            text = "TX1 → 服务1特征1   TX2/3/4 → 服务2特征1/2/3",
                            style = MaterialTheme.typography.bodySmall,
                            color = StudentModeSubtitle
                        )

                        Spacer(modifier = Modifier.height(2.dp))

                        // Fill test data - full width
                        Button(
                            onClick = {
                                val random = Random(System.currentTimeMillis())
                                UserConfig.tx_data_stream_1 = ByteArray(16).also { bytes ->
                                    random.nextBytes(bytes)
                                }
                                UserConfig.tx_data_stream_2 = IntArray(4) { random.nextInt(0, 100_000) }
                                UserConfig.tx_data_stream_3 = IntArray(4) { random.nextInt(0, 100_000) }
                                UserConfig.tx_data_stream_4 = IntArray(4) { random.nextInt(0, 100_000) }
                                txWriteStatus = "已随机填充测试数据"
                            },
                            modifier = Modifier.fillMaxWidth(),
                            shape = actionButtonShape,
                            colors = actionButtonColors
                        ) {
                            Text(text = "填充测试数据")
                        }

                        // Divider
                        Box(
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(1.dp)
                                .background(StudentModeBorder)
                        )

                        // Row 1: TX1 + TX2
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(10.dp)
                        ) {
                            Button(
                                onClick = {
                                    txWriteStatus = writeTxStream(
                                        label = "TX1",
                                        serviceUuid = UserConfig.esp32_service_1_uuid,
                                        characteristicUuid = UserConfig.esp32_service_1_characteristic_1_uuid,
                                        payload = UserConfig.tx_data_stream_1.copyOf(16)
                                    )
                                },
                                enabled = connectedDevice != null,
                                modifier = Modifier.weight(1f),
                                shape = actionButtonShape,
                                colors = actionButtonColors
                            ) {
                                Text(text = "发送 TX 1")
                            }
                            Button(
                                onClick = {
                                    txWriteStatus = writeTxStream(
                                        label = "TX2",
                                        serviceUuid = UserConfig.esp32_service_2_uuid,
                                        characteristicUuid = UserConfig.esp32_service_2_characteristic_1_uuid,
                                        payload = UserConfig.tx_data_stream_2.toBigEndianByteArray()
                                    )
                                },
                                enabled = connectedDevice != null,
                                modifier = Modifier.weight(1f),
                                shape = actionButtonShape,
                                colors = actionButtonColors
                            ) {
                                Text(text = "发送 TX 2")
                            }
                        }

                        // Row 2: TX3 + TX4
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(10.dp)
                        ) {
                            Button(
                                onClick = {
                                    txWriteStatus = writeTxStream(
                                        label = "TX3",
                                        serviceUuid = UserConfig.esp32_service_2_uuid,
                                        characteristicUuid = UserConfig.esp32_service_2_characteristic_2_uuid,
                                        payload = UserConfig.tx_data_stream_3.toBigEndianByteArray()
                                    )
                                },
                                enabled = connectedDevice != null,
                                modifier = Modifier.weight(1f),
                                shape = actionButtonShape,
                                colors = actionButtonColors
                            ) {
                                Text(text = "发送 TX 3")
                            }
                            Button(
                                onClick = {
                                    txWriteStatus = writeTxStream(
                                        label = "TX4",
                                        serviceUuid = UserConfig.esp32_service_2_uuid,
                                        characteristicUuid = UserConfig.esp32_service_2_characteristic_3_uuid,
                                        payload = UserConfig.tx_data_stream_4.toBigEndianByteArray()
                                    )
                                },
                                enabled = connectedDevice != null,
                                modifier = Modifier.weight(1f),
                                shape = actionButtonShape,
                                colors = actionButtonColors
                            ) {
                                Text(text = "发送 TX 4")
                            }
                        }

                        // Divider
                        Box(
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(1.dp)
                                .background(StudentModeBorder)
                        )

                        // Compact TX data preview
                        Surface(
                            shape = RoundedCornerShape(10.dp),
                            color = StudentModeInfoBg
                        ) {
                            Column(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(horizontal = 14.dp, vertical = 10.dp),
                                verticalArrangement = Arrangement.spacedBy(3.dp)
                            ) {
                                Text(
                                    text = "TX1: ${UserConfig.tx_data_stream_1.toHexString()}",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = StudentModeSubtitle
                                )
                                Text(
                                    text = "TX2: ${UserConfig.tx_data_stream_2.joinToString()}",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = StudentModeSubtitle
                                )
                                Text(
                                    text = "TX3: ${UserConfig.tx_data_stream_3.joinToString()}",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = StudentModeSubtitle
                                )
                                Text(
                                    text = "TX4: ${UserConfig.tx_data_stream_4.joinToString()}",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = StudentModeSubtitle
                                )
                            }
                        }

                        Text(
                            text = "状态：$txWriteStatus",
                            style = MaterialTheme.typography.bodySmall,
                            fontWeight = FontWeight.Medium,
                            color = StudentModeAccent
                        )
                    }
                } else {
                    // Dev mode: original full-width stacked layout
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 16.dp, vertical = 12.dp),
                        verticalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Text(
                            text = "TX写入验证",
                            style = MaterialTheme.typography.titleMedium,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                        Text(
                            text = "tx_data_stream_1 -> 服务1特征1，tx_data_stream_2/3/4 -> 服务2特征1/2/3",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )

                        Button(
                            onClick = {
                                val random = Random(System.currentTimeMillis())
                                UserConfig.tx_data_stream_1 = ByteArray(16).also { bytes ->
                                    random.nextBytes(bytes)
                                }
                                UserConfig.tx_data_stream_2 = IntArray(4) { random.nextInt(0, 100_000) }
                                UserConfig.tx_data_stream_3 = IntArray(4) { random.nextInt(0, 100_000) }
                                UserConfig.tx_data_stream_4 = IntArray(4) { random.nextInt(0, 100_000) }
                                txWriteStatus = "已随机填充测试数据"
                            },
                            modifier = Modifier.fillMaxWidth(),
                            shape = actionButtonShape,
                            colors = actionButtonColors
                        ) {
                            Text(text = "填充测试数据")
                        }

                        Text(
                            text = "TX1: ${UserConfig.tx_data_stream_1.toHexString()}",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Text(
                            text = "TX2: ${UserConfig.tx_data_stream_2.joinToString()}",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Text(
                            text = "TX3: ${UserConfig.tx_data_stream_3.joinToString()}",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        Text(
                            text = "TX4: ${UserConfig.tx_data_stream_4.joinToString()}",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )

                        Button(
                            onClick = {
                                txWriteStatus = writeTxStream(
                                    label = "TX1",
                                    serviceUuid = UserConfig.esp32_service_1_uuid,
                                    characteristicUuid = UserConfig.esp32_service_1_characteristic_1_uuid,
                                    payload = UserConfig.tx_data_stream_1.copyOf(16)
                                )
                            },
                            enabled = connectedDevice != null,
                            modifier = Modifier.fillMaxWidth(),
                            shape = actionButtonShape,
                            colors = actionButtonColors
                        ) {
                            Text(text = "发送 tx_data_stream_1")
                        }

                        Button(
                            onClick = {
                                txWriteStatus = writeTxStream(
                                    label = "TX2",
                                    serviceUuid = UserConfig.esp32_service_2_uuid,
                                    characteristicUuid = UserConfig.esp32_service_2_characteristic_1_uuid,
                                    payload = UserConfig.tx_data_stream_2.toBigEndianByteArray()
                                )
                            },
                            enabled = connectedDevice != null,
                            modifier = Modifier.fillMaxWidth(),
                            shape = actionButtonShape,
                            colors = actionButtonColors
                        ) {
                            Text(text = "发送 tx_data_stream_2")
                        }

                        Button(
                            onClick = {
                                txWriteStatus = writeTxStream(
                                    label = "TX3",
                                    serviceUuid = UserConfig.esp32_service_2_uuid,
                                    characteristicUuid = UserConfig.esp32_service_2_characteristic_2_uuid,
                                    payload = UserConfig.tx_data_stream_3.toBigEndianByteArray()
                                )
                            },
                            enabled = connectedDevice != null,
                            modifier = Modifier.fillMaxWidth(),
                            shape = actionButtonShape,
                            colors = actionButtonColors
                        ) {
                            Text(text = "发送 tx_data_stream_3")
                        }

                        Button(
                            onClick = {
                                txWriteStatus = writeTxStream(
                                    label = "TX4",
                                    serviceUuid = UserConfig.esp32_service_2_uuid,
                                    characteristicUuid = UserConfig.esp32_service_2_characteristic_3_uuid,
                                    payload = UserConfig.tx_data_stream_4.toBigEndianByteArray()
                                )
                            },
                            enabled = connectedDevice != null,
                            modifier = Modifier.fillMaxWidth(),
                            shape = actionButtonShape,
                            colors = actionButtonColors
                        ) {
                            Text(text = "发送 tx_data_stream_4")
                        }

                        Text(
                            text = "发送状态：$txWriteStatus",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
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

private fun writeTxStream(
    label: String,
    serviceUuid: String,
    characteristicUuid: String?,
    payload: ByteArray
): String {
    val serviceUuidValue = serviceUuid.trim()
    val characteristicUuidValue = characteristicUuid?.trim().orEmpty()
    if (serviceUuidValue.isEmpty() || characteristicUuidValue.isEmpty()) {
        return "$label 发送失败：UUID 未配置"
    }
    val sent = BleConnectionManager.writeCharacteristic(
        serviceUuid = serviceUuidValue,
        characteristicUuid = characteristicUuidValue,
        value = payload
    )
    return if (sent) {
        BleConnectionManager.recordOutgoingMessage(
            characteristicUuid = characteristicUuidValue,
            value = payload
        )
        "$label 发送成功（${payload.size}字节）"
    } else {
        "$label 发送失败"
    }
}

private fun IntArray.toBigEndianByteArray(): ByteArray {
    val source = IntArray(4)
    repeat(4) { index ->
        source[index] = this.getOrNull(index) ?: 0
    }
    val payload = ByteArray(16)
    source.forEachIndexed { index, value ->
        val offset = index * 4
        payload[offset] = ((value ushr 24) and 0xFF).toByte()
        payload[offset + 1] = ((value ushr 16) and 0xFF).toByte()
        payload[offset + 2] = ((value ushr 8) and 0xFF).toByte()
        payload[offset + 3] = (value and 0xFF).toByte()
    }
    return payload
}

private fun ByteArray.toHexString(): String {
    if (isEmpty()) return ""
    return joinToString(separator = " ") { byte ->
        "%02X".format(Locale.US, byte)
    }
}

@Composable
private fun HomeSection(
    title: String,
    cardContainerColor: Color = MaterialTheme.colorScheme.surface,
    studentStyleEnabled: Boolean = false,
    content: @Composable () -> Unit
) {
    val sectionShape = RoundedCornerShape(20.dp)
    val sectionContainerColor = if (studentStyleEnabled) {
        StudentModeSurface
    } else {
        cardContainerColor
    }
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text(
            text = title,
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = if (studentStyleEnabled) FontWeight.SemiBold else FontWeight.Normal,
            color = if (studentStyleEnabled) {
                StudentModeTitle
            } else {
                MaterialTheme.colorScheme.onSurfaceVariant
            },
            modifier = Modifier.padding(horizontal = 4.dp)
        )
        Card(
            modifier = Modifier.fillMaxWidth(),
            shape = sectionShape,
            colors = CardDefaults.cardColors(
                containerColor = sectionContainerColor
            ),
            border = if (studentStyleEnabled) BorderStroke(1.dp, StudentModeBorder) else null,
            elevation = CardDefaults.cardElevation(
                defaultElevation = if (studentStyleEnabled) 3.dp else 0.dp
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
