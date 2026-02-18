package com.example.autosync.data.network

import com.example.autosync.data.models.YouTubePlaylistResponse
import com.example.autosync.data.models.YouTubeVideoResponse
import retrofit2.http.GET
import retrofit2.http.Header
import retrofit2.http.Query

interface YouTubeApiService {
    @GET("youtube/v3/playlists")
    suspend fun getMyPlaylists(
        @Header("Authorization") token: String,
        @Query("part") part: String = "snippet",
        @Query("mine") mine: Boolean = true,
        @Query("maxResults") maxResults: Int = 50
    ): YouTubePlaylistResponse

    @GET("youtube/v3/playlistItems")
    suspend fun getPlaylistItems(
        @Header("Authorization") token: String,
        @Query("playlistId") playlistId: String,
        @Query("part") part: String = "snippet",
        @Query("maxResults") maxResults: Int = 50
    ): YouTubeVideoResponse
}