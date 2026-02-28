package com.qaryxos.companion.ui.screens

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
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
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.qaryxos.companion.data.models.IptvChannel
import com.qaryxos.companion.data.models.IptvPlaylist
import com.qaryxos.companion.data.ws.WsClient
import kotlinx.coroutines.launch

@Composable
fun ChannelManagerScreen() {
    var selectedTab by remember { mutableIntStateOf(0) }

    Column(Modifier.fillMaxSize()) {
        TabRow(selectedTabIndex = selectedTab) {
            Tab(selected = selectedTab == 0, onClick = { selectedTab = 0 },
                text = { Text("YouTube") })
            Tab(selected = selectedTab == 1, onClick = { selectedTab = 1 },
                text = { Text("IPTV") })
        }
        when (selectedTab) {
            0 -> YoutubePlayTab()
            1 -> IptvTab()
        }
    }
}

// ── YouTube tab ───────────────────────────────────────────────────────────────

@Composable
private fun YoutubePlayTab() {
    val scope = rememberCoroutineScope()
    var url       by remember { mutableStateOf("") }
    var sending   by remember { mutableStateOf(false) }
    var lastError by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(Unit) {
        WsClient.errorFlow.collect { msg -> lastError = msg }
    }

    Column(
        Modifier.fillMaxSize().padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text(
            "Введите ссылку на YouTube видео или плейлист. " +
            "Устройство автоматически разрешит её через yt-dlp.",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )

        OutlinedTextField(
            value = url,
            onValueChange = { url = it; lastError = null },
            label = { Text("YouTube URL") },
            placeholder = { Text("https://youtube.com/watch?v=...") },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
            leadingIcon = { Icon(Icons.Default.Search, null) },
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
            Text("Воспроизвести на TV")
        }

        lastError?.let { err ->
            Card(colors = CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.errorContainer)) {
                Text("Ошибка: $err", Modifier.padding(12.dp),
                    color = MaterialTheme.colorScheme.onErrorContainer,
                    style = MaterialTheme.typography.bodySmall)
            }
        }
    }
}

// ── IPTV tab ──────────────────────────────────────────────────────────────────

@Composable
private fun IptvTab() {
    var openPlaylist by remember { mutableStateOf<IptvPlaylist?>(null) }

    if (openPlaylist != null) {
        ChannelListScreen(
            playlist = openPlaylist!!,
            onBack   = { openPlaylist = null },
        )
    } else {
        PlaylistListScreen(onOpenChannels = { openPlaylist = it })
    }
}

// ── Playlist list ─────────────────────────────────────────────────────────────

