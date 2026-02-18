package com.example.autosync.data.models

import kotlinx.serialization.Serializable

// Model to send when UPDATING a file
@Serializable
data class UpdateFileRequest(
    val message: String,    // Commit message
    val content: String,    // File content, Base64 encoded
    val sha: String? = null // Existing file SHA (needed for updates, null for creation)
)

// Model we get when READING a file (to get the sha)
@Serializable
data class GetFileResponse(
    val sha: String,
    val content: String // Existing content, Base64 encoded
)

// Model for the content of the playlists.json file itself
@Serializable
data class PlaylistEntry(
    val id: String,
    val title: String
)