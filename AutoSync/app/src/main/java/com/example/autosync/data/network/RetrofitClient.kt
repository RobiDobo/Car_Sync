package com.example.autosync.data.network

import com.jakewharton.retrofit2.converter.kotlinx.serialization.asConverterFactory
import kotlinx.serialization.json.Json
import okhttp3.MediaType.Companion.toMediaType
import retrofit2.Retrofit

object RetrofitClient {
    private val json = Json { ignoreUnknownKeys = true }

    private fun createClient(baseUrl: String): Retrofit {
        return Retrofit.Builder()
            .baseUrl(baseUrl)
            .addConverterFactory(json.asConverterFactory("application/json".toMediaType()))
            .build()
    }

    val youTubeApi: YouTubeApiService = createClient("https://www.googleapis.com/")
        .create(YouTubeApiService::class.java)

    val gitHubApi: GitHubApiService = createClient("https://api.github.com/")
        .create(GitHubApiService::class.java)
}