@Composable
private fun PlaylistListScreen(onOpenChannels: (IptvPlaylist) -> Unit) {
    val scope   = rememberCoroutineScope()
    val context = LocalContext.current

    var playlists by remember { mutableStateOf<List<IptvPlaylist>>(emptyList()) }
    var loading   by remember { mutableStateOf(true) }
    var showAdd   by remember { mutableStateOf(false) }
    var addMode   by remember { mutableIntStateOf(0) }  // 0=URL  1=File
    var newUrl    by remember { mutableStateOf("") }
    var newName   by remember { mutableStateOf("") }
    var adding    by remember { mutableStateOf(false) }
    var addError  by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(Unit) {
        launch { WsClient.playlistsFlow.collect { playlists = it; loading = false } }
        WsClient.playlistsGet()
    }

    // File picker — parses M3U on-device, sends channels as JSON
    val fileLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.GetContent()
    ) { uri: Uri? ->
        uri ?: return@rememberLauncherForActivityResult
        scope.launch {
            adding = true; addError = null
            try {
                val raw = context.contentResolver
                    .openInputStream(uri)?.bufferedReader()?.readText() ?: ""
                val channels = parseM3u(raw)
                if (channels.isEmpty()) {
                    addError = "Не удалось найти каналы в файле"
                } else {
                    WsClient.playlistImport(newName.ifBlank { "Imported" }, channels)
                    kotlinx.coroutines.delay(600)
                    WsClient.playlistsGet()
                    newName = ""; showAdd = false
                }
            } catch (e: Exception) {
                addError = "Ошибка: ${e.message}"
            } finally {
                adding = false
            }
        }
    }

    Column(Modifier.fillMaxSize().padding(16.dp)) {

        // Header row
        Row(Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically) {
            Text("IPTV плейлисты", style = MaterialTheme.typography.titleMedium)
            IconButton(onClick = { showAdd = !showAdd; addError = null }) {
                Icon(if (showAdd) Icons.Default.Close else Icons.Default.Add,
                    if (showAdd) "Закрыть" else "Добавить")
            }
        }

        // Add form
        if (showAdd) {
            Spacer(Modifier.height(8.dp))
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {

                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        FilterChip(selected = addMode == 0, onClick = { addMode = 0 },
                            label = { Text("По URL") },
                            leadingIcon = { Icon(Icons.Default.Link, null, Modifier.size(16.dp)) })
                        FilterChip(selected = addMode == 1, onClick = { addMode = 1 },
                            label = { Text("Файл M3U") },
                            leadingIcon = { Icon(Icons.Default.FolderOpen, null, Modifier.size(16.dp)) })
                    }

                    OutlinedTextField(value = newName, onValueChange = { newName = it },
                        label = { Text("Название") }, modifier = Modifier.fillMaxWidth(),
                        singleLine = true)

                    if (addMode == 0) {
                        OutlinedTextField(value = newUrl, onValueChange = { newUrl = it },
                            label = { Text("M3U URL") }, placeholder = { Text("http://...") },
                            modifier = Modifier.fillMaxWidth(), singleLine = true)

                        Button(
                            onClick = {
                                scope.launch {
                                    adding = true; addError = null
                                    WsClient.playlistAdd(newUrl.trim(), newName.ifBlank { "Playlist" })
                                    kotlinx.coroutines.delay(2000)
                                    WsClient.playlistsGet()
                                    newUrl = ""; newName = ""; showAdd = false; adding = false
                                }
                            },
                            modifier = Modifier.fillMaxWidth(),
                            enabled  = newUrl.isNotBlank() && !adding,
                        ) {
                            if (adding) {
                                CircularProgressIndicator(Modifier.size(18.dp), strokeWidth = 2.dp)
                                Spacer(Modifier.width(8.dp))
                            }
                            Text("Добавить")
                        }
                    } else {
                        Button(
                            onClick = { fileLauncher.launch("*/*") },
                            modifier = Modifier.fillMaxWidth(),
                            enabled  = !adding,
                        ) {
                            if (adding) {
                                CircularProgressIndicator(Modifier.size(18.dp), strokeWidth = 2.dp)
                                Spacer(Modifier.width(8.dp))
                            }
                            Icon(Icons.Default.FolderOpen, null)
                            Spacer(Modifier.width(8.dp))
                            Text("Выбрать M3U файл")
                        }
                    }

                    addError?.let {
                        Text(it, color = MaterialTheme.colorScheme.error,
                            style = MaterialTheme.typography.bodySmall)
                    }
                }
            }
            Spacer(Modifier.height(8.dp))
        }

        if (loading) LinearProgressIndicator(Modifier.fillMaxWidth().padding(bottom = 4.dp))

        LazyColumn(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            items(playlists, key = { it.id }) { pl ->
                PlaylistCard(
                    playlist = pl,
                    onClick  = { onOpenChannels(pl) },
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
                    Text("Нет плейлистов. Добавьте M3U ссылку или файл.",
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(top = 32.dp))
                }
            }
        }
    }
}

