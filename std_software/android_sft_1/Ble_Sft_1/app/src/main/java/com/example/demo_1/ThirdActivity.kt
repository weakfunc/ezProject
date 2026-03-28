package com.example.demo_1

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
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
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import com.example.demo_1.ui.theme.Demo_1Theme
import java.util.Locale
import kotlin.random.Random

@OptIn(ExperimentalMaterial3Api::class)
class ThirdActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        val previousTab = previousTabFromIntent()
        setContent {
            Demo_1Theme {
                Scaffold(
                    modifier = Modifier.fillMaxSize(),
                    topBar = {
                        TopAppBar(title = { Text("调试") })
                    },
                    bottomBar = {
                        AppBottomNavigation(
                            currentTab = AppTab.Debug,
                            previousTab = previousTab,
                            onTabSelected = { tab ->
                                navigateToTab(AppTab.Debug, tab)
                            }
                        )
                    }
                ) { innerPadding ->
                    ThirdPage(modifier = Modifier.padding(innerPadding))
                }
            }
        }
    }
}

@Composable
fun ThirdPage(modifier: Modifier = Modifier) {
    val connectedDevice = BleConnectionManager.connectedDeviceInfo
    val isConnected = connectedDevice != null

    var txCmd    by remember { mutableStateOf(BleProtocol.CMD_TX_MIN) }
    var tx4Var1  by remember { mutableStateOf(0) }
    var tx4Var2  by remember { mutableStateOf(0) }
    var tx1Var1  by remember { mutableStateOf(0) }
    var tx1Var2  by remember { mutableStateOf(0) }
    var txStatus    by remember { mutableStateOf("等待发送") }
    var rawSendText by remember { mutableStateOf("") }

    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {

        // ── BLE 连接状态 ──────────────────────────────────────────────────────
        DebugSection(
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
                        text = "请先在 BLE 搜索页面连接设备",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                } else {
                    Text(
                        text = "已连接",
                        style = MaterialTheme.typography.titleMedium,
                        color = MaterialTheme.colorScheme.primary
                    )
                    ProtoFieldRow(
                        label1 = "设备", value1 = connectedDevice.name,
                        label2 = "RSSI", value2 = "${connectedDevice.rssi} dBm"
                    )
                    Text(
                        text = connectedDevice.address,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        fontFamily = FontFamily.Monospace
                    )
                }
            }
        }

        // ── RX 接收数据（服务1 特征1）────────────────────────────────────────
        DebugSection(title = "RX  服务1 特征1（上次收到）") {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 12.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                ProtoFieldRow(
                    label1 = "CMD",  value1 = "0x%02X".format(Locale.US, BleProtocol.rx_cmd),
                    label2 = "CNT",  value2 = "${BleProtocol.rx_cnt}"
                )
                ProtoFieldRow(
                    label1 = "4B_1", value1 = BleProtocol.rx_4byteVar_1.toByteHex(),
                    label2 = "4B_2", value2 = BleProtocol.rx_4byteVar_2.toByteHex()
                )
                ProtoFieldRow(
                    label1 = "1B_1", value1 = "0x%02X".format(Locale.US, BleProtocol.rx_1byteVar_1),
                    label2 = "1B_2", value2 = "0x%02X".format(Locale.US, BleProtocol.rx_1byteVar_2)
                )
            }
        }

        // ── TX 发送数据（服务1 特征1）────────────────────────────────────────
        DebugSection(title = "TX  服务1 特征1（发送）") {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 12.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                ProtoFieldRow(
                    label1 = "CMD",  value1 = "0x%02X".format(Locale.US, txCmd),
                    label2 = "CNT",  value2 = "自动递增"
                )
                ProtoFieldRow(
                    label1 = "4B_1", value1 = tx4Var1.toByteHex(),
                    label2 = "4B_2", value2 = tx4Var2.toByteHex()
                )
                ProtoFieldRow(
                    label1 = "1B_1", value1 = "0x%02X".format(Locale.US, tx1Var1),
                    label2 = "1B_2", value2 = "0x%02X".format(Locale.US, tx1Var2)
                )
                HorizontalDivider(modifier = Modifier.padding(vertical = 2.dp))
                Button(
                    onClick = {
                        val rng = Random(System.currentTimeMillis())
                        txCmd   = BleProtocol.CMD_TX_MIN + rng.nextInt(BleProtocol.CMD_TX_MAX - BleProtocol.CMD_TX_MIN + 1)
                        tx4Var1 = rng.nextInt()
                        tx4Var2 = rng.nextInt()
                        tx1Var1 = rng.nextInt(256)
                        tx1Var2 = rng.nextInt(256)
                        txStatus = "已随机填充"
                    },
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text("随机填充")
                }
                Button(
                    onClick = {
                        val frame = BleProtocol.buildTxFrame(
                            cmd    = txCmd,
                            var4_1 = tx4Var1,
                            var4_2 = tx4Var2,
                            var1_1 = tx1Var1,
                            var1_2 = tx1Var2
                        )
                        val sent = BleConnectionManager.writeCharacteristic(
                            serviceUuid        = UserConfig.esp32_service_1_uuid,
                            characteristicUuid = UserConfig.esp32_service_1_characteristic_1_uuid,
                            value              = frame
                        )
                        txStatus = if (sent) {
                            BleConnectionManager.recordOutgoingMessage(
                                characteristicUuid = UserConfig.esp32_service_1_characteristic_1_uuid,
                                value              = frame
                            )
                            "发送成功（${frame.size} 字节）"
                        } else {
                            "发送失败"
                        }
                    },
                    enabled = isConnected,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text("发送")
                }
                Text(
                    text = "状态：$txStatus",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }

        // ── 数据终端 ──────────────────────────────────────────────────────────
        DebugSection(title = "数据终端") {
            Column {
                TerminalOutputWindow(
                    entries = BleConnectionManager.terminalEntries,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(200.dp)
                )
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 12.dp, vertical = 8.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    TextField(
                        value = rawSendText,
                        onValueChange = { rawSendText = it },
                        modifier = Modifier.weight(1f),
                        singleLine = true,
                        label = { Text("输入发送内容") }
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Button(
                        onClick = {
                            val bytes = rawSendText.toByteArray(Charsets.UTF_8)
                            val sent = BleConnectionManager.writeCharacteristic(
                                serviceUuid        = UserConfig.esp32_service_1_uuid,
                                characteristicUuid = UserConfig.esp32_service_1_characteristic_1_uuid,
                                value              = bytes
                            )
                            if (sent) {
                                BleConnectionManager.recordOutgoingMessage(
                                    characteristicUuid = UserConfig.esp32_service_1_characteristic_1_uuid,
                                    value              = bytes
                                )
                                rawSendText = ""
                            }
                        },
                        enabled = isConnected && rawSendText.isNotBlank()
                    ) {
                        Text("发送")
                    }
                }
            }
        }
    }
}

