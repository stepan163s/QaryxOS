package com.qaryxos.companion.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.qaryxos.companion.data.models.HistoryEntry
import com.qaryxos.companion.data.ws.WsClient
import kotlinx.coroutines.launch
import java.text.SimpleDateFormat
import java.util.*

@Composable
fun HistoryScreen() {
    val scope = rememberCoroutineScope()

    var entries    by remember { mutableStateOf<List<HistoryEntry>>(emptyList()) }
    var loading    by remember { mutableStateOf(true) }
    var playingUrl by remember { mutableStateOf<String?>(null) }

    // Collect one-shot history response
    LaunchedEffect(Unit) {
        launch {
            WsClient.historyFlow.collect { list ->
                entries = list
                loading = false
            }
        }
        WsClient.historyGet()
    }

    fun reload() {
        loading = true
        scope.launch {
            WsClient.historyGet()
        }
    }

    Column(Modifier.fillMaxSize()) {
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 12.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text("History", style = MaterialTheme.typography.headlineSmall)
            Row {
                IconButton(onClick = { reload() }) {
                    Icon(Icons.Default.Refresh, "Refresh")
                }
                if (entries.isNotEmpty()) {
                    IconButton(onClick = {
                        scope.launch {
                            WsClient.historyClear()
                            entries = emptyList()
                        }
                    }) {
                        Icon(Icons.Default.DeleteSweep, "Clear all",
                            tint = MaterialTheme.colorScheme.error)
                    }
                }
            }
        }

        if (loading) LinearProgressIndicator(Modifier.fillMaxWidth())

        if (entries.isEmpty() && !loading) {
            Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                Column(horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Icon(Icons.Default.History, null, Modifier.size(48.dp),
                        tint = MaterialTheme.colorScheme.onSurfaceVariant)
                    Text("No history yet",
                        color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }
            return@Column
        }

        LazyColumn {
            items(entries, key = { it.url }) { entry ->
                HistoryItem(
                    entry     = entry,
                    isPlaying = playingUrl == entry.url,
                    onPlay    = {
                        scope.launch {
                            playingUrl = entry.url
                            WsClient.play(entry.url, entry.contentType, resume = true)
                            playingUrl = null
                        }
                    },
                    onRemove = { entries = entries.filter { it.url != entry.url } },
                )
                HorizontalDivider()
            }
        }
    }
}

@Composable
private fun HistoryItem(
    entry: HistoryEntry,
    isPlaying: Boolean,
    onPlay: () -> Unit,
    onRemove: () -> Unit,
) {
    val typeIcon = when (entry.contentType) {
        "youtube" -> Icons.Default.VideoLibrary
        "iptv"    -> Icons.Default.Tv
        else      -> Icons.Default.Link
    }
    val dateStr = remember(entry.playedAt) {
        SimpleDateFormat("MMM d, HH:mm", Locale.getDefault())
            .format(Date((entry.playedAt * 1000).toLong()))
    }

    ListItem(
        headlineContent = {
            Text(entry.title.ifBlank { entry.url },
                maxLines = 1, overflow = TextOverflow.Ellipsis)
        },
        supportingContent = {
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp),
                verticalAlignment = Alignment.CenterVertically) {
                Text(dateStr, style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
                if (entry.position > 5f) {
                    Text("•", style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant)
                    Text("resume ${fmtTime(entry.position)}",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.primary)
                }
            }
        },
        leadingContent = {
            Icon(typeIcon, null, tint = MaterialTheme.colorScheme.onSurfaceVariant)
        },
        trailingContent = {
            Row {
                if (isPlaying) {
                    CircularProgressIndicator(Modifier.size(24.dp).padding(end = 8.dp),
                        strokeWidth = 2.dp)
                } else {
                    IconButton(onClick = onPlay) {
                        Icon(Icons.Default.PlayArrow, "Play",
                            tint = MaterialTheme.colorScheme.primary)
                    }
                }
                IconButton(onClick = onRemove) {
                    Icon(Icons.Default.Close, "Remove")
                }
            }
        },
    )
}

private fun fmtTime(secs: Float): String {
    val s = secs.toInt()
    return if (s >= 3600)
        "${s / 3600}:${String.format("%02d", (s % 3600) / 60)}:${String.format("%02d", s % 60)}"
    else
        "${s / 60}:${String.format("%02d", s % 60)}"
}
