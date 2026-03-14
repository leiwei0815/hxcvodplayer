/**
 * @file mfc_player_example.cpp
 * @brief HXCPlayer SDK MFC 自动渲染示例
 * 
 * 演示如何在 MFC 应用中使用 HXCPlayer SDK 的自动渲染功能
 */

#include "stdafx.h"
#include "mfc_player_example.h"
#include "mfc_player_exampleDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// CMFCPlayerExampleDlg 对话框

CMFCPlayerExampleDlg::CMFCPlayerExampleDlg(CWnd* pParent /*=NULL*/)
    : CDialogEx(IDD_MFCPLAYEREXAMPLE_DIALOG, pParent)
    , player_(nullptr)
{
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CMFCPlayerExampleDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_VIDEO_WINDOW, m_videoWindow);
    DDX_Control(pDX, IDC_BTN_OPEN, m_btnOpen);
    DDX_Control(pDX, IDC_BTN_PLAY, m_btnPlay);
    DDX_Control(pDX, IDC_BTN_PAUSE, m_btnPause);
    DDX_Control(pDX, IDC_BTN_STOP, m_btnStop);
    DDX_Control(pDX, IDC_SLIDER_PROGRESS, m_sliderProgress);
    DDX_Control(pDX, IDC_STATIC_TIME, m_staticTime);
}

BEGIN_MESSAGE_MAP(CMFCPlayerExampleDlg, CDialogEx)
    ON_WM_PAINT()
    ON_WM_QUERYDRAGICON()
    ON_BN_CLICKED(IDC_BTN_OPEN, &CMFCPlayerExampleDlg::OnBnClickedBtnOpen)
    ON_BN_CLICKED(IDC_BTN_PLAY, &CMFCPlayerExampleDlg::OnBnClickedBtnPlay)
    ON_BN_CLICKED(IDC_BTN_PAUSE, &CMFCPlayerExampleDlg::OnBnClickedBtnPause)
    ON_BN_CLICKED(IDC_BTN_STOP, &CMFCPlayerExampleDlg::OnBnClickedBtnStop)
    ON_WM_DESTROY()
    ON_WM_TIMER()
END_MESSAGE_MAP()

// CMFCPlayerExampleDlg 消息处理程序

BOOL CMFCPlayerExampleDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    SetIcon(m_hIcon, TRUE);
    SetIcon(m_hIcon, FALSE);

    // 初始化播放器
    InitPlayer();

    // 设置进度条范围
    m_sliderProgress.SetRange(0, 1000);

    // 启动定时器（用于刷新视频和更新进度）
    SetTimer(TIMER_REFRESH_VIDEO, 16, NULL);  // 60 FPS
    SetTimer(TIMER_UPDATE_PROGRESS, 100, NULL);  // 10 Hz

    return TRUE;
}

void CMFCPlayerExampleDlg::OnPaint()
{
    if (IsIconic())
    {
        CPaintDC dc(this);
        // ... 图标绘制代码
    }
    else
    {
        CDialogEx::OnPaint();
    }
}

HCURSOR CMFCPlayerExampleDlg::OnQueryDragIcon()
{
    return static_cast<HCURSOR>(m_hIcon);
}

void CMFCPlayerExampleDlg::InitPlayer()
{
    // 创建播放器
    player_ = player_core_create();
    if (!player_) {
        AfxMessageBox(_T("创建播放器失败！"));
        return;
    }

    // 设置渲染窗口（使用静态控件的 HWND）
    HWND hwndVideo = m_videoWindow.GetSafeHwnd();
    player_core_set_render_window(player_, (void*)hwndVideo);

    // 设置为自动渲染模式（默认）
    player_core_set_render_mode(player_, RENDER_MODE_AUTO);

    // 设置回调
    player_core_set_state_changed_callback(player_, OnStateChanged, this);
    player_core_set_error_callback(player_, OnError, this);
    player_core_set_position_changed_callback(player_, OnPositionChanged, this);
    player_core_set_playback_completed_callback(player_, OnPlaybackCompleted, this);
}

