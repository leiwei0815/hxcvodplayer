/**
 * @file hxcplayer_sdk.h
 * @brief HXCPlayer Windows SDK 统一头文件
 * @version 1.0.0
 * 
 * 这是一个跨平台的视频播放器 SDK，支持多种视频格式和网络流
 */

#ifndef HXCPLAYER_SDK_H
#define HXCPLAYER_SDK_H

// DLL 导出/导入宏定义
#ifdef _WIN32
    #ifdef HXCPLAYER_DLL_EXPORTS
        #define HXCPLAYER_API __declspec(dllexport)
    #elif defined(HXCPLAYER_DLL_IMPORTS)
        #define HXCPLAYER_API __declspec(dllimport)
    #else
        #define HXCPLAYER_API  // 静态库
    #endif
#else
    #define HXCPLAYER_API  // Linux/macOS
#endif

// 包含 C 桥接接口
#include "hxc_player_core_c_bridge.h"

// SDK 版本信息
#define HXCPLAYER_SDK_VERSION_MAJOR 1
#define HXCPLAYER_SDK_VERSION_MINOR 0
#define HXCPLAYER_SDK_VERSION_PATCH 0
#define HXCPLAYER_SDK_VERSION "1.0.0"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 获取 SDK 版本字符串
 * @return 版本字符串，格式: "major.minor.patch"
 */
HXCPLAYER_API const char* hxcplayer_get_version(void);

/**
 * @brief 获取 SDK 构建信息
 * @return 构建信息字符串
 */
HXCPLAYER_API const char* hxcplayer_get_build_info(void);

/**
 * @brief 初始化 SDK（可选，自动初始化）
 * @return 0=成功，非0=失败
 */
HXCPLAYER_API int hxcplayer_init(void);

/**
 * @brief 清理 SDK 资源（可选，进程退出时自动清理）
 */
HXCPLAYER_API void hxcplayer_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // HXCPLAYER_SDK_H
