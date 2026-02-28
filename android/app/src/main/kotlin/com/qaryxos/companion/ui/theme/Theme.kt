package com.qaryxos.companion.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

private val Accent    = Color(0xFF6478DC)
private val BgDark    = Color(0xFF0A0A0A)
private val Surface   = Color(0xFF1C1C1C)
private val Surface2  = Color(0xFF2A2A2A)
private val OnSurface = Color(0xFFF0F0F0)
private val Muted     = Color(0xFF888888)
private val Error     = Color(0xFFF44336)
private val Green     = Color(0xFF4CAF50)

private val DarkColors = darkColorScheme(
    primary          = Accent,
    onPrimary        = Color.White,
    primaryContainer = Color(0xFF2A3060),
    onPrimaryContainer = Color(0xFFCDD5FF),
    secondary        = Color(0xFF9EAEFF),
    onSecondary      = Color(0xFF1A237E),
    background       = BgDark,
    onBackground     = OnSurface,
    surface          = Surface,
    onSurface        = OnSurface,
    surfaceVariant   = Surface2,
    onSurfaceVariant = Muted,
    error            = Error,
    outline          = Color(0xFF444444),
)

@Composable
fun QaryxOSTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = DarkColors,
        content = content,
    )
}
