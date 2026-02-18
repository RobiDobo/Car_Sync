package com.example.autosync.ui

import android.app.Activity
import android.util.Log
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.viewmodel.compose.viewModel
import com.example.autosync.data.models.AuthState
import com.example.autosync.viewmodel.PlaylistViewModel
import com.google.android.gms.auth.api.identity.Identity
import com.google.android.gms.auth.api.signin.GoogleSignIn

@Composable
fun MainScreen(viewModel: PlaylistViewModel = viewModel()) {
    val context = LocalContext.current
    // observe the AuthState from the ViewModel
    val authState by viewModel.authState.collectAsState()

    // Launcher for OAuth Authorization (YouTube Permissions)
    val authorizationLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.StartActivityForResult()
    ) { result ->
        if (result.resultCode == Activity.RESULT_OK) {
            // After permissions are granted, tell the ViewModel to refresh status
            viewModel.checkAuthStatus()
        }
    }

    // Launcher for One Tap Authentication
    val oneTapLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.StartIntentSenderForResult()
    ) { result ->
        if (result.resultCode == Activity.RESULT_OK) {
            try {
                // Once authenticated, we immediately request YouTube Authorization
                val googleSignInClient = GoogleSignIn.getClient(context, viewModel.getGso())
                authorizationLauncher.launch(googleSignInClient.signInIntent)
            } catch (e: Exception) {
                Log.e("MainScreen", "Error during One Tap authentication", e)
            }
        }
    }

    // UI State Machine
    when (val state = authState) {
        is AuthState.Authenticated -> {
            // The ViewModel will use getApplication() internally.
            LaunchedEffect(state.account) {
                viewModel.fetchPlaylists(state.account)
            }
            PlaylistScreen(viewModel = viewModel)
        }

        is AuthState.Loading -> {
            Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                CircularProgressIndicator()
            }
        }

        else -> {
            LoginScreen(onLoginClick = {
                Log.d("AUTH_DEBUG", "Login button clicked")
                val oneTapClient = Identity.getSignInClient(context)

                oneTapClient.beginSignIn(viewModel.getSignInRequest())
                    .addOnSuccessListener { result ->
                        Log.d("AUTH_DEBUG", "One Tap success: Launching UI")
                        try {
                            oneTapLauncher.launch(
                                IntentSenderRequest.Builder(result.pendingIntent.intentSender).build()
                            )
                        } catch (e: Exception) {
                            Log.e("AUTH_DEBUG", "Launcher failed: ${e.localizedMessage}")
                        }
                    }
                    .addOnFailureListener { e ->
                        // This is where most Google Sign-in issues are caught
                        Log.e("AUTH_DEBUG", "One Tap failed: ${e.message}")

                        // Fallback
                        Log.d("AUTH_DEBUG", "Attempting fallback to GoogleSignInClient")
                        val googleSignInClient = GoogleSignIn.getClient(context, viewModel.getGso())
                        authorizationLauncher.launch(googleSignInClient.signInIntent)
                    }
            })
        }
    }

}

@Composable
fun LoginScreen(onLoginClick: () -> Unit) {
    Box(
        modifier = Modifier.fillMaxSize(),
        contentAlignment = Alignment.Center
    ) {
        Button(onClick = onLoginClick) {
            Text("Sign in with Google")
        }
    }
}