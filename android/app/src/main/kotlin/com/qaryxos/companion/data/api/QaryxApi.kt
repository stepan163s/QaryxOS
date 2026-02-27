package com.qaryxos.companion.data.api

import com.qaryxos.companion.data.models.*
import retrofit2.Response
import retrofit2.http.*

interface QaryxApi {

    // ── Playback ─────────────────────────────────────────────────────────────
    @POST("play")
    suspend fun play(@Body req: PlayRequest): Response<OkResponse>

    @POST("pause")
    suspend fun pause(): Response<PauseResponse>

    @POST("stop")
    suspend fun stop(): Response<OkResponse>

    @POST("seek")
    suspend fun seek(@Body req: SeekRequest): Response<OkResponse>

    @POST("volume")
    suspend fun volume(@Body req: VolumeRequest): Response<OkResponse>

    @GET("status")
    suspend fun status(): Response<PlaybackStatus>

    // ── UI Navigation ────────────────────────────────────────────────────────
    @POST("ui/key")
    suspend fun sendKey(@Body req: KeyRequest): Response<OkResponse>

    @GET("ui/state")
    suspend fun uiState(): Response<Map<String, String>>

    // ── YouTube ──────────────────────────────────────────────────────────────
    @GET("youtube/channels")
    suspend fun getChannels(): Response<List<YoutubeChannel>>

    @POST("youtube/channels")
    suspend fun addChannel(@Body req: AddChannelRequest): Response<YoutubeChannel>

    @DELETE("youtube/channels/{id}")
    suspend fun removeChannel(@Path("id") id: String): Response<OkResponse>

    @GET("youtube/feed")
    suspend fun getFeed(@Query("limit") limit: Int = 30): Response<List<YoutubeVideo>>

    @POST("youtube/refresh")
    suspend fun refreshYoutube(): Response<OkResponse>

    // ── IPTV ─────────────────────────────────────────────────────────────────
    @GET("iptv/playlists")
    suspend fun getPlaylists(): Response<List<IptvPlaylist>>

    @POST("iptv/playlists")
    suspend fun addPlaylist(@Body req: AddPlaylistRequest): Response<IptvPlaylist>

    @DELETE("iptv/playlists/{id}")
    suspend fun removePlaylist(@Path("id") id: String): Response<OkResponse>

    @GET("iptv/channels")
    suspend fun getIptvChannels(
        @Query("group") group: String? = null
    ): Response<List<IptvChannel>>

    @GET("iptv/groups")
    suspend fun getGroups(): Response<List<String>>

    @POST("iptv/play/{channelId}")
    suspend fun playIptvChannel(@Path("channelId") id: String): Response<OkResponse>

    // ── OTA ──────────────────────────────────────────────────────────────────
    @GET("ota/check")
    suspend fun otaCheck(): Response<OtaCheckResponse>

    @POST("ota/update")
    suspend fun otaUpdate(): Response<OkResponse>

    @GET("ota/status")
    suspend fun otaStatus(): Response<OtaStatus>

    // ── System ───────────────────────────────────────────────────────────────
    @GET("health")
    suspend fun health(): Response<HealthResponse>
}
