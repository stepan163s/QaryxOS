package com.qaryxos.companion

import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import com.qaryxos.companion.data.ws.WsClient
import com.qaryxos.companion.ui.screens.*
import com.qaryxos.companion.ui.theme.QaryxOSTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        val sharedUrl = intent
            .takeIf { it.action == Intent.ACTION_SEND }
            ?.getStringExtra(Intent.EXTRA_TEXT)

        setContent {
            QaryxOSTheme {
                QaryxApp(sharedUrl = sharedUrl)
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        WsClient.disconnect()
    }
}

@Composable
private fun QaryxApp(sharedUrl: String?) {
    val context = LocalContext.current
    val nav     = rememberNavController()

    // Auto-connect on launch if device IP is saved
    LaunchedEffect(Unit) {
        val savedIp = WsClient.getSavedIp(context)
        if (savedIp.isNotEmpty()) {
            WsClient.connect(savedIp)
        }
    }

    NavHost(
        navController = nav,
        startDestination = if (WsClient.hasDevice(context)) "main" else "setup",
    ) {
        composable("setup") {
            SetupScreen(onConnected = {
                nav.navigate("main") { popUpTo("setup") { inclusive = true } }
            })
        }
        composable("main") {
            LaunchedEffect(sharedUrl) {
                if (!sharedUrl.isNullOrBlank()) {
                    WsClient.play(sharedUrl)
                }
            }
            MainTabs(onResetDevice = {
                nav.navigate("setup") { popUpTo("main") { inclusive = true } }
            })
        }
    }
}

@Composable
private fun MainTabs(onResetDevice: () -> Unit) {
    var tab by remember { mutableIntStateOf(0) }

    val items = listOf(
        Triple("Remote",   Icons.Default.SportsEsports, { tab = 0 }),
        Triple("History",  Icons.Default.History,       { tab = 1 }),
        Triple("Channels", Icons.Default.PlaylistPlay,  { tab = 2 }),
        Triple("Firmware", Icons.Default.SystemUpdate,  { tab = 3 }),
        Triple("Settings", Icons.Default.Settings,      { tab = 4 }),
    )

    Scaffold(
        bottomBar = {
            NavigationBar {
                items.forEachIndexed { i, (label, icon, onClick) ->
                    NavigationBarItem(
                        selected = tab == i,
                        onClick = onClick,
                        icon = { Icon(icon, label) },
                        label = { Text(label) },
                    )
                }
            }
        }
    ) { padding ->
        Box(Modifier.padding(padding)) {
            when (tab) {
                0 -> RemoteScreen()
                1 -> HistoryScreen()
                2 -> ChannelManagerScreen()
                3 -> FirmwareScreen()
                4 -> SettingsScreen(onResetDevice = onResetDevice)
            }
        }
    }
}
