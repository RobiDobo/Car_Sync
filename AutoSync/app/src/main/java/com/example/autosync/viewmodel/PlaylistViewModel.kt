package com.example.autosync.viewmodel

import android.app.Application
import android.content.Context
import android.util.Base64
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.example.autosync.BuildConfig
import com.example.autosync.data.auth.AuthManager
import com.example.autosync.data.models.AuthState
import com.example.autosync.data.network.RetrofitClient
import com.example.autosync.data.models.PlaylistEntry
import com.example.autosync.data.models.UpdateFileRequest
import com.example.autosync.data.models.VideoItem
import com.google.android.gms.auth.GoogleAuthUtil
import com.google.android.gms.auth.api.signin.GoogleSignIn
import com.google.android.gms.auth.api.signin.GoogleSignInAccount
import com.google.api.services.youtube.YouTubeScopes
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import retrofit2.HttpException

// Data class to manage UI state (includes selection status)
data class SelectablePlaylist(
    val id: String,
    val title: String,
    val isSelected: Boolean = false
)

class PlaylistViewModel(application: Application) : AndroidViewModel(application) {

    private val authManager = AuthManager(application)
    private val _authState = MutableStateFlow<AuthState>(AuthState.Idle)
    val authState: StateFlow<AuthState> = _authState
    private val _playlists = MutableStateFlow<List<SelectablePlaylist>>(emptyList())
    val playlists: StateFlow<List<SelectablePlaylist>> = _playlists

    private val _commitStatus = MutableSharedFlow<String>()
    val commitStatus = _commitStatus.asSharedFlow()

    // --- SECURELY LOADED CONFIGURATION ---
    val WEB_CLIENT_ID = BuildConfig.WEB_CLIENT_ID
    private val githubPat = BuildConfig.GITHUB_PAT
    private val repoOwner = BuildConfig.REPO_OWNER
    private val repoName = BuildConfig.REPO_NAME
    private val filePath = "PythonScripts/playlists.json"
    // -------------------------------------

    // --- Authentication and Fetching ---

    init {
        checkAuthStatus()
    }

    fun checkAuthStatus() {
        if (authManager.isAuthorized()) {
            val account = GoogleSignIn.getLastSignedInAccount(getApplication())
            account?.let { _authState.value = AuthState.Authenticated(it) }
        } else {
            _authState.value = AuthState.Unauthenticated
        }
    }

    private suspend fun getAccessToken(account: GoogleSignInAccount): String? {
        return withContext(Dispatchers.IO) {
            try {
                val scope = "oauth2:${YouTubeScopes.YOUTUBE_READONLY}"
                GoogleAuthUtil.getToken(getApplication(), account.account!!, scope)
            } catch (e: Exception) {
                Log.e("PlaylistViewModel", "Token Error", e)
                null
            }
        }
    }
    fun getSignInRequest() = authManager.getBeginSignInRequest()
    fun getGso() = authManager.getGoogleSignInOptions()

    // Fetch the playlists after successful Google Sign-In (no longer with context but only with account
    fun fetchPlaylists( account: GoogleSignInAccount) {
        viewModelScope.launch {
            val token = getAccessToken(account)
            if (token != null) {
                try {
                    val response = RetrofitClient.youTubeApi.getMyPlaylists(token = "Bearer $token")
                    _playlists.value = response.items.map {
                        SelectablePlaylist(id = it.id, title = it.snippet.title)
                    }
                } catch (e: Exception) {
                    Log.e("PlaylistViewModel", "Fetch Error", e)
                }
            }
        }
    }
    // video preview entry
    data class VideoPreview(val title: String, val thumbnailUrl: String)

    private val _previewVideos = MutableStateFlow<List<VideoItem>>(emptyList())//secret
    val previewVideos: StateFlow<List<VideoItem>> = _previewVideos// and public that goe to PlaylistScreen

    private val _isPreviewLoading = MutableStateFlow(false)
    val isPreviewLoading: StateFlow<Boolean> = _isPreviewLoading

    fun fetchPlaylistPreview(playlistId: String, account: GoogleSignInAccount) {
        viewModelScope.launch {
            _isPreviewLoading.value = true
            val token = getAccessToken(account)
            if (token != null) {
                try {
                    val response = RetrofitClient.youTubeApi.getPlaylistItems(
                        token = "Bearer $token",
                        playlistId = playlistId
                    )
                    // STOP mapping to VideoPreview. Store the VideoItem list directly.
                    _previewVideos.value = response.items
                } catch (e: Exception) {
                    _commitStatus.emit("Error fetching videos: ${e.message}")
                } finally {
                    _isPreviewLoading.value = false
                }
            }
        }
    }
    //search

    // Add to your ViewModel
    private val _searchQuery = MutableStateFlow("")
    val searchQuery: StateFlow<String> = _searchQuery

    // This flow automatically updates whenever the search query or the playlists change
    val filteredPlaylists: StateFlow<List<SelectablePlaylist>> = combine(_playlists, _searchQuery) { list, query ->
        if (query.isBlank()) list
        else list.filter { it.title.contains(query, ignoreCase = true) }
    }.stateIn(viewModelScope, SharingStarted.Lazily, emptyList())

    fun updateSearchQuery(query: String) {
        _searchQuery.value = query
    }

    // Toggle state for UI
    fun togglePlaylistSelection(playlistId: String) {
        _playlists.value = _playlists.value.map {
            if (it.id == playlistId) {
                it.copy(isSelected = !it.isSelected)
            } else {
                it
            }
        }
    }

    // --- GitHub Commit Logic ---

    // Commit the selected playlists to GitHub
    fun commitChanges() {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                // 1. Prepare JSON content
                val selectedPlaylists = _playlists.value
                    .filter { it.isSelected }
                    .map { PlaylistEntry(id = it.id, title = it.title) }

                val jsonContent = Json.encodeToString(selectedPlaylists)
                // Base64 encoding required by GitHub
                val base64Content = Base64.encodeToString(jsonContent.toByteArray(), Base64.NO_WRAP)

                // 2. Get current SHA (to enable updates)
                val currentSha: String? = try {
                    RetrofitClient.gitHubApi.getFileContent(
                        token = "Bearer $githubPat",
                        owner = repoOwner,
                        repo = repoName,
                        path = filePath
                    ).sha
                } catch (e: HttpException) {
                    // If file not found (404), SHA is null, which means we'll create a new file.
                    // Otherwise, re-throw the exception to be handled by the outer catch block.
                    if (e.code() == 404) {
                        null
                    } else {
                        throw e
                    }
                }

                // 3. Create and send update request
                val updateRequest = UpdateFileRequest(
                    message = "Automated playlist sync from Kotlin app",
                    content = base64Content,
                    sha = currentSha
                )

                RetrofitClient.gitHubApi.updateFile(
                    token = "Bearer $githubPat",
                    owner = repoOwner,
                    repo = repoName,
                    path = filePath,
                    body = updateRequest
                )
                withContext(Dispatchers.Main) {
                    _commitStatus.emit("Playlists synced successfully!")
                }
            } catch (e: Exception) {
                withContext(Dispatchers.Main) {
                    _commitStatus.emit("Error syncing playlists: ${e.message} from viewmodel")
                }
            }
        }
    }
}
