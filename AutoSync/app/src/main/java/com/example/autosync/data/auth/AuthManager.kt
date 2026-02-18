package com.example.autosync.data.auth

import android.content.Context
import com.example.autosync.BuildConfig
import com.google.android.gms.auth.api.identity.BeginSignInRequest
import com.google.android.gms.auth.api.signin.GoogleSignIn
import com.google.android.gms.auth.api.signin.GoogleSignInOptions
import com.google.android.gms.common.api.Scope
import com.google.api.services.youtube.YouTubeScopes
//refactored from MainScreen

class AuthManager(private val context: Context) {
    private val youtubeScope = Scope(YouTubeScopes.YOUTUBE_READONLY)

    // Moved from MainScreen
    fun getBeginSignInRequest(): BeginSignInRequest = BeginSignInRequest.builder()
        .setGoogleIdTokenRequestOptions(
            BeginSignInRequest.GoogleIdTokenRequestOptions.builder()
                .setSupported(true)
                .setServerClientId(BuildConfig.WEB_CLIENT_ID)
                .setFilterByAuthorizedAccounts(false)
                .build()
        )
        .build()

    // Moved from MainScreen
    fun getGoogleSignInOptions(): GoogleSignInOptions = GoogleSignInOptions.Builder(GoogleSignInOptions.DEFAULT_SIGN_IN)
        .requestScopes(youtubeScope)
        .requestEmail()
        .build()

    fun isAuthorized(): Boolean {
        val account = GoogleSignIn.getLastSignedInAccount(context)
        return account != null && GoogleSignIn.hasPermissions(account, youtubeScope)
    }
}