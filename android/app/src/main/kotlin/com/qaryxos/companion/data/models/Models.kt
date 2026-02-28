package com.qaryxos.companion.data.models

import com.google.gson.annotations.SerializedName

// ── Requests ──────────────────────────────────────────────────────────────────

data class PlayRequest(val url: String, val type: String? = null, val resume: Boolean = false)
data class SeekRequest(val seconds: Float)
data class VolumeRequest(val level: Int)
data class KeyRequest(val key: String)
data class AddChannelRequest(val url: String)
data class AddPlaylistRequest(val url: String, val name: String)

// ── Responses ─────────────────────────────────────────────────────────────────

data class OkResponse(val ok: Boolean, val message: String? = null)
data class PauseResponse(val ok: Boolean, val paused: Boolean)

data class PlaybackStatus(
    val state: String,          // idle | playing | paused | error
    val url: String?,
    val position: Float,
    val duration: Float,
    val volume: Int,
    val paused: Boolean,
)

data class HealthResponse(
    val status: String,
    val version: String,
    val mpv: String,
    @SerializedName("api_port") val apiPort: Int,
)

// ── YouTube ───────────────────────────────────────────────────────────────────

data class YoutubeChannel(
    val id: String,
    val name: String,
    val url: String,
    @SerializedName("added_at") val addedAt: Double = 0.0,
    @SerializedName("updated_at") val updatedAt: Double = 0.0,
)

data class YoutubeVideo(
    val id: String,
    val title: String,
    val url: String,
    @SerializedName("channel_id") val channelId: String,
    @SerializedName("channel_name") val channelName: String,
    val duration: Int = 0,
    val thumbnail: String = "",
)

// ── IPTV ──────────────────────────────────────────────────────────────────────

data class IptvPlaylist(
    val id: String,
    val name: String,
    val url: String,
    @SerializedName("updated_at") val updatedAt: Double = 0.0,
    @SerializedName("channel_count") val channelCount: Int = 0,
)

data class IptvChannel(
    val id: String,
    val name: String,
    val url: String,
    val group: String = "",
    val logo: String = "",
)

// ── History ───────────────────────────────────────────────────────────────────

data class HistoryEntry(
    val url: String,
    val title: String,
    @SerializedName("content_type") val contentType: String,
    @SerializedName("channel_name") val channelName: String = "",
    val thumbnail: String = "",
    val duration: Float = 0f,
    val position: Float = 0f,
    @SerializedName("played_at") val playedAt: Double = 0.0,
)

// ── WebSocket server push ─────────────────────────────────────────────────────

/** Status message pushed by the Qaryx server every ~500 ms. */
data class StatusMsg(
    val type: String = "status",
    val state: String = "idle",     // idle | playing | paused | error
    val url: String = "",
    val position: Double = 0.0,
    val duration: Double = 0.0,
    val volume: Int = 80,
    val paused: Boolean = false,
)

// ── OTA ───────────────────────────────────────────────────────────────────────

data class OtaCheckResponse(
    val current: String,
    val latest: String,
    @SerializedName("update_available") val updateAvailable: Boolean,
    @SerializedName("release_notes") val releaseNotes: String = "",
)

data class OtaStatus(
    val running: Boolean,
    val progress: String,
    val error: String?,
    @SerializedName("last_updated") val lastUpdated: String?,
)
