package com.hxcplayer.download

interface HXCDownloadListener {
    fun onDownloadListChanged(allInfos: List<HXCDownloadInfo>)
    fun onProgressUpdate(info: HXCDownloadInfo)
    fun onStatusChanged(info: HXCDownloadInfo)
}
