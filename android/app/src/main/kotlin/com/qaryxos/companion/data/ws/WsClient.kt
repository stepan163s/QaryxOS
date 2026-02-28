package com.qaryxos.companion.data.ws

import android.content.Context
import com.google.gson.Gson
import com.google.gson.GsonBuilder
import com.google.gson.JsonParser
import com.qaryxos.companion.data.models.HistoryEntry
import com.qaryxos.companion.data.models.IptvPlaylist
import com.qaryxos.companion.data.models.ServicesMsg
import com.qaryxos.companion.data.models.StatusMsg
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import java.util.concurrent.TimeUnit

enum class WsState { DISCONNECTED, CONNECTING, CONNECTED }

object WsClient {

    private val gson = GsonBuilder().disableHtmlEscaping().create()

    private val client = OkHttpClient.Builder()
        .connectTimeout(5, TimeUnit.SECONDS)
        .readTimeout(0, TimeUnit.SECONDS)       // WebSocket must have no read timeout
        .pingInterval(20, TimeUnit.SECONDS)
        .build()

    private var ws: WebSocket? = null

    private const val PREFS_NAME = "qaryxos_prefs"
    private const val KEY_DEVICE_IP = "device_ip"
    const val DEFAULT_PORT = 8080

    // ── Connection state ──────────────────────────────────────────────────────

    private val _state = MutableStateFlow(WsState.DISCONNECTED)
    val state: StateFlow<WsState> = _state.asStateFlow()

    val isConnected: Boolean get() = _state.value == WsState.CONNECTED

    // ── Server push flows ─────────────────────────────────────────────────────

    /** Status pushed by server every ~500 ms while connected. */
    private val _status = MutableStateFlow<StatusMsg?>(null)
    val status: StateFlow<StatusMsg?> = _status.asStateFlow()

    /** One-shot history response (emitted after historyGet()). */
    private val _historyFlow = MutableSharedFlow<List<HistoryEntry>>(extraBufferCapacity = 1)
    val historyFlow: SharedFlow<List<HistoryEntry>> = _historyFlow.asSharedFlow()

    /** One-shot playlists response (emitted after playlistsGet()). */
    private val _playlistsFlow = MutableSharedFlow<List<IptvPlaylist>>(extraBufferCapacity = 1)
    val playlistsFlow: SharedFlow<List<IptvPlaylist>> = _playlistsFlow.asSharedFlow()

    /** Error messages from the server (e.g. yt-dlp resolve failure). */
    private val _errorFlow = MutableSharedFlow<String>(extraBufferCapacity = 4)
    val errorFlow: SharedFlow<String> = _errorFlow.asSharedFlow()

    /** Service states (xray + tailscaled) pushed after service_get / service_set. */
    private val _servicesFlow = MutableStateFlow<ServicesMsg?>(null)
    val servicesFlow: StateFlow<ServicesMsg?> = _servicesFlow.asStateFlow()

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    fun connect(host: String, port: Int = DEFAULT_PORT) {
        ws?.cancel()
        _state.value = WsState.CONNECTING
        // Wrap IPv6 addresses in brackets for proper URL formatting
        val urlHost = if (host.contains(":")) "[$host]" else host
        val req = Request.Builder().url("ws://$urlHost:$port/").build()
        ws = client.newWebSocket(req, listener)
    }

    fun disconnect() {
        ws?.close(1000, "user disconnect")
        ws = null
        _state.value = WsState.DISCONNECTED
        _status.value = null
    }

    // ── Persistence (same prefs key as old ApiClient — existing saved IPs work) ─

    fun saveIp(context: Context, ip: String) {
        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            .edit().putString(KEY_DEVICE_IP, ip).apply()
    }

    fun getSavedIp(context: Context): String =
        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            .getString(KEY_DEVICE_IP, "") ?: ""

    fun hasDevice(context: Context): Boolean = getSavedIp(context).isNotEmpty()

    // ── Send ──────────────────────────────────────────────────────────────────

    private fun send(obj: Any) {
        ws?.send(gson.toJson(obj))
    }

    // Playback
    fun play(url: String, type: String = "direct", resume: Boolean = false) =
        send(mapOf("cmd" to "play", "url" to url, "type" to type, "resume" to resume))

    fun pause()               = send(mapOf("cmd" to "pause"))
    fun stop()                = send(mapOf("cmd" to "stop"))
    fun seek(seconds: Double) = send(mapOf("cmd" to "seek", "seconds" to seconds))
    fun volume(level: Int)    = send(mapOf("cmd" to "volume", "level" to level))

    // Navigation
    fun sendKey(key: String)      = send(mapOf("cmd" to "key", "key" to key))
    fun navigate(screen: String)  = send(mapOf("cmd" to "navigate", "screen" to screen))

    // History
    fun historyGet(limit: Int = 30) = send(mapOf("cmd" to "history_get", "limit" to limit))
    fun historyClear()              = send(mapOf("cmd" to "history_clear"))

    // IPTV playlists
    fun playlistsGet()                               = send(mapOf("cmd" to "playlists_get"))
    fun playlistAdd(url: String, name: String)       = send(mapOf("cmd" to "playlist_add", "url" to url, "name" to name))
    fun playlistDel(id: String)                      = send(mapOf("cmd" to "playlist_del", "id" to id))

    // Services
    fun serviceGet()                             = send(mapOf("cmd" to "service_get"))
    fun serviceSet(name: String, enabled: Boolean) =
        send(mapOf("cmd" to "service_set", "name" to name, "enabled" to enabled))

    // System
    fun reboot() = send(mapOf("cmd" to "reboot"))

    // ── WebSocket listener ────────────────────────────────────────────────────

    private val listener = object : WebSocketListener() {

        override fun onOpen(webSocket: WebSocket, response: Response) {
            _state.value = WsState.CONNECTED
        }

        override fun onMessage(webSocket: WebSocket, text: String) {
            try {
                val obj = JsonParser.parseString(text).asJsonObject
                when (obj.get("type")?.asString) {
                    "status" ->
                        _status.tryEmit(gson.fromJson(obj, StatusMsg::class.java))

                    "history" -> {
                        val list = obj.getAsJsonArray("entries")
                            ?.map { gson.fromJson(it, HistoryEntry::class.java) }
                            ?: emptyList()
                        _historyFlow.tryEmit(list)
                    }

                    "playlists" -> {
                        val list = obj.getAsJsonArray("playlists")
                            ?.map { gson.fromJson(it, IptvPlaylist::class.java) }
                            ?: emptyList()
                        _playlistsFlow.tryEmit(list)
                    }

                    "services" ->
                        _servicesFlow.value = gson.fromJson(obj, ServicesMsg::class.java)

                    "error" ->
                        _errorFlow.tryEmit(obj.get("msg")?.asString ?: "unknown error")
                }
            } catch (_: Exception) { /* ignore malformed frames */ }
        }

        override fun onClosing(webSocket: WebSocket, code: Int, reason: String) {
            webSocket.close(1000, null)
            _state.value = WsState.DISCONNECTED
            _status.value = null
        }

        override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
            _state.value = WsState.DISCONNECTED
            _status.value = null
        }
    }
}