/** Int → "0xAA 0xBB 0xCC 0xDD" 大端序逐字节十六进制 */
private fun Int.toByteHex(): String = "0x%02X 0x%02X 0x%02X 0x%02X".format(
    Locale.US,
    (this ushr 24) and 0xFF,
    (this ushr 16) and 0xFF,
    (this ushr  8) and 0xFF,
     this          and 0xFF
)

/**
 * 两列对齐字段行：左右各一个 label + value，label 固定宽度保证纵向对齐。
 */
@Composable
private fun ProtoFieldRow(
    label1: String, value1: String,
    label2: String, value2: String
) {
    Row(modifier = Modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier.weight(1f),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = label1,
                modifier = Modifier.width(40.dp),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Text(
                text = value1,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurface,
                fontFamily = FontFamily.Monospace
            )
        }
        Row(
            modifier = Modifier.weight(1f),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = label2,
                modifier = Modifier.width(40.dp),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Text(
                text = value2,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurface,
                fontFamily = FontFamily.Monospace
            )
        }
    }
}

@Composable
private fun DebugSection(
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
            colors = CardDefaults.cardColors(containerColor = cardContainerColor)
        ) {
            content()
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
                    text = "暂无数据，收发数据后将在此显示。",
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
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                items(items = entries, key = { it.id }) { entry ->
                    val shortUuid = entry.characteristicUuid.take(8)
                    val lineText = buildAnnotatedString {
                        withStyle(SpanStyle(color = Color(0xFF1565C0))) { append(entry.direction) }
                        append("  ")
                        withStyle(SpanStyle(color = Color(0xFF2E7D32))) { append(shortUuid) }
                        append("  ")
                        withStyle(SpanStyle(color = Color(0xFF6D4C41))) { append(entry.payloadText) }
                    }
                    Text(text = lineText, style = MaterialTheme.typography.bodySmall,
                        fontFamily = FontFamily.Monospace)
                }
            }
        }
    }
}

@Preview(showBackground = true)
@Composable
fun ThirdPagePreview() {
    Demo_1Theme {
        ThirdPage()
    }
}
