package com.qaryxos.companion.discovery

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import java.util.concurrent.atomic.AtomicBoolean

private const val SERVICE_TYPE = "_qaryxos._tcp."

data class DiscoveredDevice(val name: String, val host: String, val port: Int)

fun discoverQaryxDevices(context: Context): Flow<List<DiscoveredDevice>> = callbackFlow {
    val nsdManager = context.getSystemService(Context.NSD_SERVICE) as NsdManager
    val devices    = mutableMapOf<String, DiscoveredDevice>()
    val resolving  = AtomicBoolean(false)

    fun resolve(info: NsdServiceInfo) {
        if (!resolving.compareAndSet(false, true)) return
        nsdManager.resolveService(info, object : NsdManager.ResolveListener {
            override fun onResolveFailed(i: NsdServiceInfo, code: Int) {
                resolving.set(false)
            }
            override fun onServiceResolved(i: NsdServiceInfo) {
                resolving.set(false)
                val host = i.host?.hostAddress ?: return
                devices[i.serviceName] = DiscoveredDevice(i.serviceName, host, i.port)
                trySend(devices.values.toList())
            }
        })
    }

    val discoveryListener = object : NsdManager.DiscoveryListener {
        override fun onDiscoveryStarted(t: String) {}
        override fun onDiscoveryStopped(t: String) {}
        override fun onStartDiscoveryFailed(t: String, code: Int) { close() }
        override fun onStopDiscoveryFailed(t: String, code: Int) {}
        override fun onServiceFound(info: NsdServiceInfo) { resolve(info) }
        override fun onServiceLost(info: NsdServiceInfo) {
            devices.remove(info.serviceName)
            trySend(devices.values.toList())
        }
    }

    nsdManager.discoverServices(SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD, discoveryListener)

    awaitClose {
        runCatching { nsdManager.stopServiceDiscovery(discoveryListener) }
    }
}
