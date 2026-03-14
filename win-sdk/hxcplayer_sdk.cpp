/**
 * @file hxcplayer_sdk.cpp
 * @brief HXCPlayer SDK 实现
 */

#include "hxcplayer_sdk.h"
#include <string>

#ifdef _WIN32
    // WIN32_LEAN_AND_MEAN 和 NOMINMAX 已通过 CMake 定义（从 hxcplayer_core 继承）
    #include <windows.h>
#endif

// SDK 版本信息
extern "C" {

const char* hxcplayer_get_version(void) {
    return HXCPLAYER_SDK_VERSION;
}

const char* hxcplayer_get_build_info(void) {
    static std::string build_info;
    if (build_info.empty()) {
        build_info = "HXCPlayer SDK " HXCPLAYER_SDK_VERSION "\n";
        build_info += "Build: " __DATE__ " " __TIME__ "\n";
        #ifdef _WIN32
            build_info += "Platform: Windows\n";
            #ifdef _WIN64
                build_info += "Architecture: x64\n";
            #else
                build_info += "Architecture: x86\n";
            #endif
            #ifdef _DEBUG
                build_info += "Configuration: Debug\n";
            #else
                build_info += "Configuration: Release\n";
            #endif
        #endif
    }
    return build_info.c_str();
}

int hxcplayer_init(void) {
    // SDK 初始化（如果需要全局初始化，在这里添加）
    return 0;
}

void hxcplayer_cleanup(void) {
    // SDK 清理（如果需要全局清理，在这里添加）
}

} // extern "C"

// Windows DLL 入口点
#ifdef _WIN32
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            // DLL 被加载到进程
            hxcplayer_init();
            break;
            
        case DLL_PROCESS_DETACH:
            // DLL 从进程卸载
            hxcplayer_cleanup();
            break;
            
        case DLL_THREAD_ATTACH:
            // 新线程创建
            break;
            
        case DLL_THREAD_DETACH:
            // 线程退出
            break;
    }
    return TRUE;
}
#endif