void CMFCPlayerExampleDlg::CleanupPlayer()
{
    if (player_) {
        player_core_stop(player_);
        player_core_destroy(player_);
        player_ = nullptr;
    }
}

void CMFCPlayerExampleDlg::OnDestroy()
{
    // 停止定时器
    KillTimer(TIMER_REFRESH_VIDEO);
    KillTimer(TIMER_UPDATE_PROGRESS);

    // 清理播放器
    CleanupPlayer();

    CDialogEx::OnDestroy();
}

void CMFCPlayerExampleDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == TIMER_REFRESH_VIDEO) {
        // 刷新视频（自动渲染模式）
        if (player_) {
            player_core_refresh_video(player_);
        }
    }
    else if (nIDEvent == TIMER_UPDATE_PROGRESS) {
        // 更新进度条和时间显示
        if (player_) {
            double position = player_core_get_position(player_);
            double duration = player_core_get_duration(player_);

            if (duration > 0) {
                int progress = (int)((position / duration) * 1000);
                m_sliderProgress.SetPos(progress);

                // 更新时间显示
                CString strTime;
                strTime.Format(_T("%.1f / %.1f 秒"), position, duration);
                m_staticTime.SetWindowText(strTime);
            }
        }
    }

    CDialogEx::OnTimer(nIDEvent);
}

void CMFCPlayerExampleDlg::OnBnClickedBtnOpen()
{
    // 打开文件对话框
    CFileDialog dlg(TRUE, NULL, NULL, 
        OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST,
        _T("视频文件 (*.mp4;*.avi;*.mkv)|*.mp4;*.avi;*.mkv|所有文件 (*.*)|*.*||"));

    if (dlg.DoModal() == IDOK) {
        CString filePath = dlg.GetPathName();

        // 转换为 UTF-8
        CStringA filePathA(filePath);
        const char* path = filePathA.GetString();

        // 打开视频
        if (player_core_open(player_, path) == 0) {
            m_btnPlay.EnableWindow(TRUE);
            m_btnPause.EnableWindow(FALSE);
            m_btnStop.EnableWindow(TRUE);
        }
        else {
            AfxMessageBox(_T("打开视频失败！"));
        }
    }
}

void CMFCPlayerExampleDlg::OnBnClickedBtnPlay()
{
    if (player_) {
        player_core_play(player_);
        m_btnPlay.EnableWindow(FALSE);
        m_btnPause.EnableWindow(TRUE);
    }
}

void CMFCPlayerExampleDlg::OnBnClickedBtnPause()
{
    if (player_) {
        player_core_pause(player_);
        m_btnPlay.EnableWindow(TRUE);
        m_btnPause.EnableWindow(FALSE);
    }
}

void CMFCPlayerExampleDlg::OnBnClickedBtnStop()
{
    if (player_) {
        player_core_stop(player_);
        m_btnPlay.EnableWindow(TRUE);
        m_btnPause.EnableWindow(FALSE);
        m_sliderProgress.SetPos(0);
        m_staticTime.SetWindowText(_T("0.0 / 0.0 秒"));
    }
}

// 静态回调函数
void CMFCPlayerExampleDlg::OnStateChanged(PlayerStateC state, void* user_data)
{
    CMFCPlayerExampleDlg* pThis = (CMFCPlayerExampleDlg*)user_data;
    // 在主线程更新 UI
    // 注意：这里应该使用 PostMessage 转发到主线程
}

void CMFCPlayerExampleDlg::OnError(int error_code, const char* error_msg, void* user_data)
{
    CMFCPlayerExampleDlg* pThis = (CMFCPlayerExampleDlg*)user_data;
    CString msg;
    msg.Format(_T("播放器错误 [%d]: %s"), error_code, CString(error_msg));
    AfxMessageBox(msg);
}

void CMFCPlayerExampleDlg::OnPositionChanged(double position, void* user_data)
{
    // 进度更新在定时器中处理
}

void CMFCPlayerExampleDlg::OnPlaybackCompleted(void* user_data)
{
    CMFCPlayerExampleDlg* pThis = (CMFCPlayerExampleDlg*)user_data;
    // 播放完成，重置按钮状态
}
