package com.example.autosync.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.example.autosync.viewmodel.PlaylistViewModel
import com.example.autosync.viewmodel.SelectablePlaylist
import com.example.autosync.data.models.AuthState
import kotlinx.coroutines.flow.collect
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PlaylistScreen(viewModel: PlaylistViewModel) {
    //observers
    val searchQuery by viewModel.searchQuery.collectAsState()
    val playlists by viewModel.filteredPlaylists.collectAsState()
    val previewVideos by viewModel.previewVideos.collectAsState() // Observe video list
    val isPreviewLoading by viewModel.isPreviewLoading.collectAsState() // Observe loading
    val authState by viewModel.authState.collectAsState()
    var showSuccessNotification by remember { mutableStateOf(false) }

    val sheetState = rememberModalBottomSheetState()
    var showSheet by remember { mutableStateOf(false) }

    LaunchedEffect(Unit) {
        viewModel.commitStatus.collect { message ->
            // Trigger only if the message indicates success
            if (message.contains("successfully", ignoreCase = true)) {
                showSuccessNotification = true
                kotlinx.coroutines.delay(2500) // Show for 2.5 seconds
                showSuccessNotification = false
            }
        }
    }

    Box(modifier = Modifier.fillMaxSize()) {
        Scaffold(
            topBar = {
                TopAppBar(
                    title = {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            // Title stays on the left
                            Text(
                                text = "Select Playlists",
                                style = MaterialTheme.typography.titleMedium,
                                modifier = Modifier.wrapContentWidth()
                            )

                            // Search Bar takes up the remaining space on the right
                            TextField(
                                value = searchQuery,
                                onValueChange = { viewModel.updateSearchQuery(it) },
                                placeholder = { Text("Search...", style = MaterialTheme.typography.bodySmall) },
                                singleLine = true,
                                modifier = Modifier
                                    .weight(1f) // Takes available space next to title
                                    .padding(end = 8.dp),
                                colors = TextFieldDefaults.colors(
                                    focusedContainerColor = Color.Transparent,
                                    unfocusedContainerColor = Color.Transparent,
                                    focusedIndicatorColor = MaterialTheme.colorScheme.primary,
                                    unfocusedIndicatorColor = Color.Transparent
                                ),
                                leadingIcon = {
                                    Icon(Icons.Default.Search, contentDescription = null, modifier = Modifier.size(18.dp))
                                }
                            )
                        }
                    }
                )
            },
            bottomBar = {
                Surface(
                    tonalElevation = 8.dp, // Makes it look like a separate panel
                    shadowElevation = 12.dp,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .navigationBarsPadding() // higher than system buttons
                            .padding(bottom = 32.dp, start = 16.dp, end = 16.dp, top = 16.dp)
                    ) {
                        Button(
                            onClick = { viewModel.commitChanges() },
                            enabled = playlists.any { it.isSelected },
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(56.dp) // Make the button thicker for better touch target
                        ) {
                            Text("Commit Selection to GitHub")
                        }
                    }
                }
            }
        ) { padding ->
            LazyColumn(modifier = Modifier.padding(padding)) {
                items(playlists) { item ->
                    PlaylistItemRow(
                        item = item,
                        onToggle = { viewModel.togglePlaylistSelection(item.id) },
                        onTitleClick = {
                            // Trigger fetch and show sheet
                            if (authState is AuthState.Authenticated) {
                                viewModel.fetchPlaylistPreview(item.id, (authState as AuthState.Authenticated).account)
                                showSheet = true
                            }
                        }
                    )
                }
            }

            // The Preview Panel
            if (showSheet) {
                ModalBottomSheet(
                    onDismissRequest = { showSheet = false },
                    sheetState = sheetState
                ) {
                    Box(Modifier.fillMaxWidth().heightIn(min = 200.dp)) {
                        if (isPreviewLoading) {
                            CircularProgressIndicator(Modifier.align(Alignment.Center))
                        } else {
                            LazyColumn(Modifier.padding(16.dp)) {
                                item { Text("Preview Videos", style = MaterialTheme.typography.headlineSmall) }
                                items(previewVideos) { video ->
                                    Text(video.snippet.title, modifier = Modifier.padding(vertical = 8.dp))
                                }
                            }
                        }
                    }
                }
            }
        }
        if (showSuccessNotification) {
            Surface(
                modifier = Modifier
                    .align(Alignment.Center)
                    .padding(32.dp),
                shape = RoundedCornerShape(12.dp),
                color = Color(0xFF2E7D32), // Deep Green
                shadowElevation = 10.dp
            ) {
                Row(
                    modifier = Modifier.padding(24.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(
                        imageVector = Icons.Default.CheckCircle,
                        contentDescription = null,
                        tint = Color.White,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(Modifier.width(12.dp))
                    Text(
                        text = "Synced to GitHub!",
                        color = Color.White,
                        style = MaterialTheme.typography.titleMedium
                    )
                }
            }
    }
}
}

@Composable
fun PlaylistItemRow(item: SelectablePlaylist, onToggle: () -> Unit, onTitleClick: ()-> Unit ) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable { onToggle() }
            .padding(16.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = item.title,
            modifier = Modifier.weight(1f).clickable { onTitleClick() }

        )
        Checkbox(
            checked = item.isSelected,
            onCheckedChange = { onToggle() }
        )
    }
}
