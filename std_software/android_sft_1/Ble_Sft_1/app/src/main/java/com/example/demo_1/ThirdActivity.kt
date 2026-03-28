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
import androidx.compose.material3.ButtonDefaults
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
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import com.example.demo_1.ui.theme.Demo_1Theme
import java.util.Locale

private enum class TerminalDirectionFilter(val direction: String) {
    Tx("TX"),
    Rx("RX")
}

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

    var txCmd       by remember { mutableStateOf(BleProtocol.CMD_TX_MIN) }
    var tx4b1Text   by remember { mutableStateOf("00000000") }
    var tx4b2Text   by remember { mutableStateOf("00000000") }
    var tx1b1Text   by remember { mutableStateOf("00") }
    var tx1b2Text   by remember { mutableStateOf("00") }
    var txStatus    by remember { mutableStateOf("等待发送") }
    var rawSendText by remember { mutableStateOf("") }
    var terminalDirectionFilter by remember { mutableStateOf(TerminalDirectionFilter.Tx) }

    val filteredTerminalEntries = BleConnectionManager.terminalEntries.filter { entry ->
        entry.direction == terminalDirectionFilter.direction
    }

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

        // ── RX 接收数据（服务1 特征1，每个 CMD 独立显示）────────────────────
        DebugSection(title = "RX  服务1 特征1") {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 12.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                if (BleProtocol.rxFrames.isEmpty()) {
                    Text(
                        text = "暂未收到数据",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                } else {
                    BleProtocol.rxFrames.keys.sorted().forEachIndexed { index, cmd ->
                        val frame = BleProtocol.rxFrames[cmd] ?: return@forEachIndexed
                        if (index > 0) HorizontalDivider(modifier = Modifier.padding(vertical = 2.dp))
                        Text(
                            text = "CMD 0x%02X   CNT %d".format(Locale.US, cmd, frame.cnt),
                            style = MaterialTheme.typography.labelMedium,
                            color = MaterialTheme.colorScheme.primary
                        )
                        ProtoFieldRow(
                            label1 = "4B_1", value1 = frame.var4b1.toByteHex(),
                            label2 = "4B_2", value2 = frame.var4b2.toByteHex()
                        )
                        ProtoFieldRow(
                            label1 = "1B_1", value1 = "0x%02X".format(Locale.US, frame.var1b1),
                            label2 = "1B_2", value2 = "0x%02X".format(Locale.US, frame.var1b2)
                        )
                    }
                }
            }
        }

        // ── TX 发送数据（服务1 特征1）────────────────────────────────────────
        DebugSection(title = "TX  服务1 特征1（发送）") {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 12.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                // CMD 选择
                Text(
                    text = "CMD",
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    for (cmd in BleProtocol.CMD_TX_MIN..BleProtocol.CMD_TX_MAX) {
                        val selected = cmd == txCmd
                        Button(
                            onClick = { txCmd = cmd },
                            modifier = Modifier.weight(1f),
                            colors = ButtonDefaults.buttonColors(
                                containerColor = if (selected) {
                                    MaterialTheme.colorScheme.primary
                                } else {
                                    MaterialTheme.colorScheme.surfaceVariant
                                },
                                contentColor = if (selected) {
                                    MaterialTheme.colorScheme.onPrimary
                                } else {
                                    MaterialTheme.colorScheme.onSurfaceVariant
                                }
                            )
                        ) {
                            Text(
                                text = "0x%02X".format(Locale.US, cmd),
                                fontFamily = FontFamily.Monospace
                            )
                        }
                    }
                }

                // 4B_1 输入
                TextField(
                    value = tx4b1Text,
                    onValueChange = { tx4b1Text = filterHex(it, 8) },
                    label = { Text("4B_1  (HEX, 8位)") },
                    prefix = { Text("0x", fontFamily = FontFamily.Monospace) },
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true,
                    textStyle = TextStyle(fontFamily = FontFamily.Monospace)
                )

                // 4B_2 输入
                TextField(
                    value = tx4b2Text,
                    onValueChange = { tx4b2Text = filterHex(it, 8) },
                    label = { Text("4B_2  (HEX, 8位)") },
                    prefix = { Text("0x", fontFamily = FontFamily.Monospace) },
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true,
                    textStyle = TextStyle(fontFamily = FontFamily.Monospace)
                )

                // 1B_1 / 1B_2 并排输入
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    TextField(
                        value = tx1b1Text,
                        onValueChange = { tx1b1Text = filterHex(it, 2) },
                        label = { Text("1B_1  (HEX)") },
                        prefix = { Text("0x", fontFamily = FontFamily.Monospace) },
                        modifier = Modifier.weight(1f),
                        singleLine = true,
                        textStyle = TextStyle(fontFamily = FontFamily.Monospace)
                    )
                    TextField(
                        value = tx1b2Text,
                        onValueChange = { tx1b2Text = filterHex(it, 2) },
                        label = { Text("1B_2  (HEX)") },
                        prefix = { Text("0x", fontFamily = FontFamily.Monospace) },
                        modifier = Modifier.weight(1f),
                        singleLine = true,
                        textStyle = TextStyle(fontFamily = FontFamily.Monospace)
                    )
                }

                HorizontalDivider(modifier = Modifier.padding(vertical = 2.dp))
                Button(
                    onClick = {
                        val var4_1 = tx4b1Text.padStart(8, '0').toLongOrNull(16)?.toInt() ?: 0
                        val var4_2 = tx4b2Text.padStart(8, '0').toLongOrNull(16)?.toInt() ?: 0
                        val var1_1 = tx1b1Text.padStart(2, '0').toIntOrNull(16) ?: 0
                        val var1_2 = tx1b2Text.padStart(2, '0').toIntOrNull(16) ?: 0
                        val frame = BleProtocol.buildTxFrame(
                            cmd    = txCmd,
                            var4_1 = var4_1,
                            var4_2 = var4_2,
                            var1_1 = var1_1,
                            var1_2 = var1_2
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

        // ── 数据终端（支持 RX/TX 切换）──────────────────────────────────────
        DebugSection(title = "数据终端 (${terminalDirectionFilter.direction})") {
            Column {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 12.dp, vertical = 12.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    TerminalDirectionFilter.values().forEach { filter ->
                        val selected = terminalDirectionFilter == filter
                        Button(
                            onClick = { terminalDirectionFilter = filter },
                            modifier = Modifier.weight(1f),
                            colors = ButtonDefaults.buttonColors(
                                containerColor = if (selected) {
                                    MaterialTheme.colorScheme.primary
                                } else {
                                    MaterialTheme.colorScheme.surfaceVariant
                                },
                                contentColor = if (selected) {
                                    MaterialTheme.colorScheme.onPrimary
                                } else {
                                    MaterialTheme.colorScheme.onSurfaceVariant
                                }
                            )
                        ) {
                            Text(filter.direction)
                        }
                    }
                }
                TerminalOutputWindow(
                    entries = filteredTerminalEntries,
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

/** 过滤输入为大写十六进制字符，并限制最大长度 */
private fun filterHex(input: String, maxLen: Int): String =
    input.uppercase(Locale.US).filter { it in '0'..'9' || it in 'A'..'F' }.take(maxLen)

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
    LaunchedEffect(entries.lastOrNull()?.id) {
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
                    text = "暂无匹配报文，切换 RX/TX 或等待新数据。",
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
