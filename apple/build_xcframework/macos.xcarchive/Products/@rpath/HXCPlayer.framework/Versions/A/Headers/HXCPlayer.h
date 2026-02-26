/**
 * @file HXCPlayer.h
 * @brief HXCPlayer Framework Umbrella Header
 * 
 * 这是 HXCPlayer Framework 的主头文件（umbrella header）
 * 用于导出所有公共 API
 */

#import <Foundation/Foundation.h>

//! Project version number for HXCPlayer.
FOUNDATION_EXPORT double HXCPlayerVersionNumber;

//! Project version string for HXCPlayer.
FOUNDATION_EXPORT const unsigned char HXCPlayerVersionString[];

// 导入所有公共头文件
#import <HXCPlayer/HXCPlayerControl.h>
#import <HXCPlayer/HXCPlayerView.h>
