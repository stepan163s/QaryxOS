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
import androidx.compose.ui.unit.dp
import com.qaryxos.companion.data.models.IptvPlaylist
import com.qaryxos.companion.data.ws.WsClient
import kotlinx.coroutines.launch

@Composable
fun ChannelManagerScreen() {
    var selectedTab by remember { mutableIntStateOf(0) }

    TabRow(selectedTabIndex = selectedTab) {
        Tab(selected = selectedTab == 0, onClick = { selectedTab = 0 }) {
            Text("YouTube", modifier = Modifier.padding(12.dp))
        }
        Tab(selected = selectedTab == 1, onClick = { selectedTab = 1 }) {
            Text("IPTV", modifier = Modifier.padding(12.dp))
        }
    }

    when (selectedTab) {
        0 -> YoutubePlayTab()
        1 -> IptvPlaylistsTab()
    }
}

/** YouTube tab: play a URL directly (server resolves via yt-dlp). */
@Composable
private fun YoutubePlayTab() {
    val scope = rememberCoroutineScope()
    var url     by remember { mutableStateOf("") }
    var sending by remember { mutableStateOf(false) }

    // Show server errors
    var lastError by remember { mutableStateOf<String?>(null) }
    LaunchedEffect(Unit) {
        WsClient.errorFlow.collect { msg -> lastError = msg }
    }

    Column(Modifier.fillMaxSize().padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text("Play YouTube URL", style = MaterialTheme.typography.titleMedium)
        Text("Enter any YouTube video or playlist URL. The device resolves it automatically.",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant)

        OutlinedTextField(
            value = url,
            onValueChange = { url = it; lastError = null },
            label = { Text("YouTube URL") },
            placeholder = { Text("https://youtube.com/watch?v=...") },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
        )

        Button(
            onClick = {
                if (url.isNotBlank()) {
                    scope.launch {
                        sending = true
                        WsClient.play(url.trim(), type = "youtube")
                        sending = false
                        url = ""
                    }
                }
            },
            modifier = Modifier.fillMaxWidth(),
            enabled = url.isNotBlank() && !sending,
        ) {
            if (sending) {
                CircularProgressIndicator(Modifier.size(18.dp), strokeWidth = 2.dp)
                Spacer(Modifier.width(8.dp))
            }
            Icon(Icons.Default.PlayArrow, null)
            Spacer(Modifier.width(8.dp))
            Text("Play on TV")
        }

        lastError?.let { err ->
            Card(colors = CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.errorContainer)) {
                Text("Error: $err",
                    Modifier.padding(12.dp),
                    color = MaterialTheme.colorScheme.onErrorContainer,
                    style = MaterialTheme.typography.bodySmall)
            }
        }
    }
}

/** IPTV tab: manage M3U playlists. */
@Composable
private fun IptvPlaylistsTab() {
    val scope = rememberCoroutineScope()

    var playlists by remember { mutableStateOf<List<IptvPlaylist>>(emptyList()) }
    var loading   by remember { mutableStateOf(true) }
    var showAdd   by remember { mutableStateOf(false) }
    var newUrl    by remember { mutableStateOf("") }
    var newName   by remember { mutableStateOf("") }
    var adding    by remember { mutableStateOf(false) }

    LaunchedEffect(Unit) {
        launch {
            WsClient.playlistsFlow.collect { list ->
                playlists = list
                loading = false
            }
        }
        WsClient.playlistsGet()
    }

    Column(Modifier.fillMaxSize().padding(16.dp)) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically) {
            Text("IPTV Playlists", style = MaterialTheme.typography.titleMedium)
            IconButton(onClick = { showAdd = !showAdd }) {
                Icon(Icons.Default.Add, "Add playlist")
            }
        }

        if (showAdd) {
            Spacer(Modifier.height(8.dp))
            OutlinedTextField(
                value = newName,
                onValueChange = { newName = it },
                label = { Text("Playlist name") },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
            )
            Spacer(Modifier.height(4.dp))
            OutlinedTextField(
                value = newUrl,
                onValueChange = { newUrl = it },
                label = { Text("M3U URL") },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
            )
            Spacer(Modifier.height(8.dp))
            Button(
                onClick = {
                    if (newUrl.isNotBlank() && newName.isNotBlank()) {
                        scope.launch {
                            adding = true
                            WsClient.playlistAdd(newUrl.trim(), newName.trim())
                            // Re-fetch after adding (server takes time to download M3U)
                            kotlinx.coroutines.delay(2000)
                            WsClient.playlistsGet()
                            newUrl = ""; newName = ""; showAdd = false; adding = false
                        }
                    }
                },
                modifier = Modifier.fillMaxWidth(),
                enabled = !adding,
            ) {
                if (adding) {
                    CircularProgressIndicator(Modifier.size(18.dp), strokeWidth = 2.dp)
                    Spacer(Modifier.width(8.dp))
                }
                Text("Add Playlist")
            }
            Spacer(Modifier.height(8.dp))
        }

        if (loading) LinearProgressIndicator(Modifier.fillMaxWidth())

        LazyColumn(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            items(playlists, key = { it.id }) { pl ->
                ChannelItem(
                    name     = pl.name,
                    subtitle = "${pl.channelCount} channels  •  ${pl.url}",
                    onOpen   = { scope.launch { WsClient.navigate("iptv") } },
                    onDelete = {
                        scope.launch {
                            WsClient.playlistDel(pl.id)
                            playlists = playlists.filter { it.id != pl.id }
                        }
                    },
                )
            }
            if (playlists.isEmpty() && !loading) {
                item {
                    Text("No playlists yet. Add an M3U URL.",
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(top = 32.dp))
                }
            }
        }
    }
}

@Composable
private fun ChannelItem(name: String, subtitle: String, onOpen: () -> Unit, onDelete: () -> Unit) {
    Card(Modifier.fillMaxWidth()) {
        Row(Modifier.padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(name, style = MaterialTheme.typography.bodyLarge)
                Text(subtitle, style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 1)
            }
            IconButton(onClick = onOpen) {
                Icon(Icons.Default.PlayArrow, "Open on TV", tint = MaterialTheme.colorScheme.primary)
            }
            IconButton(onClick = onDelete) {
                Icon(Icons.Default.Delete, "Remove", tint = MaterialTheme.colorScheme.error)
            }
        }
    }
}
