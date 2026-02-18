package com.example.autosync.data.models

import com.google.android.gms.auth.api.signin.GoogleSignInAccount
//This allows MainScreen to know exactly what to show (Login or Playlists).
sealed class AuthState {
    object Idle : AuthState()
    object Loading : AuthState()
    object Unauthenticated : AuthState()
    data class Authenticated(val account: GoogleSignInAccount) : AuthState()
    data class Error(val message: String) : AuthState()
}