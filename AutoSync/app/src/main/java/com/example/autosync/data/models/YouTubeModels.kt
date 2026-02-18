package com.example.autosync.data.models

import kotlinx.serialization.Serializable

//for playlist listing
@Serializable
data class YouTubePlaylistResponse(
    val items: List<PlaylistItem>
)

@Serializable
data class PlaylistItem(
    val id: String,
    val snippet: PlaylistSnippet
)

@Serializable
data class PlaylistSnippet(
    val title: String,
    val description: String
)


//for video listing
@Serializable
data class YouTubeVideoResponse(
    val items: List<VideoItem>
)

@Serializable
data class VideoItem(
    val id: String,
    val snippet: VideoSnippet
)

@Serializable
data class VideoSnippet(
    val title: String,
    val description: String = "",
    val resourceId: VideoResourceId = VideoResourceId("")
)

@Serializable
data class VideoResourceId(
    val videoId: String
)