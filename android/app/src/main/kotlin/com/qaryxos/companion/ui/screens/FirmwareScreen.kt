package com.qaryxos.companion.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.SystemUpdate
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.qaryxos.companion.data.api.ApiClient
import com.qaryxos.companion.data.models.OtaCheckResponse
import com.qaryxos.companion.data.models.OtaStatus
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

@Composable
fun FirmwareScreen() {
    val context = LocalContext.current
    val scope   = rememberCoroutineScope()
    val api     = ApiClient.getApiForContext(context)

    var checkResult by remember { mutableStateOf<OtaCheckResponse?>(null) }
    var otaStatus   by remember { mutableStateOf<OtaStatus?>(null) }
    var loading     by remember { mutableStateOf(false) }
    var error       by remember { mutableStateOf<String?>(null) }

    // Poll OTA status when update is running
    LaunchedEffect(otaStatus?.running) {
        if (otaStatus?.running == true) {
            while (true) {
                delay(1500)
                otaStatus = runCatching { api.otaStatus().body() }.getOrNull()
                if (otaStatus?.running == false) break
            }
        }
    }

    Column(
        Modifier.fillMaxSize().padding(24.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text("Firmware", style = MaterialTheme.typography.headlineSmall)

        // Current version card
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text("Installed version", style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.primary)
                Text(
                    checkResult?.current ?: "—",
                    style = MaterialTheme.typography.headlineMedium,
                )
            }
        }

        // Check button
        Button(
            onClick = {
                scope.launch {
                    loading = true; error = null
                    checkResult = runCatching { api.otaCheck().body() }
                        .onFailure { error = it.message }
                        .getOrNull()
                    loading = false
                }
            },
            modifier = Modifier.fillMaxWidth(),
            enabled = !loading,
        ) {
            if (loading) {
                CircularProgressIndicator(Modifier.size(18.dp), strokeWidth = 2.dp)
                Spacer(Modifier.width(8.dp))
            }
            Text("Check for updates")
        }

        // Result
        checkResult?.let { result ->
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(
                    containerColor = if (result.updateAvailable)
                        MaterialTheme.colorScheme.primaryContainer
                    else MaterialTheme.colorScheme.surfaceVariant
                ),
            ) {
                Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    if (result.updateAvailable) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Icon(Icons.Default.SystemUpdate, null,
                                tint = MaterialTheme.colorScheme.primary)
                            Spacer(Modifier.width(8.dp))
                            Text("Update available: ${result.latest}",
                                style = MaterialTheme.typography.titleMedium)
                        }
                        if (result.releaseNotes.isNotEmpty()) {
                            Text(result.releaseNotes,
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                        Spacer(Modifier.height(4.dp))
                        Button(
                            onClick = {
                                scope.launch {
                                    runCatching { api.otaUpdate() }
                                    otaStatus = OtaStatus(true, "Starting...", null, null)
                                }
                            },
                            modifier = Modifier.fillMaxWidth(),
                            enabled = otaStatus?.running != true,
                        ) { Text("Update to ${result.latest}") }
                    } else {
                        Text("✓ Already up to date (${result.current})",
                            color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                }
            }
        }

        // OTA progress
        otaStatus?.let { st ->
            if (st.running || st.progress.isNotEmpty()) {
                Card(Modifier.fillMaxWidth()) {
                    Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        Text("Update progress", style = MaterialTheme.typography.labelMedium)
                        if (st.running) LinearProgressIndicator(Modifier.fillMaxWidth())
                        Text(st.progress)
                        st.error?.let { Text("Error: $it", color = MaterialTheme.colorScheme.error) }
                        st.lastUpdated?.let { Text("Updated to: $it", color = MaterialTheme.colorScheme.primary) }
                    }
                }
            }
        }

        error?.let {
            Text("Error: $it", color = MaterialTheme.colorScheme.error)
        }
    }
}
