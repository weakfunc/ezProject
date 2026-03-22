package com.example.demo_1.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable

private val DarkColorScheme = darkColorScheme(
    primary = LavenderDarkPrimary,
    onPrimary = LavenderDarkOnPrimary,
    primaryContainer = LavenderDarkPrimaryContainer,
    onPrimaryContainer = LavenderDarkOnPrimaryContainer,
    secondary = LavenderDarkSecondary,
    onSecondary = LavenderDarkOnSecondary,
    secondaryContainer = LavenderDarkSecondaryContainer,
    onSecondaryContainer = LavenderDarkOnSecondaryContainer,
    tertiary = LavenderDarkTertiary,
    onTertiary = LavenderDarkOnTertiary,
    background = LavenderDarkBackground,
    onBackground = LavenderDarkOnBackground,
    surface = LavenderDarkSurface,
    onSurface = LavenderDarkOnSurface,
    surfaceVariant = LavenderDarkSurfaceVariant,
    onSurfaceVariant = LavenderDarkOnSurfaceVariant
)

private val LightColorScheme = lightColorScheme(
    primary = LavenderLightPrimary,
    onPrimary = LavenderLightOnPrimary,
    primaryContainer = LavenderLightPrimaryContainer,
    onPrimaryContainer = LavenderLightOnPrimaryContainer,
    secondary = LavenderLightSecondary,
    onSecondary = LavenderLightOnSecondary,
    secondaryContainer = LavenderLightSecondaryContainer,
    onSecondaryContainer = LavenderLightOnSecondaryContainer,
    tertiary = LavenderLightTertiary,
    onTertiary = LavenderLightOnTertiary,
    background = LavenderLightBackground,
    onBackground = LavenderLightOnBackground,
    surface = LavenderLightSurface,
    onSurface = LavenderLightOnSurface,
    surfaceVariant = LavenderLightSurfaceVariant,
    onSurfaceVariant = LavenderLightOnSurfaceVariant
)

@Composable
fun Demo_1Theme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit
) {
    val colorScheme = if (darkTheme) DarkColorScheme else LightColorScheme

    MaterialTheme(
        colorScheme = colorScheme,
        typography = Typography,
        content = content
    )
}
