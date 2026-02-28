package com.qaryxos.companion.ui.screens

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Tv
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.qaryxos.companion.data.ws.WsClient
import com.qaryxos.companion.data.ws.WsState
import com.qaryxos.companion.discovery.DiscoveredDevice
import com.qaryxos.companion.discovery.discoverQaryxDevices
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

@Composable
fun SetupScreen(onConnected: () -> Unit) {
    val context  = LocalContext.current
    val scope    = rememberCoroutineScope()
    val wsState  by WsClient.state.collectAsState()

    var ip           by remember { mutableStateOf(WsClient.getSavedIp(context)) }
    var connecting   by remember { mutableStateOf(false) }
    var error        by remember { mutableStateOf<String?>(null) }
    var discovered   by remember { mutableStateOf<List<DiscoveredDevice>>(emptyList()) }
    var discovering  by remember { mutableStateOf(false) }
    var discoveryJob by remember { mutableStateOf<Job?>(null) }

    // Watch WS state: once connected, navigate to main
    LaunchedEffect(wsState) {
        if (wsState == WsState.CONNECTED) {
            onConnected()
        } else if (wsState == WsState.DISCONNECTED && connecting) {
            // Connection attempt failed (listener fired onFailure)
            delay(200)
            if (WsClient.state.value == WsState.DISCONNECTED) {
                error = "Cannot connect to $ip:${WsClient.DEFAULT_PORT}"
                connecting = false
            }
        }
    }

    fun connect(targetIp: String) {
        connecting = true; error = null
        WsClient.saveIp(context, targetIp.trim())
        WsClient.connect(targetIp.trim())
        // onConnected() will be called via LaunchedEffect(wsState) when state → CONNECTED
    }

    Column(
        Modifier.fillMaxSize().padding(32.dp),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text("QaryxOS", style = MaterialTheme.typography.displaySmall)
        Text("Companion App", style = MaterialTheme.typography.titleMedium,
            color = MaterialTheme.colorScheme.primary)

        Spacer(Modifier.height(40.dp))

        // Auto-discovery
        OutlinedButton(
            onClick = {
                if (discovering) {
                    discoveryJob?.cancel(); discoveryJob = null
                    discovering = false; discovered = emptyList()
                } else {
                    discovered = emptyList(); discovering = true
                    discoveryJob = scope.launch {
                        discoverQaryxDevices(context).collect { list -> discovered = list }
                    }
                }
            },
            modifier = Modifier.fillMaxWidth(),
        ) {
            if (discovering) {
                CircularProgressIndicator(Modifier.size(16.dp), strokeWidth = 2.dp)
                Spacer(Modifier.width(8.dp))
                Text("Searching… tap to stop")
            } else {
                Icon(Icons.Default.Search, null)
                Spacer(Modifier.width(8.dp))
                Text("Search on network")
            }
        }

        AnimatedVisibility(discovered.isNotEmpty()) {
            Column(Modifier.fillMaxWidth().padding(top = 4.dp)) {
                discovered.forEach { device ->
                    ListItem(
                        headlineContent = { Text(device.name) },
                        supportingContent = { Text("${device.host}:${device.port}") },
                        leadingContent = {
                            Icon(Icons.Default.Tv, null,
                                tint = MaterialTheme.colorScheme.primary)
                        },
                        modifier = Modifier.clickable {
                            discoveryJob?.cancel(); discovering = false
                            ip = device.host
                            connect(device.host)
                        },
                    )
                    HorizontalDivider()
                }
            }
        }

        if (!discovering && discovered.isEmpty()) {
            Spacer(Modifier.height(24.dp))
            HorizontalDivider()
            Spacer(Modifier.height(16.dp))
            Text("Or enter IP manually",
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style = MaterialTheme.typography.bodySmall,
                textAlign = TextAlign.Center)
            Spacer(Modifier.height(12.dp))
        } else {
            Spacer(Modifier.height(20.dp))
        }

        OutlinedTextField(
            value = ip,
            onValueChange = { ip = it; error = null },
            label = { Text("Device IP Address") },
            placeholder = { Text("10.x.x.x") },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
            isError = error != null,
            supportingText = error?.let { { Text(it) } },
        )

        Spacer(Modifier.height(16.dp))

        Button(
            onClick = {
                if (ip.isBlank()) { error = "Enter an IP address"; return@Button }
                connect(ip)
            },
            modifier = Modifier.fillMaxWidth(),
            enabled = !connecting,
        ) {
            if (connecting) {
                CircularProgressIndicator(Modifier.size(18.dp), strokeWidth = 2.dp)
                Spacer(Modifier.width(8.dp))
            }
            Text("Connect")
        }
    }
}
