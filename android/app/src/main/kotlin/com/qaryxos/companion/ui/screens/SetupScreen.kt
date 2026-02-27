package com.qaryxos.companion.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.qaryxos.companion.data.api.ApiClient
import kotlinx.coroutines.launch

@Composable
fun SetupScreen(onConnected: () -> Unit) {
    val context = LocalContext.current
    val scope   = rememberCoroutineScope()

    var ip      by remember { mutableStateOf(ApiClient.getSavedIp(context)) }
    var testing by remember { mutableStateOf(false) }
    var error   by remember { mutableStateOf<String?>(null) }

    Column(
        Modifier.fillMaxSize().padding(32.dp),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text("QaryxOS", style = MaterialTheme.typography.displaySmall)
        Text("Companion App", style = MaterialTheme.typography.titleMedium,
            color = MaterialTheme.colorScheme.primary)

        Spacer(Modifier.height(48.dp))

        Text(
            "Enter the IP address of your QaryxOS device on the local network",
            textAlign = TextAlign.Center,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )

        Spacer(Modifier.height(24.dp))

        OutlinedTextField(
            value = ip,
            onValueChange = { ip = it; error = null },
            label = { Text("Device IP Address") },
            placeholder = { Text("192.168.1.100") },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
            isError = error != null,
            supportingText = error?.let { { Text(it) } },
        )

        Spacer(Modifier.height(16.dp))

        Button(
            onClick = {
                if (ip.isBlank()) {
                    error = "Enter an IP address"
                    return@Button
                }
                scope.launch {
                    testing = true; error = null
                    val ok = runCatching {
                        ApiClient.getApi("http://${ip.trim()}:${ApiClient.DEFAULT_PORT}/")
                            .health().isSuccessful
                    }.getOrDefault(false)

                    if (ok) {
                        ApiClient.saveDeviceIp(context, ip.trim())
                        onConnected()
                    } else {
                        error = "Cannot connect to ${ip.trim()}:${ApiClient.DEFAULT_PORT}. Check the IP and make sure the device is on."
                    }
                    testing = false
                }
            },
            modifier = Modifier.fillMaxWidth(),
            enabled = !testing,
        ) {
            if (testing) {
                CircularProgressIndicator(Modifier.size(18.dp), strokeWidth = 2.dp)
                Spacer(Modifier.width(8.dp))
            }
            Text("Connect")
        }
    }
}
