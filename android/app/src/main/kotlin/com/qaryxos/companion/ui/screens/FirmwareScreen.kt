package com.qaryxos.companion.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.RestartAlt
import androidx.compose.material.icons.filled.SystemUpdate
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.qaryxos.companion.data.ws.WsClient
import kotlinx.coroutines.launch

@Composable
fun FirmwareScreen() {
    val scope = rememberCoroutineScope()
    var showReboot by remember { mutableStateOf(false) }

    Column(
        Modifier.fillMaxSize().padding(24.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text("Firmware", style = MaterialTheme.typography.headlineSmall)

        // OTA info card
        Card(Modifier.fillMaxWidth()) {
            Row(Modifier.padding(16.dp), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                Icon(Icons.Default.Info, null, tint = MaterialTheme.colorScheme.primary)
                Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Text("OTA Updates", style = MaterialTheme.typography.titleMedium)
                    Text(
                        "Firmware updates are applied via the ota/updater.py CLI tool on the device. " +
                        "Connect over SSH and run:\n\n" +
                        "  python3 /opt/qaryxos/ota/updater.py update",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
        }

        // Reboot card
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Icon(Icons.Default.RestartAlt, null, tint = MaterialTheme.colorScheme.primary)
                    Text("Device control", style = MaterialTheme.typography.titleMedium)
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
