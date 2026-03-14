/**
 * @file simple_player.c
 * @brief HXCPlayer SDK 简单示例（C 语言）
 * 
 * 演示如何使用 HXCPlayer SDK 播放视频
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 定义 DLL 导入宏
#define HXCPLAYER_DLL_IMPORTS
#include "hxcplayer_sdk.h"

// 全局变量
static int is_playing = 0;
static int playback_completed = 0;

// 状态变化回调
void on_state_changed(PlayerStateC state, void* user_data) {
    const char* state_names[] = {
        "IDLE", "OPENING", "PLAYING", "PAUSED", "STOPPED"
    };
    
    if (state >= 0 && state <= 4) {
        printf("[回调] 状态变化: %s\n", state_names[state]);
    } else if (state == PLAYER_STATE_ERROR) {
        printf("[回调] 状态变化: ERROR\n");
    }
    
    is_playing = (state == PLAYER_STATE_PLAYING);
}

// 错误回调
void on_error(int error_code, const char* error_msg, void* user_data) {
    printf("[回调] 错误: [%d] %s\n", error_code, error_msg);
}

// 播放进度回调
void on_position_changed(double position, void* user_data) {
    // 每秒打印一次进度（这里简化处理）
    static int last_sec = -1;
    int current_sec = (int)position;
    
    if (current_sec != last_sec) {
        PlayerCoreHandle* player = (PlayerCoreHandle*)user_data;
        double duration = player_core_get_duration(player);
        
        printf("\r[进度] %.1f / %.1f 秒 (%.1f%%)", 
               position, duration, 
               (position / duration) * 100.0);
        fflush(stdout);
        
        last_sec = current_sec;
    }
}

// 播放完成回调
void on_playback_completed(void* user_data) {
    printf("\n[回调] 播放完成！\n");
    playback_completed = 1;
    is_playing = 0;
}

int main(int argc, char* argv[]) {
    // 检查参数
    if (argc < 2) {
        printf("用法: %s <视频文件路径或URL>\n", argv[0]);
        printf("示例: %s test.mp4\n", argv[0]);
        printf("示例: %s http://example.com/video.mp4\n", argv[0]);
        return 1;
    }
    
    const char* video_url = argv[1];
    
    // 打印 SDK 信息
    printf("=== HXCPlayer SDK 示例 ===\n");
    printf("SDK 版本: %s\n", hxcplayer_get_version());
    printf("构建信息:\n%s\n", hxcplayer_get_build_info());
    
    // 初始化 SDK（可选）
    if (hxcplayer_init() != 0) {
        printf("SDK 初始化失败！\n");
        return 1;
    }
    
    // 创建播放器
    printf("\n正在创建播放器...\n");
    PlayerCoreHandle* player = player_core_create();
    if (!player) {
        printf("创建播放器失败！\n");
        return 1;
    }
    
    // 设置回调
    player_core_set_state_changed_callback(player, on_state_changed, NULL);
    player_core_set_error_callback(player, on_error, NULL);
    player_core_set_position_changed_callback(player, on_position_changed, player);
    player_core_set_playback_completed_callback(player, on_playback_completed, NULL);
    
    // 设置音量
    player_core_set_volume(player, 80);  // 80%
    
    // 打开视频
    printf("正在打开视频: %s\n", video_url);
    int ret = player_core_open(player, video_url);
    if (ret != 0) {
        printf("打开视频失败！错误码: %d\n", ret);
        player_core_destroy(player);
        return 1;
    }
    
    // 等待打开完成（简化处理，实际应该等待状态回调）
    #ifdef _WIN32
        Sleep(1000);  // 等待 1 秒
    #else
        sleep(1);
    #endif
    
    // 获取视频信息
    double duration = player_core_get_duration(player);
    int width = player_core_get_video_width(player);
    int height = player_core_get_video_height(player);
    
    printf("\n视频信息:\n");
    printf("  时长: %.2f 秒\n", duration);
    printf("  分辨率: %dx%d\n", width, height);
    
    // 开始播放
    printf("\n开始播放...\n");
    player_core_play(player);
    
    // 简单的控制循环
    printf("\n控制:\n");
    printf("  空格 - 播放/暂停\n");
    printf("  + - 音量增加\n");
    printf("  - - 音量减小\n");
    printf("  q - 退出\n");
    printf("  s - Seek 到 10 秒\n");
    printf("\n按回车确认命令...\n");
    
    // 主循环
    char cmd[256];
    while (!playback_completed) {
        if (fgets(cmd, sizeof(cmd), stdin)) {
            // 移除换行符
            cmd[strcspn(cmd, "\r\n")] = 0;
            
            if (strlen(cmd) == 0) {
                continue;
            }
            
            switch (cmd[0]) {
                case ' ':  // 空格：播放/暂停
                    if (is_playing) {
                        printf("\n暂停播放\n");
                        player_core_pause(player);
                    } else {
                        printf("\n继续播放\n");
                        player_core_play(player);
                    }
                    break;
                    
                case '+':  // 音量增加
                    {
                        double pos = player_core_get_position(player);
                        printf("\n音量增加 (当前位置: %.1f 秒)\n", pos);
                        // 注意：需要实现 get_volume 才能精确控制
                        player_core_set_volume(player, 100);
                    }
                    break;
                    
                case '-':  // 音量减小
                    {
                        printf("\n音量减小\n");
                        player_core_set_volume(player, 50);
                    }
                    break;
                    
                case 's':  // Seek
                case 'S':
                    printf("\nSeek 到 10 秒\n");
                    player_core_seek(player, 10.0);
                    break;
                    
                case 'q':  // 退出
                case 'Q':
                    printf("\n正在退出...\n");
                    goto cleanup;
                    
                default:
                    printf("\n未知命令: %c\n", cmd[0]);
                    break;
            }
        }
    }
    
cleanup:
    // 停止播放
    printf("停止播放...\n");
    player_core_stop(player);
    
    // 销毁播放器
    printf("销毁播放器...\n");
    player_core_destroy(player);
    
    // 清理 SDK
    hxcplayer_cleanup();
    
    printf("完成！\n");
    return 0;
}
