package com.qaryxos.companion.ui.screens

import android.content.Context
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.qaryxos.companion.data.api.ApiClient
import com.qaryxos.companion.data.models.KeyRequest
import com.qaryxos.companion.data.models.SeekRequest
import com.qaryxos.companion.data.models.VolumeRequest
import kotlinx.coroutines.launch

@Composable
fun RemoteScreen() {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val api = ApiClient.getApiForContext(context)

    var status by remember { mutableStateOf("●") }
    var connected by remember { mutableStateOf(true) }

    suspend fun sendKey(key: String) {
        runCatching { api.sendKey(KeyRequest(key)) }
            .onFailure { connected = false }
            .onSuccess { connected = true }
    }

    suspend fun seek(s: Float) = runCatching { api.seek(SeekRequest(s)) }
    suspend fun volChange(delta: Int) {
        val st = runCatching { api.status() }.getOrNull()?.body() ?: return
        api.volume(VolumeRequest((st.volume + delta).coerceIn(0, 100)))
    }

    Column(
        modifier = Modifier.fillMaxSize().padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        // Connection indicator
        Row(verticalAlignment = Alignment.CenterVertically) {
            val dot = if (connected) Color(0xFF4CAF50) else Color(0xFFF44336)
            Text(status, color = dot)
            Spacer(Modifier.width(8.dp))
            Text(if (connected) "Connected" else "Disconnected",
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant)
        }

        Spacer(Modifier.height(24.dp))

        // D-pad
        Box(modifier = Modifier.size(220.dp), contentAlignment = Alignment.Center) {
            // Up
            Box(Modifier.align(Alignment.TopCenter)) {
                DpadButton(icon = { Icon(Icons.Default.KeyboardArrowUp, null) }) {
                    scope.launch { sendKey("up") }
                }
            }
            // Left
            Box(Modifier.align(Alignment.CenterStart)) {
                DpadButton(icon = { Icon(Icons.Default.KeyboardArrowLeft, null) }) {
                    scope.launch { sendKey("left") }
                }
            }
            // OK center
            Button(
                onClick = { scope.launch { sendKey("ok") } },
                modifier = Modifier.size(72.dp),
                shape = CircleShape,
                colors = ButtonDefaults.buttonColors(
                    containerColor = MaterialTheme.colorScheme.primary
                )
            ) { Text("OK") }
            // Right
            Box(Modifier.align(Alignment.CenterEnd)) {
                DpadButton(icon = { Icon(Icons.Default.KeyboardArrowRight, null) }) {
                    scope.launch { sendKey("right") }
                }
            }
            // Down
            Box(Modifier.align(Alignment.BottomCenter)) {
                DpadButton(icon = { Icon(Icons.Default.KeyboardArrowDown, null) }) {
                    scope.launch { sendKey("down") }
                }
            }
        }

        Spacer(Modifier.height(24.dp))

        // Playback controls
        Row(horizontalArrangement = Arrangement.spacedBy(16.dp)) {
            FilledTonalButton(onClick = { scope.launch { seek(-30f) } }) {
                Icon(Icons.Default.FastRewind, null)
                Spacer(Modifier.width(4.dp))
                Text("−30s")
            }
            FilledTonalButton(onClick = { scope.launch { api.pause() } }) {
                Icon(Icons.Default.PlayArrow, null)
                Spacer(Modifier.width(4.dp))
                Text("Play/Pause")
            }
            FilledTonalButton(onClick = { scope.launch { seek(30f) } }) {
                Text("+30s")
                Spacer(Modifier.width(4.dp))
                Icon(Icons.Default.FastForward, null)
            }
        }

        Spacer(Modifier.height(16.dp))

        // Volume + Stop + Home
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            IconButton(onClick = { scope.launch { volChange(-10) } }) {
                Icon(Icons.Default.VolumeDown, "Volume Down")
            }
            IconButton(onClick = { scope.launch { volChange(10) } }) {
                Icon(Icons.Default.VolumeUp, "Volume Up")
            }
            Spacer(Modifier.width(8.dp))
            FilledTonalButton(onClick = { scope.launch { api.stop() } }) {
                Icon(Icons.Default.Stop, null)
                Spacer(Modifier.width(4.dp))
                Text("Stop")
            }
            FilledTonalButton(onClick = { scope.launch { sendKey("home") } }) {
                Icon(Icons.Default.Home, null)
                Spacer(Modifier.width(4.dp))
                Text("Home")
            }
        }

        Spacer(Modifier.height(12.dp))

        // Back button
        OutlinedButton(onClick = { scope.launch { sendKey("back") } }) {
            Icon(Icons.Default.ArrowBack, null)
            Spacer(Modifier.width(4.dp))
            Text("Back")
        }
    }
}

@Composable
private fun DpadButton(
    icon: @Composable () -> Unit,
    onClick: () -> Unit,
) {
    FilledTonalIconButton(
        onClick = onClick,
        modifier = Modifier.size(64.dp),
    ) { icon() }
}
