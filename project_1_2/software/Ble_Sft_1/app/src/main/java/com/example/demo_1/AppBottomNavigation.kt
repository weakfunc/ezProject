package com.example.demo_1

import android.app.ActivityOptions
import android.content.Intent
import androidx.activity.ComponentActivity
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.BluetoothSearching
import androidx.compose.material.icons.filled.BugReport
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import kotlin.math.roundToInt

private const val EXTRA_FROM_TAB = "extra_from_tab"

private fun bottomNavTabs(): List<AppTab> {
    return if (UserConfig.DEVELOPER_MODE) {
        listOf(AppTab.Home, AppTab.BleScan, AppTab.Settings, AppTab.Debug)
    } else {
        listOf(AppTab.Home)
    }
}

enum class AppTab(
    val icon: ImageVector,
    val contentDescription: String
) {
    Home(
        icon = Icons.Filled.Home,
        contentDescription = "\u4e3b\u9875\u9762"
    ),
    BleScan(
        icon = Icons.AutoMirrored.Filled.BluetoothSearching,
        contentDescription = "BLE\u641c\u7d22\u9875\u9762"
    ),
    Settings(
        icon = Icons.Filled.Settings,
        contentDescription = "\u8bbe\u7f6e\u9875\u9762"
    ),
    Debug(
        icon = Icons.Filled.BugReport,
        contentDescription = "\u8c03\u8bd5\u9875\u9762"
    )
}

@Composable
fun AppBottomNavigation(
    currentTab: AppTab,
    previousTab: AppTab?,
    onTabSelected: (AppTab) -> Unit
) {
    val tabs = bottomNavTabs()
    val selectedTab = currentTab.takeIf { it in tabs } ?: previousTab?.takeIf { it in tabs } ?: tabs.first()
    val tabCount = tabs.size
    val currentIndex = tabs.indexOf(selectedTab)
    val startIndex = tabs.indexOf(previousTab).takeIf { it >= 0 } ?: currentIndex

    var indicatorIndexTarget by remember(selectedTab, previousTab) {
        mutableFloatStateOf(startIndex.toFloat())
    }
    LaunchedEffect(selectedTab, previousTab) {
        indicatorIndexTarget = currentIndex.toFloat()
    }

    val animatedIndex by animateFloatAsState(
        targetValue = indicatorIndexTarget,
        animationSpec = tween(durationMillis = 240, easing = FastOutSlowInEasing),
        label = "BottomIndicatorIndex"
    )

    val density = LocalDensity.current
    val indicatorWidth = 26.dp
    val indicatorWidthPx = with(density) { indicatorWidth.toPx() }
    var navWidthPx by remember { mutableIntStateOf(0) }

    Surface(
        tonalElevation = 8.dp,
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.55f)
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 8.dp, vertical = 6.dp)
                .onSizeChanged { navWidthPx = it.width }
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(bottom = 10.dp)
            ) {
                tabs.forEach { tab ->
                    val selected = tab == selectedTab
                    val iconColor = if (selected) {
                        MaterialTheme.colorScheme.onPrimaryContainer
                    } else {
                        MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.65f)
                    }

                    Box(
                        modifier = Modifier.weight(1f),
                        contentAlignment = Alignment.Center
                    ) {
                        IconButton(onClick = { onTabSelected(tab) }) {
                            Icon(
                                imageVector = tab.icon,
                                contentDescription = tab.contentDescription,
                                tint = iconColor
                            )
                        }
                    }
                }
            }

            if (navWidthPx > 0 && tabCount > 0) {
                val slotWidthPx = navWidthPx / tabCount.toFloat()
                val targetOffsetPx = slotWidthPx * animatedIndex + (slotWidthPx - indicatorWidthPx) / 2f
                Box(
                    modifier = Modifier
                        .align(Alignment.BottomStart)
                        .offset { IntOffset(x = targetOffsetPx.roundToInt(), y = 0) }
                        .width(indicatorWidth)
                        .height(4.dp)
                        .clip(RoundedCornerShape(999.dp))
                        .background(MaterialTheme.colorScheme.primary)
                )
            }
        }
    }
}

fun ComponentActivity.navigateToTab(fromTab: AppTab, toTab: AppTab) {
    val targetTab = if (!UserConfig.DEVELOPER_MODE && toTab != AppTab.Home) {
        AppTab.Home
    } else {
        toTab
    }
    val destination = when (targetTab) {
        AppTab.Home -> MainActivity::class.java
        AppTab.BleScan -> BleScanActivity::class.java
        AppTab.Settings -> SecondActivity::class.java
        AppTab.Debug -> ThirdActivity::class.java
    }
    if (this::class.java == destination) return

    val intent = Intent(this, destination).apply {
        addFlags(Intent.FLAG_ACTIVITY_NO_ANIMATION)
        putExtra(EXTRA_FROM_TAB, fromTab.name)
    }
    val options = ActivityOptions.makeCustomAnimation(this, 0, 0).toBundle()
    startActivity(intent, options)
    finish()
    @Suppress("DEPRECATION")
    overridePendingTransition(0, 0)
}

fun ComponentActivity.previousTabFromIntent(): AppTab? {
    val tabName = intent?.getStringExtra(EXTRA_FROM_TAB) ?: return null
    return runCatching { AppTab.valueOf(tabName) }.getOrNull()
}
