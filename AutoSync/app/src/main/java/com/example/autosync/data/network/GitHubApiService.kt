package com.example.autosync.data.network

import com.example.autosync.data.models.GetFileResponse
import com.example.autosync.data.models.UpdateFileRequest
import retrofit2.http.Body
import retrofit2.http.GET
import retrofit2.http.Header
import retrofit2.http.PUT
import retrofit2.http.Path

interface GitHubApiService {
    @GET("repos/{owner}/{repo}/contents/{path}")
    suspend fun getFileContent(
        @Header("Authorization") token: String,
        @Header("X-GitHub-Api-Version") apiVersion: String = "2022-11-28",
        @Path("owner") owner: String,
        @Path("repo") repo: String,
        @Path("path") path: String
    ): GetFileResponse

    @PUT("repos/{owner}/{repo}/contents/{path}")
    suspend fun updateFile(
        @Header("Authorization") token: String,
        @Header("X-GitHub-Api-Version") apiVersion: String = "2022-11-28",
        @Path("owner") owner: String,
        @Path("repo") repo: String,
        @Path("path") path: String,
        @Body body: UpdateFileRequest
    )
}