package com.qaryxos.companion.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.qaryxos.companion.data.ws.WsClient
import com.qaryxos.companion.data.ws.WsState
import kotlinx.coroutines.launch
import kotlin.math.abs

@Composable
fun RemoteScreen() {
    val scope   = rememberCoroutineScope()
    val status  by WsClient.status.collectAsState()
    val wsState by WsClient.state.collectAsState()

    val connected = wsState == WsState.CONNECTED
    var volume by remember { mutableIntStateOf(80) }

    LaunchedEffect(status?.volume) {
        status?.volume?.let { volume = it }
    }

    fun key(k: String) = scope.launch { WsClient.sendKey(k) }

    Column(
        Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background).padding(bottom = 8.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        // Now Playing bar
        Surface(Modifier.fillMaxWidth(), color = MaterialTheme.colorScheme.surfaceVariant, tonalElevation = 2.dp) {
            Row(Modifier.padding(horizontal = 16.dp, vertical = 10.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                Box(Modifier.size(8.dp).clip(CircleShape).background(
                    if (connected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error))
                Column(Modifier.weight(1f)) {
                    val title = when {
                        !connected -> "Disconnected"
                        status?.state == "playing" || status?.state == "paused" ->
                            status?.url?.substringAfterLast("/")?.substringBefore("?")?.take(50) ?: "Playing"
                        else -> "Idle"
                    }
                    Text(title, style = MaterialTheme.typography.bodyMedium,
                        maxLines = 1, overflow = TextOverflow.Ellipsis)
                    status?.let { st ->
                        if (st.duration > 0) {
                            val progress = (st.position / st.duration).coerceIn(0.0, 1.0).toFloat()
                            Spacer(Modifier.height(4.dp))
                            Row(verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                                LinearProgressIndicator(progress = { progress },
                                    Modifier.weight(1f).height(2.dp))
                                Text("${fmtTime(st.position)} / ${fmtTime(st.duration)}",
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant)
                            }
                        }
                    }
                }
                Icon(when (status?.state) {
                    "playing" -> Icons.Default.PlayArrow
                    "paused"  -> Icons.Default.Pause
                    else      -> Icons.Default.Tv
                }, null, tint = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.size(18.dp))
            }
        }

        Spacer(Modifier.weight(0.5f))

        // D-pad with horizontal swipe-to-seek
        Box(Modifier.size(260.dp).pointerInput(Unit) {
            detectHorizontalDragGestures { _, dx ->
                if (abs(dx) > 60) scope.launch { WsClient.seek(if (dx > 0) 30.0 else -30.0) }
            }
        }, contentAlignment = Alignment.Center) {
            Box(Modifier.align(Alignment.TopCenter).padding(top = 8.dp)) {
                FilledTonalIconButton({ key("up") }, Modifier.size(68.dp)) {
                    Icon(Icons.Default.KeyboardArrowUp, null, Modifier.size(32.dp)) }
            }
            Box(Modifier.align(Alignment.CenterStart).padding(start = 8.dp)) {
                FilledTonalIconButton({ key("left") }, Modifier.size(68.dp)) {
                    Icon(Icons.Default.KeyboardArrowLeft, null, Modifier.size(32.dp)) }
            }
            FilledIconButton({ key("ok") }, Modifier.size(80.dp), shape = CircleShape,
                colors = IconButtonDefaults.filledIconButtonColors(
                    containerColor = MaterialTheme.colorScheme.primary)) {
                Text("OK", style = MaterialTheme.typography.titleMedium,
                    color = MaterialTheme.colorScheme.onPrimary)
            }
            Box(Modifier.align(Alignment.CenterEnd).padding(end = 8.dp)) {
                FilledTonalIconButton({ key("right") }, Modifier.size(68.dp)) {
                    Icon(Icons.Default.KeyboardArrowRight, null, Modifier.size(32.dp)) }
            }
            Box(Modifier.align(Alignment.BottomCenter).padding(bottom = 8.dp)) {
                FilledTonalIconButton({ key("down") }, Modifier.size(68.dp)) {
                    Icon(Icons.Default.KeyboardArrowDown, null, Modifier.size(32.dp)) }
            }
        }

        Spacer(Modifier.height(20.dp))

        // Playback controls
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
            FilledTonalButton({ scope.launch { WsClient.seek(-30.0) } },
                contentPadding = PaddingValues(horizontal = 16.dp, vertical = 10.dp)) {
                Icon(Icons.Default.FastRewind, null, Modifier.size(18.dp))
                Spacer(Modifier.width(4.dp))
                Text("−30s")
            }
            FilledIconButton({ scope.launch { WsClient.pause() } }, Modifier.size(64.dp), shape = CircleShape) {
                Icon(
                    if (status?.paused == true || status?.state == "idle")
                        Icons.Default.PlayArrow else Icons.Default.Pause,
                    null, Modifier.size(32.dp))
            }
            FilledTonalButton({ scope.launch { WsClient.seek(30.0) } },
                contentPadding = PaddingValues(horizontal = 16.dp, vertical = 10.dp)) {
                Text("+30s")
                Spacer(Modifier.width(4.dp))
                Icon(Icons.Default.FastForward, null, Modifier.size(18.dp))
            }
        }

        Spacer(Modifier.height(16.dp))

        // Volume slider
        Row(Modifier.padding(horizontal = 32.dp).fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Icon(Icons.Default.VolumeDown, null, Modifier.size(20.dp),
                tint = MaterialTheme.colorScheme.onSurfaceVariant)
            Slider(
                value = volume.toFloat(),
                onValueChange = { volume = it.toInt() },
                onValueChangeFinished = { scope.launch { WsClient.volume(volume) } },
                valueRange = 0f..100f,
                modifier = Modifier.weight(1f),
            )
            Icon(Icons.Default.VolumeUp, null, Modifier.size(20.dp),
                tint = MaterialTheme.colorScheme.onSurfaceVariant)
            Text("$volume", style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.width(28.dp))
        }

        Spacer(Modifier.height(8.dp))

        // Utility buttons
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            listOf(
                Triple(Icons.Default.Stop,      "Stop") { scope.launch { WsClient.stop() }; Unit },
                Triple(Icons.Default.Home,      "Home") { key("home"); Unit },
                Triple(Icons.Default.ArrowBack, "Back") { key("back"); Unit },
                Triple(Icons.Default.Menu,      "Menu") { key("menu"); Unit },
            ).forEach { (icon, label, action) ->
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    FilledTonalIconButton(action, Modifier.size(52.dp)) { Icon(icon, label) }
                    Text(label, style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }
        }

        Spacer(Modifier.weight(1f))
    }
}

private fun fmtTime(secs: Double): String {
    val s = secs.toInt()
    return if (s >= 3600)
        "${s / 3600}:${String.format("%02d", (s % 3600) / 60)}:${String.format("%02d", s % 60)}"
    else
        "${s / 60}:${String.format("%02d", s % 60)}"
}
