package com.qaryxos.companion

import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import com.qaryxos.companion.data.api.ApiClient
import com.qaryxos.companion.ui.screens.*

class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val sharedUrl = intent.takeIf { it.action == Intent.ACTION_SEND }
            ?.getStringExtra(Intent.EXTRA_TEXT)

        setContent {
            MaterialTheme {
                QaryxApp(sharedUrl = sharedUrl)
            }
        }
    }
}

@Composable
private fun QaryxApp(sharedUrl: String?) {
    val context = androidx.compose.ui.platform.LocalContext.current
    val nav     = rememberNavController()

    val hasDevice = ApiClient.hasDevice(context)

    NavHost(
        navController = nav,
        startDestination = if (hasDevice) "main" else "setup",
    ) {
        composable("setup") {
            SetupScreen(onConnected = { nav.navigate("main") { popUpTo("setup") { inclusive = true } } })
        }
        composable("main") {
            // Handle share intent
            LaunchedEffect(sharedUrl) {
                if (!sharedUrl.isNullOrBlank()) {
                    runCatching {
                        ApiClient.getApiForContext(context)
                            .play(com.qaryxos.companion.data.models.PlayRequest(sharedUrl))
                    }
                }
            }
            MainScreen(nav = nav)
        }
    }
}

@Composable
private fun MainScreen(nav: androidx.navigation.NavHostController) {
    var selectedTab by remember { mutableIntStateOf(0) }

    Scaffold(
        bottomBar = {
            NavigationBar {
                NavigationBarItem(
                    selected = selectedTab == 0,
                    onClick  = { selectedTab = 0 },
                    icon     = { Icon(Icons.Default.SportsEsports, null) },
                    label    = { Text("Remote") },
                )
                NavigationBarItem(
                    selected = selectedTab == 1,
                    onClick  = { selectedTab = 1 },
                    icon     = { Icon(Icons.Default.List, null) },
                    label    = { Text("Channels") },
                )
                NavigationBarItem(
                    selected = selectedTab == 2,
                    onClick  = { selectedTab = 2 },
                    icon     = { Icon(Icons.Default.SystemUpdate, null) },
                    label    = { Text("Firmware") },
                )
            }
        }
    ) { padding ->
        androidx.compose.foundation.layout.Box(
            modifier = androidx.compose.ui.Modifier.padding(padding)
        ) {
            when (selectedTab) {
                0 -> RemoteScreen()
                1 -> ChannelManagerScreen()
                2 -> FirmwareScreen()
            }
        }
    }
}
