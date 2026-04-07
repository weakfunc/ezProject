package com.example.demo_1

import android.annotation.SuppressLint
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.content.Context
import android.os.Handler
import android.os.Looper

/**
 * 后台自动重连管理器。
 *
 * 每 [CHECK_INTERVAL_MS] 检查一次连接状态；若未连接，
 * 启动一次时长 [SCAN_DURATION_MS] 的 BLE 扫描，
 * 以设备名 [UserConfig.esp32_device_name] 过滤，
 * 选取 RSSI 最强（值最大）的候选后发起连接。
 */
object BleAutoConnector {

    private const val CHECK_INTERVAL_MS = 2000L
    private const val SCAN_DURATION_MS  = 1200L

    private lateinit var appContext: Context
    private val handler = Handler(Looper.getMainLooper())
    private var isScanning = false

    private val periodicCheck = object : Runnable {
        override fun run() {
            tryAutoConnect()
            handler.postDelayed(this, CHECK_INTERVAL_MS)
        }
    }

    /** 在 Application.onCreate() 中调用一次 */
    fun init(context: Context) {
        appContext = context.applicationContext
        handler.post(periodicCheck)
    }

    @SuppressLint("MissingPermission")
    private fun tryAutoConnect() {
        if (!UserConfig.auto_connect_my_ble_device) return
        if (BleConnectionManager.connectedDeviceInfo != null) return
        if (BleConnectionManager.deviceStates.values.any {
                it == BleDeviceConnectionStatus.Connecting
            }) return
        if (isScanning) return

        val adapter = (appContext.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)
            ?.adapter ?: return
        if (!adapter.isEnabled) return
        val scanner = adapter.bluetoothLeScanner ?: return

        val targetName = UserConfig.esp32_device_name.trim()
        if (targetName.isEmpty()) return

        var bestResult: ScanResult? = null

        val callback = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                val name = try { result.device.name } catch (_: SecurityException) { null } ?: return
                if (!name.equals(targetName, ignoreCase = true)) return
                val best = bestResult
                if (best == null || result.rssi > best.rssi) {
                    bestResult = result
                }
            }

            override fun onBatchScanResults(results: MutableList<ScanResult>) {
                results.forEach { onScanResult(0, it) }
            }
        }

        try {
            scanner.startScan(callback)
            isScanning = true
        } catch (_: SecurityException) {
            return
        }

        handler.postDelayed({
            if (isScanning) {
                try { scanner.stopScan(callback) } catch (_: SecurityException) {}
                isScanning = false
            }

            val found = bestResult ?: return@postDelayed
            if (BleConnectionManager.connectedDeviceInfo != null) return@postDelayed
            if (BleConnectionManager.deviceStates.values.any {
                    it == BleDeviceConnectionStatus.Connecting
                }) return@postDelayed

            val deviceName = try { found.device.name } catch (_: SecurityException) { null }
                ?: targetName

            BleConnectionManager.connect(
                context    = appContext,
                device     = found.device,
                deviceName = deviceName,
                rssi       = found.rssi
            )
        }, SCAN_DURATION_MS)
    }
}
