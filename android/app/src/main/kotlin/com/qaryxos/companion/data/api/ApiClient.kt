package com.qaryxos.companion.data.api

import android.content.Context
import android.content.SharedPreferences
import okhttp3.OkHttpClient
import okhttp3.logging.HttpLoggingInterceptor
import retrofit2.Retrofit
import retrofit2.converter.gson.GsonConverterFactory
import java.util.concurrent.TimeUnit

object ApiClient {

    private const val PREFS_NAME = "qaryxos_prefs"
    private const val KEY_DEVICE_IP = "device_ip"
    const val DEFAULT_PORT = 8080

    private var _api: QaryxApi? = null
    private var _currentBase: String? = null

    fun getApi(baseUrl: String): QaryxApi {
        if (_api == null || _currentBase != baseUrl) {
            _api = buildRetrofit(baseUrl).create(QaryxApi::class.java)
            _currentBase = baseUrl
        }
        return _api!!
    }

    fun getApiForContext(context: Context): QaryxApi {
        val ip = getSavedIp(context)
        return getApi("http://$ip:$DEFAULT_PORT/")
    }

    private fun buildRetrofit(baseUrl: String): Retrofit {
        val logging = HttpLoggingInterceptor().apply {
            level = HttpLoggingInterceptor.Level.BASIC
        }
        val client = OkHttpClient.Builder()
            .connectTimeout(3, TimeUnit.SECONDS)
            .readTimeout(10, TimeUnit.SECONDS)
            .writeTimeout(5, TimeUnit.SECONDS)
            .addInterceptor(logging)
            .build()

        return Retrofit.Builder()
            .baseUrl(baseUrl)
            .client(client)
            .addConverterFactory(GsonConverterFactory.create())
            .build()
    }

    fun saveDeviceIp(context: Context, ip: String) {
        prefs(context).edit().putString(KEY_DEVICE_IP, ip.trim()).apply()
    }

    fun getSavedIp(context: Context): String {
        return prefs(context).getString(KEY_DEVICE_IP, "") ?: ""
    }

    fun hasDevice(context: Context): Boolean = getSavedIp(context).isNotEmpty()

    private fun prefs(context: Context): SharedPreferences =
        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
}
