package com.qaryxos.companion.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.qaryxos.companion.data.ws.WsClient
import com.qaryxos.companion.data.ws.WsState
import kotlinx.coroutines.launch

@Composable
fun SettingsScreen(onResetDevice: () -> Unit) {
    val context  = LocalContext.current
    val scope    = rememberCoroutineScope()
    val wsState  by WsClient.state.collectAsState()
    val services by WsClient.servicesFlow.collectAsState()

    // Request service state when connected
    LaunchedEffect(wsState) {
        if (wsState == WsState.CONNECTED) WsClient.serviceGet()
    }

    var ip         by remember { mutableStateOf(WsClient.getSavedIp(context)) }
    var editIp     by remember { mutableStateOf(false) }
    var newIp      by remember { mutableStateOf("") }
    var connecting by remember { mutableStateOf(false) }
    var showReboot by remember { mutableStateOf(false) }

    Column(
        Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(20.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("Settings", style = MaterialTheme.typography.headlineSmall)

        // ── Device connection ─────────────────────────────────────────────────
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Default.Wifi, null, tint = when (wsState) {
                        WsState.CONNECTED    -> MaterialTheme.colorScheme.primary
                        WsState.CONNECTING   -> MaterialTheme.colorScheme.tertiary
                        WsState.DISCONNECTED -> MaterialTheme.colorScheme.error
                    })
                    Spacer(Modifier.width(8.dp))
                    Text("Device", style = MaterialTheme.typography.titleMedium)
                    Spacer(Modifier.weight(1f))
                    Text(
                        when (wsState) {
                            WsState.CONNECTED    -> "Connected"
                            WsState.CONNECTING   -> "Connecting…"
                            WsState.DISCONNECTED -> "Disconnected"
                        },
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                HorizontalDivider()

                if (!editIp) {
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically) {
                        Column {
                            Text("IP Address", style = MaterialTheme.typography.labelMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant)
                            Text(ip.ifEmpty { "Not set" }, style = MaterialTheme.typography.bodyLarge)
                        }
                        IconButton(onClick = { newIp = ip; editIp = true }) {
                            Icon(Icons.Default.Edit, "Edit IP")
                        }
                    }
                } else {
                    OutlinedTextField(
                        value = newIp,
                        onValueChange = { newIp = it },
                        label = { Text("Device IP") },
                        placeholder = { Text("192.168.1.100") },
                        modifier = Modifier.fillMaxWidth(),
                        singleLine = true,
                    )
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        OutlinedButton(onClick = { editIp = false }, Modifier.weight(1f)) {
                            Text("Cancel")
                        }
                        Button(
                            onClick = {
                                scope.launch {
                                    connecting = true
                                    WsClient.disconnect()
                                    WsClient.saveIp(context, newIp.trim())
                                    ip = newIp.trim()
                                    WsClient.connect(ip)
                                    editIp = false
                                    connecting = false
                                }
                            },
                            modifier = Modifier.weight(1f),
                            enabled = !connecting,
                        ) {
                            if (connecting) CircularProgressIndicator(Modifier.size(16.dp), strokeWidth = 2.dp)
                            else Text("Save & Connect")
                        }
                    }
                }

                // Reconnect button when disconnected
                if (wsState == WsState.DISCONNECTED && ip.isNotEmpty() && !editIp) {
                    Button(onClick = { WsClient.connect(ip) }, Modifier.fillMaxWidth()) {
                        Icon(Icons.Default.Refresh, null)
                        Spacer(Modifier.width(8.dp))
                        Text("Reconnect")
                    }
                }
            }
        }

        // ── Services ──────────────────────────────────────────────────────────
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Default.Tune, null, tint = MaterialTheme.colorScheme.primary)
                    Spacer(Modifier.width(8.dp))
                    Text("Services", style = MaterialTheme.typography.titleMedium)
                    Spacer(Modifier.weight(1f))
                    if (wsState == WsState.CONNECTED)
                        IconButton(onClick = { WsClient.serviceGet() }, Modifier.size(32.dp)) {
                            Icon(Icons.Default.Refresh, "Refresh", Modifier.size(18.dp))
                        }
                }
                HorizontalDivider()

                // Xray
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text("Xray proxy", style = MaterialTheme.typography.bodyLarge)
                        Text(
                            when {
                                services == null               -> "unknown"
                                services!!.xray.active         -> "running"
                                else                           -> "stopped"
                            },
                            style = MaterialTheme.typography.bodySmall,
                            color = if (services?.xray?.active == true)
                                MaterialTheme.colorScheme.primary
                            else MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                    Switch(
                        checked = services?.xray?.enabled == true,
                        onCheckedChange = { on ->
                            scope.launch { WsClient.serviceSet("xray", on) }
                        },
                        enabled = wsState == WsState.CONNECTED,
                    )
                }

                HorizontalDivider()

                // Tailscale
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text("Tailscale VPN", style = MaterialTheme.typography.bodyLarge)
                        Text(
                            when {
                                services == null                    -> "unknown"
                                services!!.tailscaled.active        -> "connected"
                                else                                -> "offline"
                            },
                            style = MaterialTheme.typography.bodySmall,
                            color = if (services?.tailscaled?.active == true)
                                MaterialTheme.colorScheme.primary
                            else MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                    Switch(
                        checked = services?.tailscaled?.enabled == true,
                        onCheckedChange = { on ->
                            scope.launch { WsClient.serviceSet("tailscaled", on) }
                        },
                        enabled = wsState == WsState.CONNECTED,
                    )
                }
            }
        }

        // ── Quick actions ─────────────────────────────────────────────────────
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Default.Bolt, null, tint = MaterialTheme.colorScheme.primary)
                    Spacer(Modifier.width(8.dp))
                    Text("Device actions", style = MaterialTheme.typography.titleMedium)
                }
                HorizontalDivider()
                OutlinedButton(
                    onClick = { showReboot = true },
                    modifier = Modifier.fillMaxWidth(),
                    colors = ButtonDefaults.outlinedButtonColors(
                        contentColor = MaterialTheme.colorScheme.error),
                ) {
                    Icon(Icons.Default.RestartAlt, null)
                    Spacer(Modifier.width(8.dp))
                    Text("Reboot device")
                }
            }
        }

        // ── About ─────────────────────────────────────────────────────────────
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Default.Info, null, tint = MaterialTheme.colorScheme.primary)
                    Spacer(Modifier.width(8.dp))
                    Text("About", style = MaterialTheme.typography.titleMedium)
                }
                HorizontalDivider()
                Text("Qaryx Remote", style = MaterialTheme.typography.bodyMedium)
                Text("Minimal Local Stream Box controller",
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    style = MaterialTheme.typography.bodySmall)
                Text("github.com/stepan163s/QaryxOS",
                    color = MaterialTheme.colorScheme.primary,
                    style = MaterialTheme.typography.bodySmall)
            }
        }

        // Forget device
        TextButton(
            onClick = {
                WsClient.disconnect()
                WsClient.saveIp(context, "")
                onResetDevice()
            },
            modifier = Modifier.align(Alignment.CenterHorizontally),
        ) {
            Text("Forget device", color = MaterialTheme.colorScheme.error)
        }
    }

    if (showReboot) {
        AlertDialog(
            onDismissRequest = { showReboot = false },
            icon = { Icon(Icons.Default.RestartAlt, null) },
            title = { Text("Reboot device?") },
            text = { Text("The TV will go dark for ~8 seconds while the device reboots.") },
            confirmButton = {
                Button(
                    onClick = {
                        scope.launch { WsClient.reboot() }
                        showReboot = false
                    },
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.error),
                ) { Text("Reboot") }
            },
            dismissButton = {
                TextButton(onClick = { showReboot = false }) { Text("Cancel") }
            },
        )
    }
}
