#ifndef PLAYER_FFMPEG_WEBRTC_DECODER_H_
#define PLAYER_FFMPEG_WEBRTC_DECODER_H_

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#endif
//extern "C"
//{
#define __STDC_CONSTANT_MACROS
//#include "video/yangrecordthread.h"
#include "yangplayer/YangPlayerHandle.h"
#include "yangstream/YangStreamType.h"
//#include "yangplayer/YangPlayWidget.h"
#include <yangutil/yangavinfotype.h>
#include <yangutil/sys/YangSysMessageI.h>
#include <yangutil/sys/YangSocket.h>
#include <yangutil/sys/YangLog.h>
#include <yangutil/sys/YangMath.h>

//}
#include <atomic>
#include <future>
#include <functional>
#include <chrono>
#include <mutex>
#include <boost/asio.hpp>
class WebRTCDecoder :  public YangSysMessageI
{
    enum Status {
    STOPPED = 1,
    CONNECTTING = 2,
    CONNECTED = 3
    };
public:
    static WebRTCDecoder* GetInstance();
    // Explicit stop, e.g. the camera panel being closed. Suppresses the
    // stall watchdog so it does not fight the shutdown and reconnect.
    void stopPlay();
    void startPlay(const std::string& strUrl);
    WebRTCDecoder();
    ~WebRTCDecoder();
    void success();
    void failure(int32_t errcode);
    bool isStop() { return m_isStop.load(); }
    int width();
    int height();
    void receiveFrame();
    // Returns a copy of the most recent JPEG frame, taken while holding
    // frame_mutex_. Callers must never get a reference whose lock has
    // already been released.
    std::vector<unsigned char> getFrameData();

private:
    // Set true by an explicit stopPlay() (panel closed). Stops the receive
    // loop and tells the watchdog not to reconnect.
    std::atomic<bool> m_isStop{false};
    std::atomic<Status> m_status{STOPPED};
    // steady_clock nanoseconds; written under m_control_mutex.
    std::atomic<int64_t> m_connect_started_ns{0};
    YangPlayerHandle* m_player;
    YangFrame m_frame;

    // Serialises the control path (startPlay / stopPlay / watchdog
    // reconnect). Kept strictly separate from frame_mutex_: startPlay()
    // used to hold frame_mutex_ across stopPlay() and the std::async
    // assignment, which waits on the receive thread that is itself trying
    // to take frame_mutex_ - a guaranteed deadlock.
    std::mutex m_control_mutex;
    // Guards ONLY the decoded frame buffer (m_frame_data / m_width /
    // m_height / m_last_frame_pts). Always taken through RAII.
    std::mutex frame_mutex_;

    static WebRTCDecoder *g_pSingleton;
    boost::asio::ip::tcp::socket* m_psocket=nullptr;
    std::vector<unsigned char> m_frame_data;

    // steady_clock nanoseconds of the last decoded frame (0 = none yet).
    // Written by the receive thread, read by the watchdog.
    std::atomic<int64_t> m_last_frame_ns{0};
    // receive-thread-only bookkeeping
    std::chrono::steady_clock::time_point m_last_novideobuffer_log{};
    bool m_stall_logged = false;
    int64_t m_last_frame_pts = -1;

    // A stream that stays CONNECTED but delivers no new frame for this
    // long is treated as dead and rebuilt.
    static constexpr int kStreamDeadSeconds = 8;
    // A connection stuck in CONNECTTING for this long never resolved.
    static constexpr int kConnectStuckSeconds = 15;

    // Watchdog thread: rebuilds a stalled connection with no user action.
    std::future<void> m_watchdogFuture;
    std::atomic<bool> m_watchdog_stop{false};
    std::atomic<bool> m_watchdog_running{false};
    void watchdogLoop();
    void startWatchdog();
    void stopWatchdog();
    bool isStreamDead() const;

    // Both assume m_control_mutex is held.
    void startPlayLocked(const std::string& strUrl);
    void stopPlayLocked();
protected:
    YangContext* m_context;
    std::string m_url;
    std::future<void> m_playFutrue;
    int m_width=1920;
    int m_height=1080;
};

#endif // ! PLAYER_FFMPEG_WEBRTC_DECODER_H_
