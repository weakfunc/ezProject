package com.example.demo_1

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Card
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import com.example.demo_1.ui.theme.Demo_1Theme

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
                        TopAppBar(
                            title = { Text(text = "\u8c03\u8bd5") }
                        )
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

    Column(
        modifier = modifier
            .fillMaxSize()
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        Text(
            text = "\u5f53\u524dBLE\u8fde\u63a5\u72b6\u6001\uff1a" + if (isConnected) "\u5df2\u8fde\u63a5" else "\u672a\u8fde\u63a5",
            style = MaterialTheme.typography.bodyMedium,
            color = if (isConnected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant
        )
        Card(modifier = Modifier.fillMaxWidth()) {
            Column(
                modifier = Modifier.padding(12.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Text(
                    text = "BLE\u8bbe\u5907\u4fe1\u606f",
                    style = MaterialTheme.typography.titleMedium
                )
                if (connectedDevice == null) {
                    Text(
                        text = "\u6682\u65e0\u5df2\u8fde\u63a5\u8bbe\u5907",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                } else {
                    Text(
                        text = "\u8bbe\u5907\u540d\u79f0\uff1a${connectedDevice.name}",
                        style = MaterialTheme.typography.bodyMedium
                    )
                    Text(
                        text = "MAC\uff1a${connectedDevice.address}",
                        style = MaterialTheme.typography.bodyMedium
                    )
                    Text(
                        text = "RSSI\uff1a${connectedDevice.rssi}",
                        style = MaterialTheme.typography.bodyMedium
                    )
                }
            }
        }
        if (connectedDevice == null) {
            Spacer(modifier = Modifier.height(4.dp))
            Text(
                text = "\u8bf7\u5148\u5728BLE\u641c\u7d22\u9875\u9762\u70b9\u51fb\u201c\u8fde\u63a5\u201d",
                style = MaterialTheme.typography.bodySmall,
                color = Color.Gray
            )
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