@Composable
private fun PlaylistCard(
    playlist: IptvPlaylist,
    onClick: () -> Unit,
    onDelete: () -> Unit,
) {
    Card(Modifier.fillMaxWidth().clickable(onClick = onClick)) {
        Row(Modifier.padding(horizontal = 12.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically) {
            Icon(Icons.Default.Tv, null, Modifier.size(36.dp).padding(end = 12.dp),
                tint = MaterialTheme.colorScheme.primary)
            Column(Modifier.weight(1f)) {
                Text(playlist.name, style = MaterialTheme.typography.bodyLarge,
                    maxLines = 1, overflow = TextOverflow.Ellipsis)
                Text("${playlist.channelCount} каналов",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            Icon(Icons.Default.ChevronRight, "Открыть",
                tint = MaterialTheme.colorScheme.onSurfaceVariant)
            IconButton(onClick = onDelete) {
                Icon(Icons.Default.Delete, "Удалить",
                    tint = MaterialTheme.colorScheme.error)
            }
        }
    }
}

// ── Channel list ──────────────────────────────────────────────────────────────

@Composable
private fun ChannelListScreen(playlist: IptvPlaylist, onBack: () -> Unit) {
    val scope = rememberCoroutineScope()
    var channels by remember { mutableStateOf<List<IptvChannel>>(emptyList()) }
    var loading  by remember { mutableStateOf(true) }
    var query    by remember { mutableStateOf("") }

    LaunchedEffect(playlist.id) {
        launch {
            WsClient.iptvChannelsFlow.collect { (plId, list) ->
                if (plId == playlist.id || plId.isEmpty()) {
                    channels = list; loading = false
                }
            }
        }
        WsClient.iptvChannelsGet(playlist.id)
    }

    val filtered = remember(channels, query) {
        if (query.isBlank()) channels
        else channels.filter {
            it.name.contains(query, ignoreCase = true) ||
            it.group.contains(query, ignoreCase = true)
        }
    }

    Column(Modifier.fillMaxSize()) {

        // Top bar
        Row(Modifier.fillMaxWidth().padding(horizontal = 4.dp, vertical = 4.dp),
            verticalAlignment = Alignment.CenterVertically) {
            IconButton(onClick = onBack) { Icon(Icons.Default.ArrowBack, "Назад") }
            Column(Modifier.weight(1f)) {
                Text(playlist.name, style = MaterialTheme.typography.titleMedium,
                    maxLines = 1, overflow = TextOverflow.Ellipsis)
                if (!loading)
                    Text("${filtered.size} / ${channels.size} каналов",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }

        // Search
        OutlinedTextField(
            value = query, onValueChange = { query = it },
            placeholder = { Text("Поиск...") },
            leadingIcon = { Icon(Icons.Default.Search, null) },
            trailingIcon = {
                if (query.isNotEmpty())
                    IconButton(onClick = { query = "" }) { Icon(Icons.Default.Clear, "Сброс") }
            },
            modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 4.dp),
            singleLine = true,
        )

        if (loading) LinearProgressIndicator(Modifier.fillMaxWidth())

        LazyColumn(
            contentPadding    = PaddingValues(12.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            items(filtered, key = { it.id }) { ch ->
                ChannelCard(channel = ch, onPlay = {
                    scope.launch { WsClient.play(ch.url, type = "iptv") }
                })
            }
            if (filtered.isEmpty() && !loading) {
                item {
                    Text(
                        if (query.isBlank()) "Каналы не найдены (сервер вернул 0)"
                        else "Ничего по \"$query\"",
                        Modifier.padding(top = 24.dp),
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
        }
    }
}

@Composable
private fun ChannelCard(channel: IptvChannel, onPlay: () -> Unit) {
    Card(Modifier.fillMaxWidth().clickable(onClick = onPlay)) {
        Row(Modifier.padding(horizontal = 12.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(channel.name, style = MaterialTheme.typography.bodyMedium,
                    maxLines = 1, overflow = TextOverflow.Ellipsis)
                if (channel.group.isNotBlank())
                    Text(channel.group, style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 1)
            }
            Icon(Icons.Default.PlayArrow, "Играть",
                tint = MaterialTheme.colorScheme.primary)
        }
    }
}

// ── M3U parser (Android-side, for file import) ────────────────────────────────

private fun parseM3u(content: String): List<Map<String, String>> {
    val result      = mutableListOf<Map<String, String>>()
    val groupRegex  = Regex("""group-title="([^"]*)"""")
    var name  = ""
    var group = ""

    for (line in content.lineSequence()) {
        val t = line.trim()
        when {
            t.startsWith("#EXTINF:") -> {
                group = groupRegex.find(t)?.groupValues?.getOrNull(1) ?: ""
                name  = t.substringAfterLast(',').trim()
            }
            t.isNotEmpty() && !t.startsWith('#') && name.isNotEmpty() -> {
                result += mapOf("name" to name, "url" to t, "group" to group)
                name = ""; group = ""
            }
        }
    }
    return result
}
