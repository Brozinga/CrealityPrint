#include "WebRTCDecoder.h"
#include <cstring>
#include <thread>
#include <yangstream/YangSynBuffer.h>
#include <thread>
#include <chrono>
#include <fstream>
#include <iostream>
#include <jpeglib.h>
#include <boost/log/trivial.hpp>

namespace {
inline int64_t now_ns()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

WebRTCDecoder* WebRTCDecoder::g_pSingleton = new (std::nothrow) WebRTCDecoder();
WebRTCDecoder::WebRTCDecoder()
{
    yang_setCLogLevel(1);
    m_context = new YangContext();
    m_context->init();

    m_context->synMgr.session->playBuffer = (YangSynBuffer*)yang_calloc(sizeof(YangSynBuffer), 1);//new YangSynBuffer();
    yang_create_synBuffer(m_context->synMgr.session->playBuffer);

    m_context->avinfo.sys.mediaServer = Yang_Server_P2p;//Yang_Server_Srs/Yang_Server_Zlm
    m_context->avinfo.rtc.rtcSocketProtocol = Yang_Socket_Protocol_Udp;//

    m_context->avinfo.rtc.rtcLocalPort = 10000 + yang_random() % 15000;
    memset(m_context->avinfo.rtc.localIp, 0, sizeof(m_context->avinfo.rtc.localIp));
    yang_getLocalInfo(m_context->avinfo.sys.familyType, m_context->avinfo.rtc.localIp);
    m_context->avinfo.rtc.enableDatachannel = yangfalse;
    m_context->avinfo.rtc.iceCandidateType = YangIceHost;
    m_context->avinfo.rtc.turnSocketProtocol = Yang_Socket_Protocol_Udp;

    m_context->avinfo.rtc.enableAudioBuffer = yangtrue; //use audio buffer
    m_context->avinfo.audio.enableAudioFec = yangfalse; //srs not use audio fec
    m_player = YangPlayerHandle::createPlayerHandle(m_context, this);
}
WebRTCDecoder::~WebRTCDecoder()
{
    //stopplay();
}
WebRTCDecoder* WebRTCDecoder::GetInstance()
{
    return g_pSingleton;
}
void WebRTCDecoder::stopPlay()
{
    // Explicit stop (panel closed). Suppress the watchdog first, outside
    // m_control_mutex, so it can't be mid-reconnect holding the lock while
    // we wait for it.
    m_isStop = true;
    stopWatchdog();

    std::lock_guard<std::mutex> guard(m_control_mutex);
    stopPlayLocked();
}

// Assumes m_control_mutex is held. Tears down the current session and
// leaves m_status == STOPPED.
void WebRTCDecoder::stopPlayLocked()
{
    m_isStop = true;
    // The receive thread only ever takes frame_mutex_ (briefly, per
    // frame) and never m_control_mutex, so waiting for it here - while
    // holding m_control_mutex and NOT frame_mutex_ - cannot deadlock.
    // Bounded so a hang inside the yang lib is logged rather than freezing
    // the caller forever.
    if (m_playFutrue.valid())
    {
        if (m_playFutrue.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
            BOOST_LOG_TRIVIAL(error) << "[WebRTC] receive thread did not exit within 5s during stop";
        else
            m_playFutrue.get();
    }
    if (m_player) m_player->stopPlay();
    m_status = STOPPED;
    m_stall_logged = false;
    m_last_frame_ns = 0;
}

void WebRTCDecoder::startPlay(const std::string& strUrl)
{
    std::lock_guard<std::mutex> guard(m_control_mutex);
    m_isStop = false;
    startPlayLocked(strUrl);
}

// Assumes m_control_mutex is held.
void WebRTCDecoder::startPlayLocked(const std::string& strUrl)
{
    int waitCount = 100;
    while (m_status == CONNECTTING && waitCount-- > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

    const bool same_url = (strUrl == m_url);
    // "Already playing the same URL" is only a valid reason to no-op if
    // the stream is actually still delivering frames. The old code
    // skipped the rebuild on status==CONNECTED alone, so a reload never
    // recovered a connection the peer had dropped silently - only an app
    // restart did.
    if (same_url && m_status == CONNECTED && !isStreamDead())
    {
        BOOST_LOG_TRIVIAL(info) << "[WebRTC] startPlay(" << strUrl << ") ignored: stream already alive";
        return;
    }

    std::cout << "connected!" << m_status << ":" << strUrl << ":" << m_url << "\r\n";
    BOOST_LOG_TRIVIAL(info) << "[WebRTC] startPlay url=" << strUrl << " prevUrl=" << m_url
        << " prevStatus=" << m_status.load() << " sameUrl=" << same_url;

    // Any previous session (connected, dead, or a stuck CONNECTTING) must
    // be fully torn down before playRtc() runs again - the socket layer
    // is not safe against overlapping playRtc() calls on the singleton.
    if (m_status != STOPPED || m_playFutrue.valid())
    {
        BOOST_LOG_TRIVIAL(warning) << "[WebRTC] tearing down previous session before (re)connect (status="
            << m_status.load() << ")";
        stopPlayLocked();
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
    }

    m_status = CONNECTTING;
    m_connect_started_ns = now_ns();
    m_url = strUrl;
    m_isStop = false;
    m_stall_logged = false;
    m_last_frame_pts = -1;
    // Treat the connect start like a frame so isStreamDead() has a
    // reference point until the first real frame arrives.
    m_last_frame_ns = now_ns();

    m_context->synMgr.session->playBuffer->resetVideoClock(m_context->synMgr.session->playBuffer->session);
    int32_t err = m_player->playRtc(0, const_cast<char*>(m_url.c_str()));
    std::cout << "connected!" << err << "\r\n";
    BOOST_LOG_TRIVIAL(info) << "[WebRTC] playRtc(" << m_url << ") returned " << err;
    if (!err)
    {
        m_playFutrue = std::async(std::launch::async, [this](){
            while (!this->m_isStop)
            {
                this->receiveFrame();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });
        // Arm the stall watchdog for the lifetime of this stream.
        // Idempotent: a no-op if one is already running (e.g. this call
        // was the watchdog itself reconnecting).
        startWatchdog();
    }
    else
    {
        BOOST_LOG_TRIVIAL(error) << "[WebRTC] playRtc(" << m_url << ") failed synchronously, err=" << err;
        m_status = STOPPED;
    }
}

bool WebRTCDecoder::isStreamDead() const
{
    const int64_t n = now_ns();
    const Status s = m_status.load();
    if (s == CONNECTED)
        return n - m_last_frame_ns.load() > (int64_t) kStreamDeadSeconds * 1000000000LL;
    if (s == CONNECTTING)
        return n - m_connect_started_ns.load() > (int64_t) kConnectStuckSeconds * 1000000000LL;
    return false;
}

void WebRTCDecoder::startWatchdog()
{
    if (m_watchdog_running.exchange(true))
        return; // already running
    m_watchdog_stop = false;
    m_watchdogFuture = std::async(std::launch::async, [this]{ this->watchdogLoop(); });
}

void WebRTCDecoder::stopWatchdog()
{
    m_watchdog_stop = true;
    if (m_watchdogFuture.valid())
        m_watchdogFuture.wait();
    m_watchdog_running = false;
}

void WebRTCDecoder::watchdogLoop()
{
    while (!m_watchdog_stop)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (m_watchdog_stop || m_isStop)
            continue;

        // Cheap pre-check without the lock. A CONNECTED stream that went
        // silent, a CONNECTTING that never resolved, or a STOPPED session
        // that failed/dropped (as opposed to an explicit stopPlay(), which
        // also kills this loop) all warrant a rebuild.
        if (m_status != STOPPED && !isStreamDead())
            continue;

        // Don't block on a startPlay()/stopPlay() in flight - retry next tick.
        std::unique_lock<std::mutex> lk(m_control_mutex, std::try_to_lock);
        if (!lk.owns_lock())
            continue;
        if (m_watchdog_stop || m_isStop || m_url.empty())
            continue;
        if (m_status != STOPPED && !isStreamDead())
            continue;

        const std::string url = m_url;
        BOOST_LOG_TRIVIAL(warning) << "[WebRTC] webrtc stream stalled, reconnecting: " << url;
        startPlayLocked(url);
    }
}


int WebRTCDecoder::width() 
{
    return m_width;
}
int WebRTCDecoder::height(){
    return m_height;
    }
void YUV420P_to_RGB24(const unsigned char* yuv, unsigned char* rgb, int width, int height) {
    const unsigned char* y_plane = yuv;
    const unsigned char* u_plane = yuv + width * height;
    const unsigned char* v_plane = yuv + width * height + (width / 2) * (height / 2);
    
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int yy = y_plane[y * width + x];
            int uu = u_plane[(y / 2) * (width / 2) + (x / 2)];
            int vv = v_plane[(y / 2) * (width / 2) + (x / 2)];
            
            // YUV to RGB conversion formulas
            int r = yy + 1.402 * (vv - 128);
            int g = yy - 0.34414 * (uu - 128) - 0.71414 * (vv - 128);
            int b = yy + 1.772 * (uu - 128);
            
            // Clamp values to [0, 255]
            r = std::max(0, std::min(255, r));
            g = std::max(0, std::min(255, g));
            b = std::max(0, std::min(255, b));
            
            rgb[(y * width + x) * 3 + 0] = r;
            rgb[(y * width + x) * 3 + 1] = g;
            rgb[(y * width + x) * 3 + 2] = b;
        }
    }
}
    /**
 * 将 RGB 数据压缩为 JPEG 并存储在内存中
 * 
 * @param rgb_data 输入的 RGB 数据 (格式为 R,G,B,R,G,B,...)
 * @param width 图像宽度
 * @param height 图像高度
 * @param quality JPEG 质量 (1-100)
 * @param[out] out_buffer 输出的 JPEG 数据
 * @param[out] out_size 输出的 JPEG 数据大小
 * 
 * @return 成功返回 true，失败返回 false
 */
bool rgb_to_jpeg(const unsigned char* rgb_data, int width, int height, int quality,
    std::vector<unsigned char>& out_buffer) {
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;

    // 初始化 JPEG 压缩对象
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    // 设置内存目标
    unsigned char* buffer = nullptr;
    unsigned long buffer_size = 0;
    jpeg_mem_dest(&cinfo, &buffer, &buffer_size);

    // 设置图像参数
    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;  // RGB 有 3 个分量
    cinfo.in_color_space = JCS_RGB;

    // 设置默认参数
    jpeg_set_defaults(&cinfo);

    // 设置质量
    jpeg_set_quality(&cinfo, quality, TRUE);

    // 开始压缩
    jpeg_start_compress(&cinfo, TRUE);

    // 逐行写入数据
    JSAMPROW row_pointer[1];
    int row_stride = width * 3;  // RGB 每行有 width*3 字节

    while (cinfo.next_scanline < cinfo.image_height) {
    row_pointer[0] = (JSAMPROW)&rgb_data[cinfo.next_scanline * row_stride];
    jpeg_write_scanlines(&cinfo, row_pointer, 1);
}

// 完成压缩
jpeg_finish_compress(&cinfo);

// 将数据拷贝到输出缓冲区
out_buffer.assign(buffer, buffer + buffer_size);

// 清理
jpeg_destroy_compress(&cinfo);
free(buffer);

return true;
}
std::vector<unsigned char> WebRTCDecoder::getFrameData()
{
    // Return a copy taken under the lock. The previous version released
    // the lock (or never took it, on the not-CONNECTED path) and handed
    // back a reference to m_frame_data that the receive thread could be
    // reallocating from under the HTTP handler at the same time.
    std::lock_guard<std::mutex> guard(this->frame_mutex_);
    return m_frame_data;
}
void WebRTCDecoder::receiveFrame(){
     //uint8_t* t_vb = m_context->synMgr.session->playBuffer->getVideoRef(m_context->synMgr.session->playBuffer->session, &m_frame);
    // Runs only on the receive thread, so m_last_frame_pts / m_stall_logged
    // / m_last_novideobuffer_log need no lock. frame_mutex_ is taken only
    // for the buffer swap at the end.
    const int64_t now = now_ns();
    const auto now_tp = std::chrono::steady_clock::now();
    YangVideoBuffer*  vb = m_player->getVideoBuffer();
    if(vb == nullptr)
    {
        // No video buffer at all: the player isn't producing anything.
        // getFrameData() keeps handing out the last JPEG it ever decoded
        // regardless, so the browser just sees a frozen image. Throttled
        // since this is polled every ~2ms.
        if(m_status == CONNECTED && now_tp - m_last_novideobuffer_log > std::chrono::seconds(3))
        {
            BOOST_LOG_TRIVIAL(warning) << "[WebRTC] no video buffer from player while status=CONNECTED (url="
                << m_url << ")";
            m_last_novideobuffer_log = now_tp;
        }
        return;
    }
    uint8_t* t_vb = vb->getVideoRef(&m_frame);
    // getVideoRef() can return a non-null pointer into the ring buffer
    // even when no new frame has arrived since the last poll, so use the
    // frame's own pts to tell an actually-new frame apart from the same
    // stale one being handed back repeatedly.
    const bool isNewFrame = t_vb && m_frame.pts != m_last_frame_pts;
    if (t_vb && isNewFrame)
    {
        const int w = vb->m_width;
        const int h = vb->m_height;
        m_last_frame_pts = m_frame.pts;

        if(w <= 0 || h <= 0)
        {
            // Non-positive size. The old code took frame_mutex_ and then
            // returned from inside the locked region without unlocking,
            // wedging every later getFrameData() / receiveFrame() forever
            // - the freeze nothing but an app restart could clear. Now
            // nothing is locked on this path.
            BOOST_LOG_TRIVIAL(warning) << "[WebRTC] got a frame with invalid size " << w << "x" << h
                << " (url=" << m_url << ")";
            return;
        }

        // Decode outside the lock so getFrameData() isn't blocked for the
        // whole YUV->RGB->JPEG conversion on every frame.
        std::vector<unsigned char> yuvData(w * h * 3 / 2);
        std::vector<unsigned char> rgbData(w * h * 3);
        std::copy(t_vb, t_vb + yuvData.size(), yuvData.begin());
        YUV420P_to_RGB24(yuvData.data(), rgbData.data(), w, h);
        std::vector<unsigned char> frame_data;
        const bool ok = rgb_to_jpeg(rgbData.data(), w, h, /*quality*/ 90, frame_data);

        if (ok)
        {
            std::lock_guard<std::mutex> guard(this->frame_mutex_);
            m_width = w;
            m_height = h;
            m_frame_data = std::move(frame_data);
        }

        if(m_stall_logged)
        {
            BOOST_LOG_TRIVIAL(info) << "[WebRTC] frame stream resumed after "
                << (now - m_last_frame_ns.load()) / 1000000 << "ms without a new frame (url=" << m_url << ")";
            m_stall_logged = false;
        }
        m_last_frame_ns = now;
    }
    else if(m_status == CONNECTED)
    {
        // No new frame this poll. Normal for brief gaps at the stream's
        // framerate, but if it drags on while still CONNECTED the rtc link
        // most likely died silently (e.g. wifi drop) without failure()
        // ever firing. Log once when the gap crosses 3s; the watchdog
        // rebuilds the connection once it crosses kStreamDeadSeconds.
        const int64_t gap_ms = (now - m_last_frame_ns.load()) / 1000000;
        if(gap_ms > 3000 && !m_stall_logged)
        {
            BOOST_LOG_TRIVIAL(warning) << "[WebRTC] video stream appears stalled: no new frame for "
                << gap_ms << "ms (url=" << m_url << ")";
            m_stall_logged = true;
        }
    }
}
void WebRTCDecoder::success()
{
    m_status = CONNECTED;
    // Reset the stall clock so a slow first frame after connect doesn't
    // immediately look dead to the watchdog.
    m_last_frame_ns = now_ns();
    const auto elapsed = (now_ns() - m_connect_started_ns.load()) / 1000000;
    BOOST_LOG_TRIVIAL(info) << "[WebRTC] connected: url=" << m_url << " after " << elapsed << "ms";
}
void WebRTCDecoder::failure(int32_t errcode)
{
    m_status = STOPPED;
    BOOST_LOG_TRIVIAL(error) << "[WebRTC] connection failed: url=" << m_url << " errcode=" << errcode;
    //emit RtcConnectFailure(errcode);
}
