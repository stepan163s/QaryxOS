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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.qaryxos.companion.data.api.ApiClient
import com.qaryxos.companion.data.models.*
import kotlinx.coroutines.launch

@Composable
fun ChannelManagerScreen() {
    val context = LocalContext.current
    val scope   = rememberCoroutineScope()
    val api     = ApiClient.getApiForContext(context)

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
        0 -> YoutubeChannelsTab(api = api, scope = scope)
        1 -> IptvPlaylistsTab(api = api, scope = scope)
    }
}

@Composable
private fun YoutubeChannelsTab(
    api: com.qaryxos.companion.data.api.QaryxApi,
    scope: kotlinx.coroutines.CoroutineScope,
) {
    var channels by remember { mutableStateOf<List<YoutubeChannel>>(emptyList()) }
    var showAdd  by remember { mutableStateOf(false) }
    var newUrl   by remember { mutableStateOf("") }
    var loading  by remember { mutableStateOf(false) }

    LaunchedEffect(Unit) {
        channels = runCatching { api.getChannels().body() ?: emptyList() }.getOrDefault(emptyList())
    }

    Column(Modifier.fillMaxSize().padding(16.dp)) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text("Channels", style = MaterialTheme.typography.titleMedium)
            Row {
                IconButton(onClick = { scope.launch {
                    api.refreshYoutube()
                    channels = runCatching { api.getChannels().body() ?: emptyList() }.getOrDefault(emptyList())
                }}) { Icon(Icons.Default.Refresh, "Refresh") }
                IconButton(onClick = { showAdd = true }) {
                    Icon(Icons.Default.Add, "Add channel")
                }
            }
        }

        if (showAdd) {
            OutlinedTextField(
                value = newUrl,
                onValueChange = { newUrl = it },
                label = { Text("YouTube channel URL") },
                modifier = Modifier.fillMaxWidth(),
                trailingIcon = {
                    IconButton(onClick = {
                        if (newUrl.isNotBlank()) {
                            scope.launch {
                                loading = true
                                runCatching { api.addChannel(AddChannelRequest(newUrl)) }
                                channels = runCatching { api.getChannels().body() ?: emptyList() }.getOrDefault(emptyList())
                                newUrl = ""
                                showAdd = false
                                loading = false
                            }
                        }
                    }) { Icon(Icons.Default.Check, "Add") }
                },
            )
            Spacer(Modifier.height(8.dp))
        }

        if (loading) { LinearProgressIndicator(Modifier.fillMaxWidth()) }

        LazyColumn(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            items(channels, key = { it.id }) { ch ->
                ChannelItem(
                    name = ch.name,
                    subtitle = ch.url,
                    onDelete = {
                        scope.launch {
                            api.removeChannel(ch.id)
                            channels = channels.filter { it.id != ch.id }
                        }
                    }
                )
            }
            if (channels.isEmpty() && !loading) {
                item {
                    Text("No channels yet. Add a YouTube channel URL.",
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(top = 32.dp))
                }
            }
        }
    }
}

@Composable
private fun IptvPlaylistsTab(
    api: com.qaryxos.companion.data.api.QaryxApi,
    scope: kotlinx.coroutines.CoroutineScope,
) {
    var playlists by remember { mutableStateOf<List<IptvPlaylist>>(emptyList()) }
    var showAdd   by remember { mutableStateOf(false) }
    var newUrl    by remember { mutableStateOf("") }
    var newName   by remember { mutableStateOf("") }
    var loading   by remember { mutableStateOf(false) }

    LaunchedEffect(Unit) {
        playlists = runCatching { api.getPlaylists().body() ?: emptyList() }.getOrDefault(emptyList())
    }

    Column(Modifier.fillMaxSize().padding(16.dp)) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text("IPTV Playlists", style = MaterialTheme.typography.titleMedium)
            IconButton(onClick = { showAdd = !showAdd }) {
                Icon(Icons.Default.Add, "Add playlist")
            }
        }

        if (showAdd) {
            OutlinedTextField(
                value = newName,
                onValueChange = { newName = it },
                label = { Text("Playlist name") },
                modifier = Modifier.fillMaxWidth(),
            )
            Spacer(Modifier.height(4.dp))
            OutlinedTextField(
                value = newUrl,
                onValueChange = { newUrl = it },
                label = { Text("M3U URL") },
                modifier = Modifier.fillMaxWidth(),
            )
            Spacer(Modifier.height(8.dp))
            Button(
                onClick = {
                    if (newUrl.isNotBlank() && newName.isNotBlank()) {
                        scope.launch {
                            loading = true
                            runCatching { api.addPlaylist(AddPlaylistRequest(newUrl, newName)) }
                            playlists = runCatching { api.getPlaylists().body() ?: emptyList() }.getOrDefault(emptyList())
                            newUrl = ""; newName = ""; showAdd = false; loading = false
                        }
                    }
                },
                modifier = Modifier.fillMaxWidth(),
            ) { Text("Add Playlist") }
            Spacer(Modifier.height(8.dp))
        }

        if (loading) { LinearProgressIndicator(Modifier.fillMaxWidth()) }

        LazyColumn(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            items(playlists, key = { it.id }) { pl ->
                ChannelItem(
                    name = pl.name,
                    subtitle = "${pl.channelCount} channels  •  ${pl.url}",
                    onDelete = {
                        scope.launch {
                            api.removePlaylist(pl.id)
                            playlists = playlists.filter { it.id != pl.id }
                        }
                    }
                )
            }
        }
    }
}

@Composable
private fun ChannelItem(name: String, subtitle: String, onDelete: () -> Unit) {
    Card(Modifier.fillMaxWidth()) {
        Row(
            Modifier.padding(12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f)) {
                Text(name, style = MaterialTheme.typography.bodyLarge)
                Text(subtitle, style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1)
            }
            IconButton(onClick = onDelete) {
                Icon(Icons.Default.Delete, "Remove", tint = MaterialTheme.colorScheme.error)
            }
        }
    }
}
