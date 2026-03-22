package com.example.demo_1

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.LocalRippleConfiguration
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import com.example.demo_1.ui.theme.Demo_1Theme

@OptIn(ExperimentalMaterial3Api::class)
class SecondActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        if (!UserConfig.DEVELOPER_MODE) {
            navigateToTab(AppTab.Settings, AppTab.Home)
            return
        }
        val previousTab = previousTabFromIntent()
        setContent {
            Demo_1Theme {
                Scaffold(
                    modifier = Modifier.fillMaxSize(),
                    containerColor = MaterialTheme.colorScheme.background,
                    topBar = {
                        TopAppBar(
                            title = { Text(text = UserConfig.settings_page_title) }
                        )
                    },
                    bottomBar = {
                        AppBottomNavigation(
                            currentTab = AppTab.Settings,
                            previousTab = previousTab,
                            onTabSelected = { tab ->
                                navigateToTab(AppTab.Settings, tab)
                            }
                        )
                    }
                ) { innerPadding ->
                    SecondPage(modifier = Modifier.padding(innerPadding))
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SecondPage(modifier: Modifier = Modifier) {
    Column(
        modifier = modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 18.dp, vertical = 16.dp),
        verticalArrangement = Arrangement.spacedBy(18.dp)
    ) {
        SettingsSection(
            title = "BLE\u8bbe\u7f6e",
            cardContainerColor = MaterialTheme.colorScheme.surfaceVariant
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 18.dp, vertical = 16.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "\u81ea\u52a8\u8fc7\u6ee4\u6211\u7684BLE\u8bbe\u5907",
                    style = MaterialTheme.typography.titleMedium,
                    color = MaterialTheme.colorScheme.onSurface
                )
                CompositionLocalProvider(LocalRippleConfiguration provides null) {
                    Switch(
                        checked = UserConfig.auto_filter_my_ble_devices,
                        onCheckedChange = { UserConfig.auto_filter_my_ble_devices = it },
                        interactionSource = remember { MutableInteractionSource() },
                        colors = SwitchDefaults.colors(
                            checkedThumbColor = MaterialTheme.colorScheme.primary,
                            checkedTrackColor = MaterialTheme.colorScheme.primaryContainer,
                            checkedBorderColor = Color.Transparent,
                            uncheckedThumbColor = MaterialTheme.colorScheme.surface,
                            uncheckedTrackColor = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.20f),
                            uncheckedBorderColor = Color.Transparent
                        )
                    )
                }
            }
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 18.dp, vertical = 16.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "\u81ea\u52a8\u914d\u7f6eBLE\u8bbe\u5907",
                    style = MaterialTheme.typography.titleMedium,
                    color = MaterialTheme.colorScheme.onSurface
                )
                CompositionLocalProvider(LocalRippleConfiguration provides null) {
                    Switch(
                        checked = UserConfig.auto_config_ble_device,
                        onCheckedChange = { UserConfig.auto_config_ble_device = it },
                        interactionSource = remember { MutableInteractionSource() },
                        colors = SwitchDefaults.colors(
                            checkedThumbColor = MaterialTheme.colorScheme.primary,
                            checkedTrackColor = MaterialTheme.colorScheme.primaryContainer,
                            checkedBorderColor = Color.Transparent,
                            uncheckedThumbColor = MaterialTheme.colorScheme.surface,
                            uncheckedTrackColor = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.20f),
                            uncheckedBorderColor = Color.Transparent
                        )
                    )
                }
            }
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 18.dp, vertical = 16.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "\u81ea\u52a8\u8fde\u63a5\u6211\u7684BLE\u8bbe\u5907",
                    style = MaterialTheme.typography.titleMedium,
                    color = MaterialTheme.colorScheme.onSurface
                )
                CompositionLocalProvider(LocalRippleConfiguration provides null) {
                    Switch(
                        checked = UserConfig.auto_connect_my_ble_device,
                        onCheckedChange = { UserConfig.auto_connect_my_ble_device = it },
                        interactionSource = remember { MutableInteractionSource() },
                        colors = SwitchDefaults.colors(
                            checkedThumbColor = MaterialTheme.colorScheme.primary,
                            checkedTrackColor = MaterialTheme.colorScheme.primaryContainer,
                            checkedBorderColor = Color.Transparent,
                            uncheckedThumbColor = MaterialTheme.colorScheme.surface,
                            uncheckedTrackColor = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.20f),
                            uncheckedBorderColor = Color.Transparent
                        )
                    )
                }
            }
        }
        SettingsSection(
            title = "\u901a\u7528\u8bbe\u7f6e"
        ) {
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(72.dp)
            )
        }
    }
}

@Composable
private fun SettingsSection(
    title: String,
    cardContainerColor: Color = MaterialTheme.colorScheme.surface,
    content: @Composable () -> Unit
) {
    Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text(
            text = title,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(horizontal = 4.dp)
        )
        Card(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(20.dp),
            colors = CardDefaults.cardColors(
                containerColor = cardContainerColor
            ),
            elevation = CardDefaults.cardElevation(
                defaultElevation = 2.dp
            )
        ) {
            content()
        }
    }
}

@Preview(showBackground = true)
@Composable
fun SecondPagePreview() {
    Demo_1Theme {
        SecondPage()
    }
}
