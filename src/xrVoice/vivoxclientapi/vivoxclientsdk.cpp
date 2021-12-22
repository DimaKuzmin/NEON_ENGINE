/* Copyright (c) 2013-2018 by Mercer Road Corp
 *
 * Permission to use, copy, modify or distribute this software in binary or source form
 * for any purpose is allowed only under explicit prior consent in writing from Mercer Road Corp
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND MERCER ROAD CORP DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL MERCER ROAD CORP
 * BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */
#include "../stdafx.h"
#include "vivoxclientsdk.h"
#include "VxcErrors.h"
#include "Vxc.h"
#include "VxcRequests.h"
#include "VxcEvents.h"
#include <stdint.h>
#include <sstream>
#include <set>
#include <string>
#include <string.h>
#include <mutex>
#include <atomic>
#include <memory>

#include <Windows.h>

#include <assert.h>
#include <map>
#include <vector>
#include "types.h"
#include "memallocators.h"



namespace VivoxClientApi {
static std::string CodePageToUTF8(const char *cpBuf, size_t cpBufLen)
{
    if (cpBuf == 0 || cpBufLen == 0 || cpBuf[0] == 0) {
        return "";
    }
    std::string uBuf;
    size_t wLen = cpBufLen * 2;
    WCHAR *wideBuf = new WCHAR[wLen];
    if (wideBuf != 0) {
        memset(wideBuf, 0, wLen);
        int wideCount = MultiByteToWideChar(GetACP(), 0, cpBuf, (int)cpBufLen, wideBuf, (int)wLen);
        if (wideCount >= 0) {
            uBuf.resize(wLen * 2);
            int uCount = WideCharToMultiByte(CP_UTF8, 0, wideBuf, wideCount, (char *)uBuf.data(), (int)uBuf.size(), NULL, NULL);
            if (uCount >= 0) {
                uBuf.resize((size_t)uCount);
            } else {
                uBuf = "";
            }
        }
        delete[] wideBuf;
    }
    return uBuf;
}


#define CHECK_RET(x) if (!(x)) { m_app->onAssert(__FUNCTION__, __LINE__,#x); return; }
#define CHECK_RET1(x, y) if (!(x)) { m_app->onAssert(__FUNCTION__, __LINE__,#x); return y; }
#define CHECK(x) if (!(x)) { m_app->onAssert(__FUNCTION__, __LINE__,#x); }
#define CHECK_CONT(x) if (!(x)) { m_app->onAssert(__FUNCTION__, __LINE__,#x); continue; }
#define CHECK_BREAK(x) if (!(x)) { m_app->onAssert(__FUNCTION__, __LINE__,#x); break; }
#define ALWAYS_ASSERT(x) m_app->onAssert(__FUNCTION__, __LINE__,#x)

//#   define LOG_ERR(...) fprintf(stderr, __VA_ARGS__)
//#   define LOG_INFO(...) fprintf(stdout, __VA_ARGS__)

#   define LOG_ERR(...) Msg(__VA_ARGS__)
#   define LOG_INFO(...) Msg(__VA_ARGS__)


static AudioDeviceId AudioDeviceIdFromCodePage(const char *device_id, const char *device_name)
{
    return AudioDeviceId(device_id, CodePageToUTF8(device_name, strlen(device_name) + 1));
}

static std::string AudioDeviceIdString(const AudioDeviceId &id)
{
    return id.GetAudioDeviceId().c_str();
}

static VCSStatus issueRequest(vx_req_base_t *request)
{
    int outstandingRequestCount = 0;
#ifdef _DEBUG
    char *xml = NULL;
    vx_request_to_xml(request, &xml);
    OutputDebugStringA(xml);
    OutputDebugStringA("\r\n");
    vx_free(xml);
#endif
    VCSStatus status = vx_issue_request3(request, &outstandingRequestCount);
    if (outstandingRequestCount > 10) {
        LOG_ERR("warning: outstandingRequestCount = %d\n", outstandingRequestCount);
    }
    return status;
}

#ifdef _DEBUG
std::string NowString()
{
    char buf[80];
    SYSTEMTIME lt;
    GetLocalTime(&lt);
    _snprintf_c(buf, sizeof(buf), "%02d:%02d:%02d.%03d", lt.wHour, lt.wMinute, lt.wSecond, lt.wMilliseconds);
    return buf;
}
#endif
static void sleepMicroseconds(size_t us)
{
    SleepEx((DWORD)(us / 1000), TRUE);
}

std::vector<std::string> split(const char *s)
{
    std::vector<std::string> ss;
    std::string t = s;
    size_t lastpos = 0;
    for (;;) {
        size_t pos = t.find("\n", lastpos);
        if (pos > lastpos && pos != 0) {
            if (pos == std::string::npos) {
                if (lastpos < t.size()) {
                    ss.push_back(t.substr(lastpos, t.size() - lastpos));
                }
                break;
            } else {
                ss.push_back(t.substr(lastpos, pos - lastpos));
            }
        }
        lastpos = pos + 1;
        if (lastpos >= t.size()) {
            break;
        }
    }
    return ss;
}

static const char *safe_str(const char *s)
{
    if (s == NULL) {
        return "";
    }
    return s;
}

static char *GetNextRequestId(const char *parent, const char *prefix)
{
    static int lastRequestId = 0;
    std::stringstream ss;
    if (parent && parent[0]) {
        ss << parent << "." << prefix << lastRequestId++;
        return vx_strdup(ss.str().c_str());
    } else {
        ss << prefix << lastRequestId++;
        return vx_strdup(ss.str().c_str());
    }
}

class Participant
{
public:
    Participant(IClientApiEventHandler *app, const Uri &uri) :
        m_app(app),
        m_uri(uri)
    {
        (void)m_app;
        m_isSpeaking = -1;
        m_energy = -1;
        m_currentVolume = 50;
        m_desiredVolume = 50;
        m_currentMutedForMe = false;
        m_desiredMutedForMe = false;
        m_volumeRequestInProgress = false;
        m_mutedForMeRequestInProgress = false;
        m_mutedForAll = -1;
    }

    void NextState(const std::string &sessionHandle, const Uri &channelUri)
    {
        (void)channelUri;
        if (!m_volumeRequestInProgress && m_currentVolume != m_desiredVolume) {
            vx_req_session_set_participant_volume_for_me_t *req;
            vx_req_session_set_participant_volume_for_me_create(&req);
            req->session_handle = vx_strdup(sessionHandle.c_str());
            req->participant_uri = vx_strdup(m_uri.ToString());
            req->volume = m_desiredVolume;
            issueRequest(&req->base);
            m_volumeRequestInProgress = true;
        }
        if (!m_mutedForMeRequestInProgress && m_currentMutedForMe != m_desiredMutedForMe) {
            vx_req_session_set_participant_mute_for_me_t *req;
            vx_req_session_set_participant_mute_for_me_create(&req);
            req->session_handle = vx_strdup(sessionHandle.c_str());
            req->participant_uri = vx_strdup(m_uri.ToString());
            req->mute = m_desiredMutedForMe ? 1 : 0;
            issueRequest(&req->base);
            m_mutedForMeRequestInProgress = true;
        }
    }

    // returns true if updated
    bool SetIsSpeaking(bool value)
    {
        if (m_isSpeaking == -1 || value != (m_isSpeaking == 1 ? true : false)) {
            m_isSpeaking = value ? 1 : 0;
            return true;
        }
        return false;
    }

    bool SetEnergy(double value)
    {
        if (m_energy != value) {
            m_energy = value;
            return true;
        }
        return false;
    }

    bool SetMutedForAll(bool value)
    {
        if (m_mutedForAll == -1 || value != (m_mutedForAll == 1 ? true : false)) {
            m_mutedForAll = value ? 1 : 0;
            return true;
        }
        return false;
    }

    bool GetMutedForAll() const
    {
        return m_mutedForAll > 0;
    }

    const Uri &GetUri() const { return m_uri; }
    bool GetIsSpeaking() const { return m_isSpeaking > 0; }
    double GetEnergy() const { return m_energy; }
    int GetCurrentVolume() const { return m_currentVolume; }
    bool GetCurrentMutedForMe() const { return m_currentMutedForMe; }
    int GetDesiredVolume() const { return m_desiredVolume; }
    bool GetDesiredMutedForMe() const { return m_desiredMutedForMe; }
    void SetCurrentVolume(int value) { m_currentVolume = value; }
    void SetCurrentMutedForMe(bool muted) { m_currentMutedForMe = muted; }
    void SetDesiredVolume(int value) { m_desiredVolume = value; }
    void SetDesiredMutedForMe(bool muted) { m_desiredMutedForMe = muted; }
    void SetVolumeRequestInProgress(bool value) { m_volumeRequestInProgress = value; }
    void SetMutedForMeRequestInProgress(bool value) { m_mutedForMeRequestInProgress = value; }

private:
    IClientApiEventHandler *m_app;
    Uri m_uri;
    int m_isSpeaking;
    double m_energy;
    int m_currentVolume;
    bool m_currentMutedForMe;
    int m_desiredVolume;
    bool m_desiredMutedForMe;
    bool m_volumeRequestInProgress;
    bool m_mutedForMeRequestInProgress;
    int m_mutedForAll;
};

class Channel
{
public:
    typedef enum {
        ChannelStateDisconnected,
        ChannelStateConnecting,
        ChannelStateConnected,
        ChannelStateDisconnecting
    } ChannelState;

    Channel(IClientApiEventHandler *app, const Uri &uri, const AccountName &accountName, const std::string &accountHandle, const std::string &sessionGroupHandle) :
        m_app(app)
    {
        CHECK(app != NULL);
        CHECK(uri.IsValid());
        m_channelUri = uri;
        CHECK(accountName.IsValid() && !accountName.IsAnonymous());
        m_accountName = accountName;
        m_accountHandle = accountHandle;               // NB: accountHandle and sessionGroupHandle may be empty in the case when the Channel is created before SingleLoginMultiChannel manager is logged in
        m_sessionGroupHandle = sessionGroupHandle;     // In this case they will be initialized later in SetAccountHandle() and SetSessionGroupHandle()
        m_currentState = ChannelStateDisconnected;
        m_desiredState = ChannelStateDisconnected;
        m_currentVolume = 50;
        m_desiredVolume = 50;
        m_volumeRequestInProgress = false;
    }

    virtual ~Channel()
    {
        ClearParticipants();
    }

    void SetAccountHandle(const std::string &accountHandle)
    {
        // Channel objects can be constructed before SingleLoginMultiChannelManager goes to login, thus m_accountHandle is set here instead of constructor
        CHECK_RET(!accountHandle.empty());
        m_accountHandle = accountHandle;
    }

    void SetSessionGroupHandle(const std::string &sessionGroupHandle)
    {
        CHECK_RET(!sessionGroupHandle.empty());
        if (!m_sessionGroupHandle.empty() && m_currentState != ChannelStateDisconnected) {
            // already set and the channel is not disconnected
            return;
        }
        m_sessionGroupHandle = sessionGroupHandle;
    }

    VCSStatus Join(const char *accessToken)
    {
        m_accessToken = safe_str(accessToken);
        m_desiredState = ChannelStateConnected;
        return 0;
    }

    void Leave()
    {
        m_desiredState = ChannelStateDisconnected;
    }

    void NextState()
    {
        if (m_currentState == ChannelStateDisconnected && m_desiredState == ChannelStateConnected) {
            CHECK_RET(m_channelUri.IsValid());
            {
                vx_req_sessiongroup_add_session *req;
                vx_req_sessiongroup_add_session_create(&req);
                req->connect_audio = 1;
                req->connect_text = 0;
                req->uri = vx_strdup(m_channelUri.ToString());
                req->sessiongroup_handle = vx_strdup(m_sessionGroupHandle.c_str());
                req->base.cookie = GetNextRequestId(NULL, "S");
                req->session_handle = vx_strdup(req->base.cookie);
                req->account_handle = vx_strdup(m_accountHandle.c_str());
                if (!m_accessToken.empty()) {
                    req->access_token = vx_strdup(m_accessToken.c_str());
                }
                m_sessionHandle = req->session_handle;
                m_currentState = ChannelStateConnecting;
#ifdef _DEBUG
                LOG_INFO("%s: issuing vx_req_sessiongroup_add_session to %s\n", NowString().c_str(), req->uri);
#endif
                issueRequest(&req->base);
            }
            {
                vx_req_sessiongroup_set_tx_all_sessions_t *req;
                vx_req_sessiongroup_set_tx_all_sessions_create(&req);
                req->sessiongroup_handle = vx_strdup(m_sessionGroupHandle.c_str());
                issueRequest(&req->base);
            }
        } else if ((m_currentState == ChannelStateConnecting || m_currentState == ChannelStateConnected) && m_desiredState == ChannelStateDisconnected) {
            vx_req_sessiongroup_remove_session *req;
            vx_req_sessiongroup_remove_session_create(&req);
            req->session_handle = vx_strdup(m_sessionHandle.c_str());
            req->sessiongroup_handle = vx_strdup(m_sessionGroupHandle.c_str());
            m_currentState = ChannelStateDisconnecting;
            issueRequest(&req->base);
        } else if (m_currentState == ChannelStateConnected) {
            if (!m_volumeRequestInProgress && m_currentVolume != m_desiredVolume) {
                vx_req_session_set_local_render_volume_t *req;
                vx_req_session_set_local_render_volume_create(&req);
                req->session_handle = vx_strdup(m_sessionHandle.c_str());
                req->volume = m_desiredVolume;
                issueRequest(&req->base);
                m_volumeRequestInProgress = true;
            }
        }
    }

    ChannelState GetDesiredState() const { return m_desiredState; }
    ChannelState GetCurrentState() const { return m_currentState; }
    void SetCurrentState(ChannelState value)
    {
        if (m_currentState != value) {
            m_currentState = value;
            if (m_currentState == ChannelStateDisconnected) {
                ClearParticipants();
            }
        }
    }
    void SetDesiredState(ChannelState value) { m_desiredState = value; }

    int GetCurrentVolume() const { return m_currentVolume; }
    int GetDesiredVolume() const { return m_desiredVolume; }
    bool GetVolumeRequestInProgress() const { return m_volumeRequestInProgress; }
    void SetCurrentVolume(int value) { m_currentVolume = value; }
    void SetDesiredVolume(int value) { m_desiredVolume = value; }
    void SetVolumeRequestInProgress(bool value) { m_volumeRequestInProgress = value; }

    const Uri &GetUri() const { return m_channelUri; }
    const std::string &GetSessionHandle() const { return m_sessionHandle; }

    int GetParticipantAudioOutputDeviceVolumeForMe(const Uri &target)
    {
        Participant *p = FindParticipantByUri(target, false);
        if (p == NULL) {
            return 50;     /// default value
        }
        return p->GetCurrentVolume();
    }

    VCSStatus SetParticipantAudioOutputDeviceVolumeForMe(const Uri &target, int volume)
    {
        Participant *p = FindParticipantByUri(target, false);
        if (p == NULL) 
        {
            return VX_E_NO_EXIST;
        }
        if (volume != p->GetDesiredVolume()) 
        {
            p->SetDesiredVolume(volume);
            p->NextState(m_sessionHandle, m_channelUri);
        }
        return 0;
    }

    VCSStatus SetParticipantMutedForAll(const Uri &target, bool muted, const char *accessToken)
    {
        vx_req_channel_mute_user_t *req;
        vx_req_channel_mute_user_create(&req);
        req->account_handle = vx_strdup(m_accountHandle.c_str());
        req->channel_uri = vx_strdup(m_channelUri.ToString());
        req->participant_uri = vx_strdup(target.ToString());
        req->set_muted = muted ? 1 : 0;
        if (NULL != accessToken) {
            req->access_token = vx_strdup(accessToken);
        }
        return issueRequest(&req->base);
    }

    bool GetParticipantMutedForAll(const Uri &target)
    {
        Participant *p = FindParticipantByUri(target);
        if (p == NULL) {
            return false;
        }
        return p->GetMutedForAll();
    }

    VCSStatus SetParticipantMutedForMe(const Uri &target, bool muted)
    {
        Participant *p = FindParticipantByUri(target);
        if (p == NULL) {
            return VX_E_NO_EXIST;
        }
        if (muted != p->GetDesiredMutedForMe()) {
            p->SetDesiredMutedForMe(muted);
            p->NextState(m_sessionHandle, m_channelUri);
        }
        return 0;
    }

    VCSStatus MuteAll(bool set_muted, const char *accessToken)
    {
        vx_req_channel_mute_all_users_t *req;
        vx_req_channel_mute_all_users_create(&req);
        req->account_handle = vx_strdup(m_accountHandle.c_str());
        req->channel_uri = vx_strdup(m_channelUri.ToString());
        req->set_muted = set_muted ? 1 : 0;
        if (NULL != accessToken) {
            req->access_token = vx_strdup(accessToken);
        }
        return issueRequest(&req->base);
    }

    VCSStatus KickUser(const Uri &userUri, const char *accessToken)
    {
        vx_req_channel_kick_user_t *req;
        vx_req_channel_kick_user_create(&req);
        req->account_handle = vx_strdup(m_accountHandle.c_str());
        req->channel_uri = vx_strdup(m_channelUri.ToString());
        req->participant_uri = vx_strdup(userUri.ToString());
        if (NULL != accessToken) {
            req->access_token = vx_strdup(accessToken);
        }
        return issueRequest(&req->base);
    }

    VCSStatus SetTransmissionToThisChannel()
    {
        vx_req_sessiongroup_set_tx_session_t *req;
        vx_req_sessiongroup_set_tx_session_create(&req);
        req->session_handle = vx_strdup(m_sessionHandle.c_str());
        return issueRequest(&req->base);
    }

    VCSStatus Set3DPosition(double x, double y, double z, double at_x, double at_y, double at_z)
    {
        vx_req_session_set_3d_position_t *req;
        vx_req_session_set_3d_position_create(&req);
        req->req_disposition_type = req_disposition_no_reply_required;
        req->session_handle = vx_strdup(m_sessionHandle.c_str());
        req->listener_position[0] = x;
        req->listener_position[1] = y;
        req->listener_position[2] = z;
        req->speaker_position[0] = x;
        req->speaker_position[1] = y;
        req->speaker_position[2] = z;

        req->listener_at_orientation[0] = at_x;
        req->listener_at_orientation[1] = at_y;
        req->listener_at_orientation[2] = at_z;

        /* There is usuall not a good reason to change the up vector
         * The player usually prefers hearing in an up right position
         * even when his character is upside down or horizantal in a game.
         */

        req->listener_up_orientation[0]  = 0;
        req->listener_up_orientation[1] = 1;
        req->listener_up_orientation[2] = 0;

        return issueRequest(&req->base);
    }

    VCSStatus SetSttTranscriptionOn(bool on, const char *accessToken)
    {
        vx_req_session_transcription_control_t *req;
        vx_req_session_transcription_control_create(&req);
        req->session_handle = vx_strdup(m_sessionHandle.c_str());
        req->enable = on ? 1 : 0;
        req->access_token = vx_strdup(accessToken);
        return issueRequest(&req->base);
    }

    void HandleResponse(vx_resp_session_set_local_speaker_volume *resp)
    {
        vx_req_session_set_local_speaker_volume_t *req = reinterpret_cast<vx_req_session_set_local_speaker_volume_t *>(resp->base.request);
        if (resp->base.return_code != 0) {
            if (m_desiredVolume == req->volume) {
                m_desiredVolume = m_currentVolume;
            }
            m_app->onSetChannelAudioOutputDeviceVolumeFailed(m_accountName, m_channelUri, req->volume, resp->base.status_code);
        } else {
            m_currentVolume = req->volume;
            m_app->onSetChannelAudioOutputDeviceVolumeCompleted(m_accountName, m_channelUri, req->volume);
        }
        m_volumeRequestInProgress = false;
    }

    void HandleResponse(vx_resp_session_set_local_render_volume *resp)
    {
        vx_req_session_set_local_render_volume_t *req = reinterpret_cast<vx_req_session_set_local_render_volume_t *>(resp->base.request);
        if (resp->base.return_code != 0) {
            if (m_desiredVolume == req->volume) {
                m_desiredVolume = m_currentVolume;
            }
            m_app->onSetChannelAudioOutputDeviceVolumeFailed(m_accountName, m_channelUri, req->volume, resp->base.status_code);
        } else {
            m_currentVolume = req->volume;
            m_app->onSetChannelAudioOutputDeviceVolumeCompleted(m_accountName, m_channelUri, req->volume);
        }
        m_volumeRequestInProgress = false;
    }

    void HandleResponse(vx_resp_session_set_participant_volume_for_me *resp)
    {
        vx_req_session_set_participant_volume_for_me_t *req = reinterpret_cast<vx_req_session_set_participant_volume_for_me_t *>(resp->base.request);
        CHECK_RET(req->participant_uri != NULL);
        Participant *p = FindParticipantByUri(Uri(req->participant_uri));
        CHECK_RET(p != NULL);
        if (resp->base.return_code != 0) {
            if (p->GetDesiredVolume() == req->volume) {
                p->SetDesiredVolume(p->GetCurrentVolume());
            }
            m_app->onSetParticipantAudioOutputDeviceVolumeForMeFailed(m_accountName, Uri(req->participant_uri), m_channelUri, req->volume, resp->base.status_code);
        } else {
            p->SetCurrentVolume(req->volume);
            m_app->onSetParticipantAudioOutputDeviceVolumeForMeCompleted(m_accountName, Uri(req->participant_uri), m_channelUri, req->volume);
        }
        p->SetVolumeRequestInProgress(false);
        p->NextState(m_sessionHandle, m_channelUri);
    }

    void HandleResponse(vx_resp_channel_mute_user *resp)
    {
        vx_req_channel_mute_user_t *req = reinterpret_cast<vx_req_channel_mute_user_t *>(resp->base.request);
        Participant *p = FindParticipantByUri(Uri(req->participant_uri), false);
        CHECK_RET(p != NULL);
        bool req_muted = req->set_muted ? true : false;
        if (resp->base.return_code != 0) {
            m_app->onSetParticipantMutedForAllFailed(m_accountName, Uri(req->participant_uri), m_channelUri, req_muted, resp->base.status_code);
        } else {
            m_app->onSetParticipantMutedForAllCompleted(m_accountName, Uri(req->participant_uri), m_channelUri, req_muted);
        }
        p->NextState(m_sessionHandle, m_channelUri);
    }

    void HandleResponse(vx_resp_channel_mute_all_users *resp)
    {
        vx_req_channel_mute_all_users_t *req = reinterpret_cast<vx_req_channel_mute_all_users_t *>(resp->base.request);
        bool req_muted = req->set_muted ? true : false;
        if (resp->base.return_code != 0) {
            m_app->onMuteAllFailed(m_accountName, m_channelUri, req_muted, resp->base.status_code);
        } else {
            m_app->onMuteAllCompleted(m_accountName, m_channelUri, req_muted);
        }
    }

    void HandleResponse(vx_resp_session_set_participant_mute_for_me *resp)
    {
        vx_req_session_set_participant_mute_for_me_t *req = reinterpret_cast<vx_req_session_set_participant_mute_for_me_t *>(resp->base.request);
        Participant *p = FindParticipantByUri(Uri(req->participant_uri), false);
        CHECK_RET(p != NULL);
        bool req_muted = req->mute ? true : false;
        if (resp->base.return_code != 0) {
            if (p->GetDesiredMutedForMe() == req_muted) {
                p->SetDesiredMutedForMe(p->GetCurrentMutedForMe());
            }
            m_app->onSetParticipantMutedForMeFailed(m_accountName, Uri(req->participant_uri), m_channelUri, req_muted, resp->base.status_code);
        } else {
            p->SetCurrentMutedForMe(req_muted);
            m_app->onSetParticipantMutedForMeCompleted(m_accountName, Uri(req->participant_uri), m_channelUri, req_muted);
        }
        p->SetMutedForMeRequestInProgress(false);
        p->NextState(m_sessionHandle, m_channelUri);
    }

    void HandleEvent(vx_evt_participant_added *evt)
    {
        Participant *p = FindParticipantByUri(Uri(evt->participant_uri), false);
        CHECK_RET(p == NULL);
        p = FindParticipantByUri(Uri(evt->participant_uri), true);
        CHECK_RET(p != NULL);
        if (evt->is_current_user) {
            CHECK(GetCurrentState() == Channel::ChannelStateConnecting);
            if (GetCurrentState() == Channel::ChannelStateConnecting) {
                SetCurrentState(Channel::ChannelStateConnected);
                m_app->onChannelJoined(m_accountName, GetUri());
                m_app->onChannelJoinedEx(m_accountName, GetUri(), m_sessionGroupHandle.c_str(), m_sessionHandle.c_str());
            }
        }

        m_app->onParticipantAdded(m_accountName, m_channelUri, p->GetUri(), evt->is_current_user != 0 ? true : false);
    }

    void HandleEvent(vx_evt_participant_updated *evt)
    {
        Participant *p = FindParticipantByUri(Uri(evt->participant_uri));
        // CHECK_RET(p != NULL);
        if (p != NULL) {
            bool changed = p->SetIsSpeaking(evt->is_speaking ? true : false);
            changed |= p->SetEnergy(evt->energy);
            changed |= p->SetMutedForAll(evt->is_moderator_muted ? true : false);
            if (changed) {
                m_app->onParticipantUpdated(m_accountName, m_channelUri, p->GetUri(), evt->is_current_user != 0 ? true : false, p->GetIsSpeaking(), p->GetEnergy(), p->GetMutedForAll());
            }
        } else {
            // a rarely seen case where a participant_updated event is see without a participant_added event arriving first
            Uri uri;
            uri = (const Uri)evt->participant_uri;
            m_app->onParticipantAdded(m_accountName, m_channelUri, uri, evt->is_current_user != 0 ? true : false);
        }
    }

    void HandleEvent(vx_evt_participant_removed *evt)
    {
        Participant *p = FindParticipantByUri(Uri(evt->participant_uri), false);
        // CHECK_RET(p != NULL);
        if (p != NULL) {
            m_app->onParticipantLeft(m_accountName, m_channelUri, p->GetUri(), evt->is_current_user != 0 ? true : false, (IClientApiEventHandler::ParticipantLeftReason)evt->reason);
            m_participants.erase(p->GetUri());
            delete p;
        }
    }

    void HandleEvent(vx_evt_transcribed_message *evt)
    {
        m_app->onSttTranscriptionReceived(m_accountName, Uri(evt->participant_uri), m_channelUri, evt->text, evt->language, evt->participant_displayname);
    }

private:
    void ClearParticipants()
    {
        for (std::map<Uri, Participant *>::const_iterator i = m_participants.begin(); i != m_participants.end(); ++i) {
            delete i->second;
        }
        m_participants.clear();
    }

    Participant *FindParticipantByUri(const Uri &uri, bool create = false)
    {
        std::map<Uri, Participant *>::const_iterator i = m_participants.find(uri);
        if (i == m_participants.end()) {
            if (create) {
                Participant *p = new Participant(m_app, uri);
                m_participants[uri] = p;
                return p;
            } else {
                return NULL;
            }
        } else {
            return i->second;
        }
    }

    std::map<Uri, Participant *> m_participants;

    ChannelState m_desiredState;
    ChannelState m_currentState;
    int m_currentVolume;
    int m_desiredVolume;
    bool m_volumeRequestInProgress;

    Uri m_channelUri;
    std::string m_accessToken;
    std::string m_sessionHandle;
    IClientApiEventHandler *m_app;
    std::string m_accountHandle;
    AccountName m_accountName;
    std::string m_sessionGroupHandle;
};

class MultiChannelSessionGroup
{
public:
    MultiChannelSessionGroup(IClientApiEventHandler *app, const AccountName &accountName) :
        m_accountName(accountName),
        m_channelTransmissionPolicyRequestInProgress(false),
        m_app(app)
    {
    }

    virtual ~MultiChannelSessionGroup()
    {
        Clear();
    }

    void Clear()
    {
        m_sessionGroupHandle.clear();
        m_accountHandle.clear();
        for (std::map<Uri, Channel *>::const_iterator i = m_channels.begin(); i != m_channels.end(); ++i) {
            delete i->second;
        }
        m_channels.clear();
    }

    VCSStatus JoinChannel(const Uri &channelUri, const char *channelAccessToken, bool multiChannel)
    {
        if (!channelUri.IsValid()) {
            return VX_E_INVALID_ARGUMENT;
        }
        Channel *c = FindChannel(channelUri);
        if (c == NULL) {
            c = new Channel(m_app, channelUri, m_accountName, m_accountHandle, m_sessionGroupHandle);
            m_channels[channelUri] = c;
        }
        if (!multiChannel) {
            for (std::map<Uri, Channel *>::const_iterator i = m_channels.begin(); i != m_channels.end(); ++i) {
                if (i->second != c) {
                    i->second->Leave();
                }
            }
        }
        c->Join(channelAccessToken);
        return 0;
    }

    VCSStatus LeaveChannel(const Uri &channelUri)
    {
        if (!channelUri.IsValid()) {
            return VX_E_INVALID_ARGUMENT;
        }
        Channel *s = FindChannel(channelUri);
        if (s == NULL) {
            return 0;
        }
        s->Leave();
        return 0;
    }

    VCSStatus LeaveAll()
    {
        for (std::map<Uri, Channel *>::const_iterator i = m_channels.begin(); i != m_channels.end(); ++i) {
            i->second->SetDesiredState(Channel::ChannelStateDisconnected);
        }
        return 0;
    }

    VCSStatus StartPlayFileIntoChannels(const char *filename)
    {
        if (!filename || !filename[0]) {
            return VX_E_INVALID_ARGUMENT;
        }

        // The SDK will do this too, but it's easier to pick up the error here
        FILE *fp = NULL;
        fopen_s(&fp, filename, "r");
        if (!fp) {
            return VX_E_FILE_OPEN_FAILED;
        }
        fclose(fp);

        if (HasConnectedChannel()) {
            vx_req_sessiongroup_control_audio_injection_t *req;
            vx_req_sessiongroup_control_audio_injection_create(&req);
            req->audio_injection_control_type = VX_SESSIONGROUP_AUDIO_INJECTION_CONTROL_RESTART;
            req->sessiongroup_handle = vx_strdup(m_sessionGroupHandle.c_str());
            req->filename = vx_strdup(filename);
            issueRequest(&req->base);
        }
        return 0;
    }

    void StopPlayFileIntoChannels()
    {
        vx_req_sessiongroup_control_audio_injection_t *req;
        vx_req_sessiongroup_control_audio_injection_create(&req);
        req->audio_injection_control_type = VX_SESSIONGROUP_AUDIO_INJECTION_CONTROL_STOP;
        req->sessiongroup_handle = vx_strdup(m_sessionGroupHandle.c_str());
        issueRequest(&req->base);
    }

    int GetChannelAudioOutputDeviceVolume(const Uri &channel) const
    {
        if (!channel.IsValid()) {
            return 50;     /// default value
        }
        Channel *s = FindChannel(channel);
        if (s == NULL) {
            return 50;     /// default value
        }
        return s->GetCurrentVolume();
    }

    VCSStatus SetChannelAudioOutputDeviceVolume(const Uri &channel, int volume)
    {
        if (!channel.IsValid()) {
            return VX_E_INVALID_ARGUMENT;
        }
        Channel *s = FindChannel(channel);
        if (s == NULL) {
            return VX_E_NO_EXIST;
        }
        if (volume != s->GetDesiredVolume()) {
            s->SetDesiredVolume(volume);
            s->NextState();
        }
        return 0;
    }

    int GetParticipantAudioOutputDeviceVolumeForMe(const Uri &target, const Uri &channel)
    {
        if (!channel.IsValid()) {
            return 50;     /// default value
        }
        Channel *s = FindChannel(channel);
        if (s == NULL) {
            return 50;     /// default value
        }
        return s->GetParticipantAudioOutputDeviceVolumeForMe(target);
    }

    VCSStatus SetParticipantAudioOutputDeviceVolumeForMe(const Uri &target, const Uri &channel, int volume)
    {
        if (!channel.IsValid()) {
            return VX_E_INVALID_ARGUMENT;
        }
        Channel *s = FindChannel(channel);
        if (s == NULL) {
            return VX_E_NO_EXIST;
        }
        return s->SetParticipantAudioOutputDeviceVolumeForMe(target, volume);
    }

    VCSStatus SetParticipantMutedForAll(const Uri &target, const Uri &channel, bool muted, const char *accessToken)
    {
        if (!channel.IsValid()) {
            return VX_E_INVALID_ARGUMENT;
        }
        Channel *s = FindChannel(channel);
        if (s == NULL) {
            return VX_E_NO_EXIST;
        }

        return s->SetParticipantMutedForAll(target, muted, accessToken);
    }

    bool GetParticipantMutedForAll(const Uri &target, const Uri &channel) const
    {
        CHECK_RET1(channel.IsValid(), false);
        Channel *s = FindChannel(channel);
        if (s == NULL) {
            return false;
        }
        return s->GetParticipantMutedForAll(target);
    }

    VCSStatus SetParticipantMutedForMe(const Uri &target, const Uri &channel, bool muted)
    {
        if (!channel.IsValid()) {
            return VX_E_INVALID_ARGUMENT;
        }
        Channel *s = FindChannel(channel);
        if (s == NULL) {
            return VX_E_NO_EXIST;
        }
        return s->SetParticipantMutedForMe(target, muted);
    }

    VCSStatus MuteAll(const Uri &channel, bool set_muted, const char *accessToken)
    {
        if (!channel.IsValid()) {
            return VX_E_INVALID_ARGUMENT;
        }
        Channel *s = FindChannel(channel);
        if (s == NULL) {
            return VX_E_NO_EXIST;
        }

        return s->MuteAll(set_muted, accessToken);
    }

    VCSStatus KickUser(const Uri &channel, const Uri &userUri, const char *accessToken)
    {
        if (!channel.IsValid()) {
            return VX_E_INVALID_ARGUMENT;
        }
        Channel *s = FindChannel(channel);
        if (s == NULL) {
            return VX_E_NO_EXIST;
        }

        return s->KickUser(userUri, accessToken);
    }

    ChannelTransmissionPolicy GetCurrentChannelTransmissionPolicy() const { return m_currentChannelTransmissionPolicy; }
    ChannelTransmissionPolicy GetDesiredChannelTransmissionPolicy() const { return m_desiredChannelTransmissionPolicy; }

    VCSStatus Set3DPosition(const Uri &channel, double x, double y, double z, double at_x, double at_y, double at_z)
    {
        Channel *s = FindChannel(channel);
        if (s == NULL) {
            return VX_E_NO_EXIST;
        }

        return s->Set3DPosition(x, y, z, at_x, at_y, at_z);
    }

    VCSStatus SetTransmissionToSpecificChannel(const Uri &channel)
    {
        if (m_desiredChannelTransmissionPolicy.GetChannelTransmissionPolicy() != ChannelTransmissionPolicy::vx_channel_transmission_policy_specific_channel ||
            m_desiredChannelTransmissionPolicy.GetSpecificTransmissionChannel() != channel)
        {
            if (!channel.IsValid()) {
                return VX_E_INVALID_ARGUMENT;
            }
            Channel *s = FindChannel(channel);
            if (s == NULL) {
                return VX_E_NO_EXIST;
            }

            m_desiredChannelTransmissionPolicy.SetTransmissionToSpecificChannel(channel);
        }
        return 0;
    }

    VCSStatus SetTransmissionToAll()
    {
        if (m_desiredChannelTransmissionPolicy.GetChannelTransmissionPolicy() != ChannelTransmissionPolicy::vx_channel_transmission_policy_all) {
            m_desiredChannelTransmissionPolicy.SetTransmissionToAll();
        }
        return 0;
    }

    VCSStatus SetTransmissionToNone()
    {
        if (m_desiredChannelTransmissionPolicy.GetChannelTransmissionPolicy() != ChannelTransmissionPolicy::vx_channel_transmission_policy_none) {
            m_desiredChannelTransmissionPolicy.SetTransmissionToNone();
        }
        return 0;
    }

    VCSStatus SetSttTranscriptionOn(const Uri &channel, bool on, const char *accessToken)
    {
        Channel *s = FindChannel(channel);
        if (s == NULL) {
            return VX_E_NO_EXIST;
        }
        return s->SetSttTranscriptionOn(on, accessToken);
    }


    void NextState()
    {
        std::set<Channel *> channelsToDisconnect;
        std::set<Channel *> channelsToConnect;
        std::set<Channel *> connectedChannels;
        std::set<Channel *> channelsDisconnecting;
        bool currentlyConnectingChannel = false;

        for (std::map<Uri, Channel *>::const_iterator i = m_channels.begin(); i != m_channels.end(); ++i) {
            if (i->second->GetDesiredState() == Channel::ChannelStateDisconnected && (i->second->GetCurrentState() == Channel::ChannelStateConnected)) {
                channelsToDisconnect.insert(i->second);
                channelsDisconnecting.insert(i->second);     // this channel will be moving to the disconnecting state before the check below.
            }
            if (i->second->GetDesiredState() == Channel::ChannelStateConnected && (i->second->GetCurrentState() == Channel::ChannelStateDisconnected)) {
                channelsToConnect.insert(i->second);
            }
            if (i->second->GetDesiredState() == Channel::ChannelStateConnected && i->second->GetCurrentState() == Channel::ChannelStateConnected) {
                connectedChannels.insert(i->second);
            }
            if (i->second->GetCurrentState() == Channel::ChannelStateDisconnecting) {
                channelsDisconnecting.insert(i->second);
            }
            currentlyConnectingChannel |= i->second->GetCurrentState() == Channel::ChannelStateConnecting;
        }

        // This is tricky.
        // If we have zero channels, only add one
        // (Don't begin connecting a channel if another is already connecting)
        if (!currentlyConnectingChannel && !channelsToConnect.empty() && channelsDisconnecting.empty()) {
            (*channelsToConnect.begin())->NextState();
            return;
        }

        // Disconnect from channels before connecting to new channels
        for (std::set<Channel *>::const_iterator i = channelsToDisconnect.begin(); i != channelsToDisconnect.end(); ++i) {
            (*i)->NextState();
        }

        if (channelsDisconnecting.empty() && !connectedChannels.empty()) {
            for (std::set<Channel *>::const_iterator i = channelsToConnect.begin(); i != channelsToConnect.end(); ++i) {
                (*i)->NextState();
            }
        }

        if (!m_channelTransmissionPolicyRequestInProgress) {
            if (m_desiredChannelTransmissionPolicy.GetChannelTransmissionPolicy() != m_currentChannelTransmissionPolicy.GetChannelTransmissionPolicy()) {
                switch (m_desiredChannelTransmissionPolicy.GetChannelTransmissionPolicy()) {
                    case ChannelTransmissionPolicy::vx_channel_transmission_policy_specific_channel:
                        Channel *s;
                        s = FindChannel(m_desiredChannelTransmissionPolicy.GetSpecificTransmissionChannel());
                        if (s != NULL) {
                            s->SetTransmissionToThisChannel();
                        }
                        break;
                    case ChannelTransmissionPolicy::vx_channel_transmission_policy_all:
                        vx_req_sessiongroup_set_tx_all_sessions_t *req_all;
                        vx_req_sessiongroup_set_tx_all_sessions_create(&req_all);
                        req_all->sessiongroup_handle = vx_strdup(m_sessionGroupHandle.c_str());
                        issueRequest(&req_all->base);
                        break;
                    case ChannelTransmissionPolicy::vx_channel_transmission_policy_none:
                        vx_req_sessiongroup_set_tx_no_session_t *req_none;
                        vx_req_sessiongroup_set_tx_no_session_create(&req_none);
                        req_none->sessiongroup_handle = vx_strdup(m_sessionGroupHandle.c_str());
                        issueRequest(&req_none->base);
                        break;
                    default:
                        break;
                }
                m_channelTransmissionPolicyRequestInProgress = true;
            } else if (m_desiredChannelTransmissionPolicy.GetSpecificTransmissionChannel() != m_currentChannelTransmissionPolicy.GetSpecificTransmissionChannel()) {
                Channel *s = FindChannel(m_desiredChannelTransmissionPolicy.GetSpecificTransmissionChannel());
                if (s != NULL) {
                    m_channelTransmissionPolicyRequestInProgress = true;
                    s->SetTransmissionToThisChannel();
                }
            }
        }
    }

    const std::string &GetSessionGroupHandle() const { return m_sessionGroupHandle; }

    void HandleResponse(vx_resp_sessiongroup_add_session *resp)
    {
        vx_req_sessiongroup_add_session *req = reinterpret_cast<vx_req_sessiongroup_add_session *>(resp->base.request);
        CHECK_RET(!m_accountHandle.empty());
        Channel *c = FindChannelBySessionHandle(req->session_handle);
        CHECK_RET(c != NULL);
        if (resp->base.return_code == 1) {
            if (c->GetDesiredState() == Channel::ChannelStateConnected) {
                m_app->onChannelJoinFailed(m_accountName, c->GetUri(), resp->base.status_code);
                m_channels.erase(c->GetUri());
                delete c;
            }
        }
    }

    void HandleResponse(vx_resp_sessiongroup_remove_session *resp)
    {
        vx_req_sessiongroup_remove_session *req = reinterpret_cast<vx_req_sessiongroup_remove_session *>(resp->base.request);
        CHECK_RET(!m_accountHandle.empty());
        Channel *c = FindChannelBySessionHandle(req->session_handle);
        CHECK_RET(c != NULL);
        if (resp->base.return_code == 1) {
            if (c->GetDesiredState() == Channel::ChannelStateConnected) {
                m_app->onChannelJoinFailed(m_accountName, c->GetUri(), resp->base.status_code);
                m_channels.erase(c->GetUri());
                delete c;
            }
        }
    }

    void HandleResponse(vx_resp_session_set_local_speaker_volume *resp)
    {
        vx_req_session_set_local_speaker_volume_t *req = reinterpret_cast<vx_req_session_set_local_speaker_volume_t *>(resp->base.request);
        CHECK_RET(!m_accountHandle.empty());
        Channel *c = FindChannelBySessionHandle(req->session_handle);
        CHECK_RET(c != NULL);
        c->HandleResponse(resp);
        c->NextState();
    }

    void HandleResponse(vx_resp_session_set_local_render_volume *resp)
    {
        vx_req_session_set_local_render_volume_t *req = reinterpret_cast<vx_req_session_set_local_render_volume_t *>(resp->base.request);
        CHECK_RET(!m_accountHandle.empty());
        Channel *c = FindChannelBySessionHandle(req->session_handle);
        CHECK_RET(c != NULL);
        c->HandleResponse(resp);
        c->NextState();
    }

    void HandleResponse(vx_resp_session_set_participant_volume_for_me *resp)
    {
        vx_req_session_set_participant_volume_for_me_t *req = reinterpret_cast<vx_req_session_set_participant_volume_for_me_t *>(resp->base.request);
        CHECK_RET(!m_accountHandle.empty());
        Channel *c = FindChannelBySessionHandle(req->session_handle);
        CHECK_RET(c != NULL);
        c->HandleResponse(resp);
        c->NextState();
    }

    void HandleResponse(vx_resp_channel_mute_user *resp)
    {
        vx_req_channel_mute_user_t *req = reinterpret_cast<vx_req_channel_mute_user_t *>(resp->base.request);
        CHECK_RET(!m_accountHandle.empty());
        Channel *c = FindChannel(Uri(req->channel_uri));
        CHECK_RET(c != NULL);
        c->HandleResponse(resp);
        c->NextState();
    }

    void HandleResponse(vx_resp_channel_mute_all_users *resp)
    {
        vx_req_channel_mute_all_users_t *req = reinterpret_cast<vx_req_channel_mute_all_users_t *>(resp->base.request);
        CHECK_RET(!m_accountHandle.empty());
        Channel *c = FindChannel(Uri(req->channel_uri));
        CHECK_RET(c != NULL);
        c->HandleResponse(resp);
        c->NextState();
    }

    void HandleResponse(vx_resp_session_set_participant_mute_for_me *resp)
    {
        vx_req_session_set_participant_mute_for_me_t *req = reinterpret_cast<vx_req_session_set_participant_mute_for_me_t *>(resp->base.request);
        CHECK_RET(!m_accountHandle.empty());
        Channel *c = FindChannelBySessionHandle(req->session_handle);
        CHECK_RET(c != NULL);
        c->HandleResponse(resp);
        c->NextState();
    }

    void HandleResponse(vx_resp_sessiongroup_set_tx_session *resp)
    {
        vx_req_sessiongroup_set_tx_session_t *req = reinterpret_cast<vx_req_sessiongroup_set_tx_session_t *>(resp->base.request);
        Channel *c = FindChannelBySessionHandle(req->session_handle);
        CHECK_RET(c != NULL);
        if (resp->base.return_code != 0) {
            if (m_desiredChannelTransmissionPolicy.GetChannelTransmissionPolicy() == ChannelTransmissionPolicy::vx_channel_transmission_policy_specific_channel) {
                m_desiredChannelTransmissionPolicy.SetChannelTransmissionPolicy(m_currentChannelTransmissionPolicy.GetChannelTransmissionPolicy());
            }
            if (m_desiredChannelTransmissionPolicy.GetSpecificTransmissionChannel() == c->GetUri()) {
                m_desiredChannelTransmissionPolicy.SetSpecificTransmissionChannel(m_currentChannelTransmissionPolicy.GetSpecificTransmissionChannel());
            }
            m_app->onSetChannelTransmissionToSpecificChannelFailed(m_accountName, c->GetUri(), resp->base.status_code);
        } else {
            m_currentChannelTransmissionPolicy.SetTransmissionToSpecificChannel(c->GetUri());
            m_app->onSetChannelTransmissionToSpecificChannelCompleted(m_accountName, c->GetUri());
        }
        m_channelTransmissionPolicyRequestInProgress = false;
        NextState();
    }

    void HandleResponse(vx_resp_sessiongroup_set_tx_all_sessions *resp)
    {
        if (resp->base.return_code != 0) {
            if (m_desiredChannelTransmissionPolicy.GetChannelTransmissionPolicy() == ChannelTransmissionPolicy::vx_channel_transmission_policy_all) {
                m_desiredChannelTransmissionPolicy.SetChannelTransmissionPolicy(m_currentChannelTransmissionPolicy.GetChannelTransmissionPolicy());
            }
            m_app->onSetChannelTransmissionToAllFailed(m_accountName, resp->base.status_code);
        } else {
            m_currentChannelTransmissionPolicy.SetTransmissionToAll();
            m_app->onSetChannelTransmissionToAllCompleted(m_accountName);
        }
        m_channelTransmissionPolicyRequestInProgress = false;
        NextState();
    }

    void HandleResponse(vx_resp_sessiongroup_set_tx_no_session *resp)
    {
        if (resp->base.return_code != 0) {
            if (m_desiredChannelTransmissionPolicy.GetChannelTransmissionPolicy() == ChannelTransmissionPolicy::vx_channel_transmission_policy_none) {
                m_desiredChannelTransmissionPolicy.SetChannelTransmissionPolicy(m_currentChannelTransmissionPolicy.GetChannelTransmissionPolicy());
            }
            m_app->onSetChannelTransmissionToNoneFailed(m_accountName, resp->base.status_code);
        } else {
            m_currentChannelTransmissionPolicy.SetTransmissionToNone();
            m_app->onSetChannelTransmissionToNoneCompleted(m_accountName);
        }
        m_channelTransmissionPolicyRequestInProgress = false;
        NextState();
    }

    void HandleEvent(vx_evt_media_stream_updated *evt)
    {
        Channel *c = FindChannelBySessionHandle(evt->session_handle);
        CHECK_RET(c != NULL);

        // evt states are
        // Connecting,  nothing to do,  just a progress msg
        // connected,  user has completed the signalling,  next msg will be a participant added for himself.
        // disconnecting, call is being torn down, progress msg
        // disconnected, call is terminated either normally of because of an error

        if (evt->state == session_media_disconnected) {
            if (evt->status_code != 0) {
                // hit an error connecting to or while in a channel, do not retry to join the channel.
                if (c->GetDesiredState() == Channel::ChannelStateConnected) {
                    if (c->GetCurrentState() == Channel::ChannelStateConnected) {
                        c->SetCurrentState(Channel::ChannelStateDisconnected);
                        c->SetDesiredState(Channel::ChannelStateDisconnected);
                        m_app->onChannelExited(m_accountName, c->GetUri(), evt->status_code);
                    } else if (c->GetCurrentState() == Channel::ChannelStateConnecting) {
                        c->SetCurrentState(Channel::ChannelStateDisconnected);
                        c->SetDesiredState(Channel::ChannelStateDisconnected);
                        m_app->onChannelJoinFailed(m_accountName, c->GetUri(), evt->status_code);
                    }
                } else if (c->GetCurrentState() == Channel::ChannelStateDisconnecting) {
                    c->SetCurrentState(Channel::ChannelStateDisconnected);
                    c->SetDesiredState(Channel::ChannelStateDisconnected);
                    m_app->onChannelExited(m_accountName, c->GetUri(), evt->status_code);
                }
            } else {
                // no status_code, leaving the channel,  therefore the player must have initiated the request to leave.
                // the the channelstate should be set on the session_removed event, the player could still be connected to the text plane
                c->SetCurrentState(Channel::ChannelStateDisconnected);      // this is the wrong place if the text plane is being used.
                c->SetDesiredState(Channel::ChannelStateDisconnected);
                m_app->onChannelExited(m_accountName, c->GetUri(), evt->status_code);
            }
            if (evt->call_stats != NULL) {
                m_app->onCallStatsUpdated(m_accountName, *evt->call_stats, true);
            }
        } else if (evt->state == session_media_disconnecting) {
            // Not much of anything to do,  might consider moving the DesiredState to Disconnected
            if (evt->call_stats != NULL) {
                m_app->onCallStatsUpdated(m_accountName, *evt->call_stats, true);
            }
        }

        // if (c != NULL) c->HandleEvent(evt);
    }

    void HandleEvent(vx_evt_participant_added *evt)
    {
        Channel *c = FindChannelBySessionHandle(evt->session_handle);
        CHECK_RET(c != NULL);
        c->HandleEvent(evt);
    }

    void HandleEvent(vx_evt_participant_updated *evt)
    {
        Channel *c = FindChannelBySessionHandle(evt->session_handle);
        CHECK_RET(c != NULL);
        c->HandleEvent(evt);
    }

    void HandleEvent(vx_evt_participant_removed *evt)
    {
        Channel *c = FindChannelBySessionHandle(evt->session_handle);
        CHECK_RET(c != NULL);
        c->HandleEvent(evt);
    }

    void HandleEvent(vx_evt_sessiongroup_removed *evt)
    {
        CHECK_RET(m_sessionGroupHandle == evt->sessiongroup_handle);
        std::string removedSessionGroupHandle = m_sessionGroupHandle;

        // Notify the app and do nothing more here
        m_app->onSessionGroupRemoved(m_accountName, removedSessionGroupHandle.c_str());
    }

    void HandleEvent(vx_evt_transcribed_message *evt)
    {
        Channel *c = FindChannelBySessionHandle(evt->session_handle);
        CHECK_RET(c != NULL);
        c->HandleEvent(evt);
    }

    int IssueGetStats(bool reset)
    {
        if (!GetSessionGroupHandle().empty()) {
            vx_req_sessiongroup_get_stats *req;
            vx_req_sessiongroup_get_stats_create(&req);
            req->sessiongroup_handle = vx_strdup(GetSessionGroupHandle().c_str());
            req->reset_stats = reset ? 1 : 0;
            return issueRequest(&req->base);
        }
        return 0;
    }

    bool IsUsingSessionHandle(const char *handle) const
    {
        return FindChannelBySessionHandle(handle) != NULL;
    }

    void SetAccountHandle(const std::string &accountHandle)
    {
        // Store new m_accountHandle when SingleLoginMultiChannelManager goes to login
        // (it was not filled in when MutltiChannelSessionGroup was constructed)
        CHECK_RET(!accountHandle.empty());
        m_accountHandle = accountHandle;

        // And propagate it to underlying objects created before
        for (std::map<Uri, Channel *>::const_iterator i = m_channels.begin(); i != m_channels.end(); ++i) {
            i->second->SetAccountHandle(m_accountHandle);
        }

        // Create new m_sessionGroupHandle at this moment too
        if (m_sessionGroupHandle.empty()) {
            char *cookie = GetNextRequestId(NULL, "G");
            m_sessionGroupHandle = cookie;
            vx_free(cookie);

            // And propagate it to underlying objects
            for (std::map<Uri, Channel *>::const_iterator i = m_channels.begin(); i != m_channels.end(); ++i) {
                i->second->SetSessionGroupHandle(m_sessionGroupHandle);
            }
        }
    }

private:
    Channel *FindChannelBySessionHandle(const char *handle) const
    {
        CHECK_RET1(handle != NULL, NULL);
        CHECK_RET1(handle[0] != 0, NULL);
        for (std::map<Uri, Channel *>::const_iterator i = m_channels.begin(); i != m_channels.end(); ++i) {
            if (i->second->GetSessionHandle() == handle) {
                return i->second;
            }
        }
        return NULL;
    }

    bool HasConnectedChannel() const
    {
        for (std::map<Uri, Channel *>::const_iterator i = m_channels.begin(); i != m_channels.end(); ++i) {
            if (i->second->GetDesiredState() == Channel::ChannelStateConnected && i->second->GetCurrentState() == Channel::ChannelStateConnected) {
                return true;
            }
        }
        return false;
    }

    Channel *FindChannel(const Uri &channelUri) const
    {
        std::map<Uri, Channel *>::const_iterator i = m_channels.find(channelUri);
        if (i == m_channels.end()) {
            return NULL;
        }
        return i->second;
    }

    std::string m_sessionGroupHandle;
    std::string m_accountHandle;
    AccountName m_accountName;
    ChannelTransmissionPolicy m_currentChannelTransmissionPolicy;
    ChannelTransmissionPolicy m_desiredChannelTransmissionPolicy;
    bool m_channelTransmissionPolicyRequestInProgress;

    std::map<Uri, Channel *> m_channels;
    IClientApiEventHandler *m_app;
};

class UserBlockPolicy
{
public:
    UserBlockPolicy()
    {
        m_currentBlocked = false;
        m_desiredBlocked = false;
    }
    UserBlockPolicy(const Uri &uri)
    {
        m_uri = uri;
        m_currentBlocked = false;
        m_desiredBlocked = false;
    }
    bool GetCurrentBlock() const
    {
        return m_currentBlocked;
    }
    void SetCurrentBlock(bool value)
    {
        m_currentBlocked = value;
    }
    bool GetDesiredBlock() const
    {
        return m_desiredBlocked;
    }
    void SetDesiredBlock(bool value)
    {
        m_desiredBlocked = value;
    }

private:
    Uri m_uri;
    bool m_currentBlocked;
    bool m_desiredBlocked;
};

class SingleLoginMultiChannelManager
{
public:
    typedef enum {
        LoginStateLoggedOut,
        LoginStateLoggingIn,
        LoginStateLoggedIn,
        LoginStateLoggingOut
    } LoginState;

    SingleLoginMultiChannelManager(
            IClientApiEventHandler *app,
            const std::string &connectorHandle,
            const AccountName &name,
            bool multichannel,
            int participantUpdateFrequency = -1
            ) :
        m_app(app),
        m_sg(app, name)
    {
        CHECK(!connectorHandle.empty());
        // CHECK(name.IsValid());
        m_name = name;
        m_connectorHandle = connectorHandle;
        m_currentLoginState = LoginStateLoggedOut;
        m_desiredLoginState = LoginStateLoggedOut;
        m_participantUpdateFrequency = participantUpdateFrequency;
        m_multichannel = multichannel;
    }

    ~SingleLoginMultiChannelManager()
    {
        for (std::map<Uri, UserBlockPolicy *>::const_iterator i = m_userBlockPolicy.begin(); i != m_userBlockPolicy.end(); ++i) {
            delete i->second;
        }
    }

    VCSStatus Login(const char *accessToken)
    {
        m_desiredLoginState = LoginStateLoggedIn;
        m_desiredCredentials = safe_str(accessToken);
        return 0;
    }

    VCSStatus NextState(VCSStatus status)
    {
        NextState();
        return status;
    }

    void NextState()
    {
        if (m_currentLoginState == LoginStateLoggedOut && m_desiredLoginState == LoginStateLoggedIn) {
            SetAccountHandle();     // create new m_accountHandle and set it to underlying objects
            vx_req_account_anonymous_login_t *req;
            vx_req_account_anonymous_login_create(&req);
            req->connector_handle = VX_HANDLE(vx_strdup(m_connectorHandle.c_str()));
            req->base.cookie = GetNextRequestId(NULL, "A");
            req->account_handle = VX_HANDLE(vx_strdup(m_accountHandle.c_str()));
            req->participant_property_frequency = (m_participantUpdateFrequency >= 0) ? m_participantUpdateFrequency : 100;
            req->enable_buddies_and_presence = 0;
            req->acct_name = vx_strdup(m_name.ToString());
            req->access_token = vx_strdup(m_desiredCredentials.c_str());
            m_currentLoginState = LoginStateLoggingIn;
            m_currentCredentials = m_desiredCredentials;
            issueRequest(&req->base);
        } else if ((m_currentLoginState == LoginStateLoggedIn || m_currentLoginState == LoginStateLoggingIn) && m_desiredLoginState == LoginStateLoggedOut) {
            vx_req_account_logout_t *req;
            vx_req_account_logout_create(&req);
            req->account_handle = vx_strdup(m_accountHandle.c_str());
            m_currentLoginState = LoginStateLoggingOut;
            issueRequest(&req->base);
        }
        if (m_desiredLoginState == LoginStateLoggedIn && m_currentLoginState == LoginStateLoggedIn) {
            std::stringstream blocked;
            std::stringstream unblocked;
            const char *blockSep = "";
            const char *unblockSep = "";
            for (std::map<Uri, UserBlockPolicy *>::const_iterator i = m_userBlockPolicy.begin(); i != m_userBlockPolicy.end(); ++i) {
                if (i->second->GetCurrentBlock() && !i->second->GetDesiredBlock()) {
                    unblocked << unblockSep << i->first.ToString();
                    unblockSep = "\n";
                } else if (!i->second->GetCurrentBlock() && i->second->GetDesiredBlock()) {
                    blocked << blockSep << i->first.ToString();
                    blockSep = "\n";
                }
                i->second->SetCurrentBlock(i->second->GetDesiredBlock());
            }
            std::string blockedStr = blocked.str();
            std::string unblockedStr = unblocked.str();
            if (!blockedStr.empty()) {
                vx_req_account_control_communications_t *req;
                vx_req_account_control_communications_create(&req);
                req->account_handle = vx_strdup(m_accountHandle.c_str());
                req->user_uris = vx_strdup(blockedStr.c_str());
                req->operation = vx_control_communications_operation_block;
                issueRequest(&req->base);
            }

            if (!unblockedStr.empty()) {
                vx_req_account_control_communications_t *req;
                vx_req_account_control_communications_create(&req);
                req->account_handle = vx_strdup(m_accountHandle.c_str());
                req->user_uris = vx_strdup(unblockedStr.c_str());
                req->operation = vx_control_communications_operation_unblock;
                issueRequest(&req->base);
            }
            m_sg.NextState();
        }
    }

    void Logout()
    {
        if (m_desiredLoginState != LoginStateLoggedOut) {
            m_desiredLoginState = LoginStateLoggedOut;
        }
    }

    VCSStatus JoinChannel(const Uri &channelUri, const char *channelAccessToken)
    {
        int status = m_sg.JoinChannel(channelUri, channelAccessToken, m_multichannel);
        return status;
    }

    VCSStatus LeaveChannel(const Uri &channelUri)
    {
        int status = m_sg.LeaveChannel(channelUri);
        return status;
    }

    VCSStatus LeaveAll()
    {
        return m_sg.LeaveAll();
    }

    VCSStatus BlockUsers(const std::set<Uri> &usersToBlock)
    {
        for (std::set<Uri>::const_iterator i = usersToBlock.begin(); i != usersToBlock.end(); ++i) {
            std::map<Uri, UserBlockPolicy *>::const_iterator k = m_userBlockPolicy.find(*i);
            UserBlockPolicy *ubp;
            if (k == m_userBlockPolicy.end()) {
                ubp = new UserBlockPolicy(*i);
                m_userBlockPolicy[*i] = ubp;
            } else {
                ubp = k->second;
            }
            ubp->SetDesiredBlock(true);
        }
        return 0;
    }

    VCSStatus UnblockUsers(const std::set<Uri> &usersToUnblock)
    {
        for (std::set<Uri>::const_iterator i = usersToUnblock.begin(); i != usersToUnblock.end(); ++i) {
            std::map<Uri, UserBlockPolicy *>::const_iterator k = m_userBlockPolicy.find(*i);
            if (k == m_userBlockPolicy.end()) {
                continue;
            }
            k->second->SetDesiredBlock(false);
        }
        return 0;
    }

    VCSStatus CheckBlockedUser(const Uri &user)
    {
        return (m_actualBlockedPolicy.find(user) == m_actualBlockedPolicy.end()) ? 0 : 1;
    }

    VCSStatus IssueGetStats(bool reset)
    {
        return m_sg.IssueGetStats(reset);
    }

    VCSStatus StartPlayFileIntoChannels(const char *filename)
    {
        VCSStatus status = m_sg.StartPlayFileIntoChannels(filename);
        return status;
    }

    void StopPlayFileIntoChannels()
    {
        m_sg.StopPlayFileIntoChannels();
    }

    VCSStatus KickUser(const Uri &channel, const Uri &userUri, const char *accessToken)
    {
        return m_sg.KickUser(channel, userUri, accessToken);
    }

    VCSStatus MuteAll(const Uri &channel, bool set_muted, const char *accessToken)
    {
        return m_sg.MuteAll(channel, set_muted, accessToken);
    }

    int GetChannelAudioOutputDeviceVolume(const Uri &channel) const
    {
        return m_sg.GetChannelAudioOutputDeviceVolume(channel);
    }

    VCSStatus SetChannelAudioOutputDeviceVolume(const Uri &channel, int volume)
    {
        return m_sg.SetChannelAudioOutputDeviceVolume(channel, volume);
    }

    int GetParticipantAudioOutputDeviceVolumeForMe(const Uri &target, const Uri &channel)
    {
        return m_sg.GetParticipantAudioOutputDeviceVolumeForMe(target, channel);
    }

    VCSStatus SetParticipantAudioOutputDeviceVolumeForMe(const Uri &target, const Uri &channel, int volume)
    {
        return m_sg.SetParticipantAudioOutputDeviceVolumeForMe(target, channel, volume);
    }

    VCSStatus SetParticipantMutedForAll(const Uri &target, const Uri &channel, bool muted, const char *accessToken)
    {
        return m_sg.SetParticipantMutedForAll(target, channel, muted, accessToken);
    }

    bool GetParticipantMutedForAll(const Uri &target, const Uri &channel) const
    {
        return m_sg.GetParticipantMutedForAll(target, channel);
    }

    VCSStatus SetParticipantMutedForMe(const Uri &target, const Uri &channel, bool muted)
    {
        return m_sg.SetParticipantMutedForMe(target, channel, muted);
    }

    ChannelTransmissionPolicy GetChannelTransmissionPolicy() const
    {
        return m_sg.GetCurrentChannelTransmissionPolicy();
    }

    VCSStatus Set3DPosition(const Uri &channel, double x, double y, double z, double at_x, double at_y, double at_z)
    {
        return NextState(m_sg.Set3DPosition(channel, x, y, z, at_x, at_y, at_z));
    }

    VCSStatus SetTransmissionToSpecificChannel(const Uri &channel)
    {
        return NextState(m_sg.SetTransmissionToSpecificChannel(channel));
    }

    VCSStatus SetTransmissionToAll()
    {
        return NextState(m_sg.SetTransmissionToAll());
    }

    VCSStatus SetTransmissionToNone()
    {
        return NextState(m_sg.SetTransmissionToNone());
    }

    VCSStatus SetSttTranscriptionOn(const Uri &channel, bool on, const char *accessToken)
    {
        return NextState(m_sg.SetSttTranscriptionOn(channel, on, accessToken));
    }

    void HandleResponse(vx_resp_sessiongroup_add_session *resp)
    {
        return m_sg.HandleResponse(resp);
    }

    void HandleResponse(vx_resp_sessiongroup_remove_session *resp)
    {
        return m_sg.HandleResponse(resp);
    }

    void HandleResponse(vx_resp_sessiongroup_control_audio_injection_t *resp)
    {
        vx_req_sessiongroup_control_audio_injection_t *req = (vx_req_sessiongroup_control_audio_injection_t *)resp->base.request;
        if (resp->base.return_code != 0) {
            m_app->onStartPlayFileIntoChannelsFailed(m_name, req->filename, resp->base.status_code);
        } else {
            m_app->onStartPlayFileIntoChannels(m_name, req->filename);
        }
    }

    void HandleResponse(vx_resp_account_control_communications_t *resp)
    {
        if (resp->base.return_code == 0) {
            vx_req_account_control_communications_t *req = reinterpret_cast<vx_req_account_control_communications_t *>(resp->base.request);
            if (req->operation == vx_control_communications_operation_block) {
                std::vector<std::string> blocked = split(req->user_uris);
                for (std::vector<std::string>::const_iterator i = blocked.begin(); i != blocked.end(); ++i) {
                    m_actualBlockedPolicy.insert(Uri(i->c_str()));
                }
            } else if (req->operation == vx_control_communications_operation_unblock) {
                std::vector<std::string> blocked = split(req->user_uris);
                for (std::vector<std::string>::const_iterator i = blocked.begin(); i != blocked.end(); ++i) {
                    m_actualBlockedPolicy.erase(Uri(i->c_str()));
                }
            } else if (req->operation == vx_control_communications_operation_clear) {
                m_actualBlockedPolicy.clear();
            }
        }
        NextState();
    }

    void HandleResponse(vx_resp_account_anonymous_login_t *resp)
    {
        vx_req_account_anonymous_login *req = reinterpret_cast<vx_req_account_anonymous_login *>(resp->base.request);
        CHECK_RET(req->account_handle == m_accountHandle);
        CHECK_RET(m_currentLoginState == LoginStateLoggingIn || m_currentLoginState == LoginStateLoggingOut);
        if (m_desiredLoginState == LoginStateLoggedIn) {
            if (resp->base.return_code == 1) {
                m_currentLoginState = LoginStateLoggedOut;
                m_desiredLoginState = LoginStateLoggedOut;
                m_app->onLoginFailed(m_name, resp->base.status_code);
            } else {
                m_currentLoginState = m_desiredLoginState;
                m_app->onLoginCompleted(m_name);
                m_app->onLoginCompletedEx(m_name, m_accountHandle.c_str());
            }
        }
        NextState();
    }

    void HandleResponse(vx_resp_account_logout_t *resp)
    {
        vx_req_account_logout *req = reinterpret_cast<vx_req_account_logout *>(resp->base.request);
        CHECK_RET(req->account_handle == m_accountHandle);
        CHECK_RET(m_currentLoginState == LoginStateLoggingOut);
        if (m_desiredLoginState == LoginStateLoggedOut) {
            if (resp->base.return_code == 1) {
                m_currentLoginState = LoginStateLoggedIn;
                m_desiredLoginState = LoginStateLoggedIn;
                m_app->onLogoutFailed(m_name, resp->base.status_code);
            } else {
                m_currentLoginState = m_desiredLoginState;
                m_sg.Clear();
                m_app->onLogoutCompleted(m_name);
            }
        }
        NextState();
    }

    void HandleResponse(vx_resp_channel_kick_user_t *resp)
    {
        /// Check the return_code of the response to check for errors
        vx_req_channel_kick_user_t *req = (vx_req_channel_kick_user_t *)resp->base.request;
        if (resp->base.return_code != 0) {
            m_app->onParticipantKickFailed(m_name, Uri(req->channel_uri), Uri(req->participant_uri), resp->base.status_code);
        } else {
            m_app->onParticipantKickedCompleted(m_name, Uri(req->channel_uri), Uri(req->participant_uri));
        }
    }

    void HandleResponse(vx_resp_session_set_local_speaker_volume *resp)
    {
        return m_sg.HandleResponse(resp);
    }

    void HandleResponse(vx_resp_session_set_local_render_volume *resp)
    {
        return m_sg.HandleResponse(resp);
    }

    void HandleResponse(vx_resp_session_set_participant_volume_for_me *resp)
    {
        return m_sg.HandleResponse(resp);
    }

    void HandleResponse(vx_resp_channel_mute_user *resp)
    {
        return m_sg.HandleResponse(resp);
    }

    void HandleResponse(vx_resp_channel_mute_all_users *resp)
    {
        return m_sg.HandleResponse(resp);
    }

    void HandleResponse(vx_resp_session_set_participant_mute_for_me *resp)
    {
        return m_sg.HandleResponse(resp);
    }

    void HandleResponse(vx_resp_sessiongroup_set_tx_session *resp)
    {
        return m_sg.HandleResponse(resp);
    }

    void HandleResponse(vx_resp_sessiongroup_set_tx_all_sessions *resp)
    {
        return m_sg.HandleResponse(resp);
    }

    void HandleResponse(vx_resp_sessiongroup_set_tx_no_session *resp)
    {
        return m_sg.HandleResponse(resp);
    }

    void HandleEvent(vx_evt_account_login_state_change_t *evt)
    {
        // This event can happen in v5 when the network goes down.
        // Do not automatically try to re-login unless its a DNS or network error,  alert the application level that a logout has happened.
        CHECK_RET(evt->account_handle == m_accountHandle);
        if (evt->state == login_state_logged_out) {
            if (m_currentLoginState == LoginStateLoggedIn) {
                m_currentLoginState = LoginStateLoggedOut;
                m_desiredLoginState = LoginStateLoggedOut;
                m_app->onLogoutCompleted(m_name);
            } else if (m_currentLoginState != LoginStateLoggedIn && (evt->status_code == VxNetworkNameResolutionFailed || evt->status_code == VxNetworkUnableToConnectToServer)) {
                // experiencing a DNS lookup failure or a network data error when the player was not logged in.  Retry
                m_currentLoginState = LoginStateLoggedOut;
                m_desiredLoginState = LoginStateLoggedIn;
            }
        }
        NextState();
    }

    void HandleEvent(vx_evt_media_stream_updated *evt)
    {
        m_sg.HandleEvent(evt);
        NextState();
    }

    void HandleEvent(vx_evt_participant_added *evt)
    {
        m_sg.HandleEvent(evt);
        NextState();
    }
    void HandleEvent(vx_evt_participant_updated *evt)
    {
        m_sg.HandleEvent(evt);
        NextState();
    }
    void HandleEvent(vx_evt_participant_removed *evt)
    {
        m_sg.HandleEvent(evt);
        NextState();
    }
    void HandleEvent(vx_evt_media_completion *evt)
    {
        if (evt->completion_type == sessiongroup_audio_injection) {
            m_app->onPlayFileIntoChannelsStopped(m_name, m_playingFile.c_str());
            NextState();
        }
    }

    void HandleEvent(vx_evt_sessiongroup_removed *evt)
    {
        NextState();
        m_sg.HandleEvent(evt);
    }

    void HandleEvent(vx_evt_transcribed_message *evt)
    {
        m_sg.HandleEvent(evt);
    }

    const std::string &GetAccountHandle() const { return m_accountHandle; }
    const AccountName &GetName() const { return m_name; }
    const std::string &GetSessionGroupHandle() const { return m_sg.GetSessionGroupHandle(); }
    bool IsUsingSessionHandle(const char *handle) const { return m_sg.IsUsingSessionHandle(handle); }

private:
    void SetAccountHandle()
    {
        // create new m_accountHandle and propagate it to underlying objects
        char *cookie = GetNextRequestId(NULL, "A");
        m_accountHandle = cookie;
        vx_free(cookie);
        m_sg.SetAccountHandle(m_accountHandle);
    }

private:
    AccountName m_name;
    std::string m_accountHandle;
    std::string m_connectorHandle;

    LoginState m_desiredLoginState;
    std::string m_desiredCredentials;

    LoginState m_currentLoginState;
    std::string m_currentCredentials;
    std::string m_playingFile;
    IClientApiEventHandler *m_app;

    MultiChannelSessionGroup m_sg;

    std::map<Uri, UserBlockPolicy *> m_userBlockPolicy;
    std::set<Uri> m_actualBlockedPolicy;

    int m_participantUpdateFrequency;

    bool m_multichannel;
};

class ClientConnectionImpl
{
public:
    typedef enum {
        ConnectorStateUninitialized,
        ConnectorStateInitializing,
        ConnectorStateInitialized,
        ConnectorStateUninitializing
    } ConnectorState;

    ClientConnectionImpl()
    {
        ResetVariables();
    }

    virtual ~ClientConnectionImpl()
    {
        Uninitialize();
        ClearLoginsMap();
    }

    VCSStatus Initialize(
            IClientApiEventHandler *app,
            IClientApiEventHandler::LogLevel logLevel,
            bool multiChannel,
            bool multiLogin,
            bool overrideAllocators,
            unsigned int codecMask,
            int &inputBuffers,
            int &outputBuffers,
            bool forceCaptureSilence
            )
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        if (app == NULL) {
            return VX_E_INVALID_ARGUMENT;
        }
        if (m_app != NULL) {
            return VX_E_ALREADY_INITIALIZED;
        }

        m_multiChannel = multiChannel;
        m_multiLogin = multiLogin;

        vx_sdk_config_t config;
        int retval = vx_get_default_config3(&config, sizeof(config));
        if (retval != 0) {
            return retval;
        }

        m_loglevel = logLevel;
        config.callback_handle = this;
        config.pf_sdk_message_callback = &sOnResponseOrEventFromSdk;
        config.pf_logging_callback = &sOnLogMessageFromSdk;
        if (IClientApiEventHandler::LogLevelNone <= logLevel && logLevel <= IClientApiEventHandler::LogLevelAll) {
            config.initial_log_level = (vx_log_level)logLevel;
        } else {
#ifdef _DEBUG
            config.initial_log_level = log_warning;
#else // if !_DEBUG
            config.initial_log_level = log_error;
#endif // _DEBUG
        }

        config.allow_shared_capture_devices = 1;

#ifdef VX_TEST_MEM_ALLOCATORS
        if (overrideAllocators) {
            config.pf_calloc_func = pf_calloc_func;
            config.pf_free_aligned_func = pf_free_aligned_func;
            config.pf_free_func = pf_free_func;
            config.pf_malloc_aligned_func = pf_malloc_aligned_func;
            config.pf_malloc_func = pf_malloc_func;
            config.pf_realloc_func = pf_realloc_func;
        }
#else
        (void)overrideAllocators;
#endif

        config.pf_on_audio_unit_started = &f_on_audio_unit_started;
        config.pf_on_audio_unit_stopped = &f_on_audio_unit_stopped;
        config.pf_on_audio_unit_after_capture_audio_read = &f_on_audio_unit_after_capture_audio_read;
        config.pf_on_audio_unit_before_capture_audio_sent = &f_on_audio_unit_before_capture_audio_sent;
        config.pf_on_audio_unit_before_recv_audio_rendered = &f_on_audio_unit_before_recv_audio_rendered;

#if VIVOX_SDK_HAS_ADVANCED_AUDIO_LEVELS
        config.enable_advanced_auto_levels = 1;
#endif

        config.force_capture_silence = forceCaptureSilence ? 1 : 0;

        if (inputBuffers > 0) {
            config.capture_device_buffer_size_intervals = inputBuffers;
        }

        if (outputBuffers > 0) {
            config.render_device_buffer_size_intervals = outputBuffers;
        }

        inputBuffers = config.capture_device_buffer_size_intervals;
        outputBuffers = config.render_device_buffer_size_intervals;

        // zero means use the default
        if (codecMask != 0) {
            m_codecMask = codecMask;
        }
#ifdef VX_HAS_UDP_CALLBACKS
        vx_test_set_udp_frame_callbacks(&config);
#endif

        m_app = app;
        retval = vx_initialize3(&config, sizeof(config));
        if (retval != 0) {
            m_app = NULL;
            return retval;
        }

        /// Load local cache of audio input and output device member variables
        RequestAudioInputDevices();
        RequestAudioOutputDevices();

        while (!m_audioInputDeviceListPopulated ||
               !m_audioOutputDeviceListPopulated)
        {
            OnResponseOrEventFromSdkUiThread();
            Sleep(100);
        }

        return 0;
    }

    void Uninitialize()
    {
        if (m_app != NULL) {
            if (m_currentState == ConnectorStateInitialized || m_currentState == ConnectorStateInitializing) {
                Disconnect(m_currentServer);
            }
            while (m_currentState == ConnectorStateUninitializing) {
                // wait for the the response
                WaitForShutdownResponse();
                sleepMicroseconds(30000);
            }
            vx_uninitialize();
            m_app = NULL;
        }
        ResetVariables();
    }

    VCSStatus StartAudioOutputDeviceTest(const char *filename)
    {
        CHECK_RET1(filename && filename[0], VX_E_INVALID_ARGUMENT);
        FILE *fp = NULL;
        fopen_s(&fp, filename, "r");
        CHECK_RET1(fp, VX_E_FILE_OPEN_FAILED);
        fclose(fp);
        vx_req_aux_render_audio_start_t *req;
        vx_req_aux_render_audio_start_create(&req);
        req->sound_file_path = vx_strdup(filename);
        req->loop = 1;
        issueRequest(&req->base);
        m_audioOutputDeviceTestIsRunning = true;
        return 0;
    }

    void StopAudioOutputDeviceTest()
    {
        if (m_audioOutputDeviceTestIsRunning) {
            vx_req_aux_render_audio_stop_t *req;
            vx_req_aux_render_audio_stop_create(&req);
            issueRequest(&req->base);
            m_audioOutputDeviceTestIsRunning = false;
        }
    }

    bool AudioOutputDeviceTestIsRunning() const
    {
        return m_audioOutputDeviceTestIsRunning;
    }

    VCSStatus StartAudioInputDeviceTestRecord()
    {
        CHECK_RET1(m_audioOutputDeviceTestIsRunning == false, VX_E_FAILED);
        CHECK_RET1(m_audioInputDeviceTestIsPlayingBack == false, VX_E_FAILED);
        CHECK_RET1(m_audioInputDeviceTestIsRecording == false, VX_E_FAILED);
        vx_req_aux_start_buffer_capture *req;
        vx_req_aux_start_buffer_capture_create(&req);
        issueRequest(&req->base);
        m_audioInputDeviceTestIsRecording = true;
        return 0;
    }

    void StopAudioInputDeviceTestRecord()
    {
        if (m_audioInputDeviceTestIsRecording) {
            vx_req_aux_capture_audio_stop_t *req;
            vx_req_aux_capture_audio_stop_create(&req);
            issueRequest(&req->base);
            m_audioInputDeviceTestIsRecording = false;
            m_audioInputDeviceTestHasAudioToPlayback = true;
        }
    }

    VCSStatus StartAudioInputDeviceTestPlayback()
    {
        CHECK_RET1(m_audioOutputDeviceTestIsRunning == false, VX_E_FAILED);
        CHECK_RET1(m_audioInputDeviceTestIsPlayingBack == false, VX_E_FAILED);
        CHECK_RET1(m_audioInputDeviceTestIsRecording == false, VX_E_FAILED);
        vx_req_aux_play_audio_buffer_t *req;
        vx_req_aux_play_audio_buffer_create(&req);
        issueRequest(&req->base);
        m_audioInputDeviceTestIsPlayingBack = true;
        return 0;
    }

    void StopAudioInputDeviceTestPlayback()
    {
        if (m_audioInputDeviceTestIsPlayingBack) {
            vx_req_aux_render_audio_stop_t *req;
            vx_req_aux_render_audio_stop_create(&req);
            issueRequest(&req->base);
            m_audioInputDeviceTestIsPlayingBack = false;
        }
    }

    bool AudioInputDeviceTestIsRecording() const
    {
        return m_audioInputDeviceTestIsRecording;
    }
    bool AudioInputDeviceTestIsPlayingBack() const
    {
        return m_audioInputDeviceTestIsPlayingBack;
    }
    bool AudioInputDeviceTestHasAudioToPlayback() const
    {
        return m_audioInputDeviceTestHasAudioToPlayback;
    }
    VCSStatus Connect(const Uri &server, unsigned short minUdpPort, unsigned short maxUdpPort)
    {
        CHECK_RET1(server.IsValid(), VX_E_INVALID_ARGUMENT);

        m_desiredServer = server;
        m_desiredMinUdpPort = minUdpPort;
        m_desiredMaxUdpPort = maxUdpPort;
        m_desiredState = ConnectorStateInitialized;
        NextState();
        return 0;
    }

    void Disconnect(const Uri &server)
    {
        if (m_desiredState != ConnectorStateUninitialized) {
            std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
            CHECK_RET(m_desiredServer == server);
            // the act of disconnecting should clear all login information
            ClearLoginsMap();
            m_desiredServer.Clear();
            m_desiredMinUdpPort = m_desiredMaxUdpPort = 0;
            m_desiredState = ConnectorStateUninitialized;
            NextState();
        }
    }

    VCSStatus Login(const AccountName &accountName, const char *accessToken, const char *captureDevice, const char *renderDevice, int participantUpdateFrequency)
    {
        (void)captureDevice;
        (void)renderDevice;
        CHECK_RET1(accountName.IsValid(), VX_E_INVALID_ARGUMENT);
        CHECK_RET1(m_desiredServer.IsValid(), VX_E_FAILED);

        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> s = FindLogin(accountName);
        if (!s) {
            m_logins[accountName] = s = std::make_shared<SingleLoginMultiChannelManager>(
                    m_app,
                    m_connectorHandle,
                    accountName,
                    m_multiChannel,
                    participantUpdateFrequency);
        }

        if (m_multiLogin == false) {
            // logout everyone else
            for (std::map<AccountName, std::shared_ptr<SingleLoginMultiChannelManager> >::const_iterator i = m_logins.begin(); i != m_logins.end(); ++i) {
                if (i->second != s) {
                    i->second->Logout();
                }
            }
        }

        VCSStatus status = s->Login(accessToken);
        CHECK_RET1(status == 0, status);
        NextState();
        return 0;
    }

    VCSStatus Logout(const AccountName &accountName)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> s = FindLogin(accountName);
        if (s == NULL) {
            return 0;
        }
        s->Logout();
        NextState();
        return 0;
    }

    VCSStatus JoinChannel(const AccountName &accountName, const Uri &channelUri, const char *channelAccessToken)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> s = FindLogin(accountName);
        if (s) {
            return NextState(s->JoinChannel(channelUri, channelAccessToken));
        }
        return VX_E_NO_EXIST;
    }

    VCSStatus LeaveChannel(const AccountName &accountName, const Uri &channelUri)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> s = FindLogin(accountName);
        if (s) {
            return NextState(s->LeaveChannel(channelUri));
        }
        return VX_E_NO_EXIST;
    }

    VCSStatus LeaveAll(const AccountName &accountName)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> s = FindLogin(accountName);
        if (s) {
            return NextState(s->LeaveAll());
        }
        return VX_E_NO_EXIST;
    }

    VCSStatus BlockUsers(const AccountName &accountName, const std::set<Uri> &usersToBlock)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> s = FindLogin(accountName);
        if (s) {
            return NextState(s->BlockUsers(usersToBlock));
        }
        return VX_E_NO_EXIST;
    }

    VCSStatus UnblockUsers(const AccountName &accountName, const std::set<Uri> &usersToUnblock)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> s = FindLogin(accountName);
        if (s) {
            return NextState(s->UnblockUsers(usersToUnblock));
        }
        return VX_E_NO_EXIST;
    }

    VCSStatus CheckBlockedUser(const AccountName &accountName, const Uri &user)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> s = FindLogin(accountName);
        if (s) {
            return s->CheckBlockedUser(user);
        }
        return VX_E_NO_EXIST;
    }

    clock_t GetClock()
    {
        return m_clock;
    }

    void SetCodecMask(unsigned int codecsMask)
    {
        m_codecMask = codecsMask;
    }

    int GetCodecMask() const
    {
        return m_codecMask;
    }

    VCSStatus IssueGetStats(const AccountName &accountName, bool reset)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> s = FindLogin(accountName);
        if (s) {
            return s->IssueGetStats(reset);
        }
        return VX_E_NO_EXIST;
    }

    VCSStatus StartPlayFileIntoChannels(const AccountName &accountName, const char *filename)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> s = FindLogin(accountName);
        if (s) {
            return NextState(s->StartPlayFileIntoChannels(filename));
        }
        return VX_E_NO_EXIST;
    }

    VCSStatus StopPlayFileIntoChannels(const AccountName &accountName)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> s = FindLogin(accountName);
        if (s) {
            s->StopPlayFileIntoChannels();
            NextState();
            return 0;
        }
        return VX_E_NO_EXIST;
    }

    VCSStatus KickUser(const AccountName &accountName, const Uri &channelUri, const Uri &userUri, const char *accessToken)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> s = FindLogin(accountName);
        if (s) {
            return NextState(s->KickUser(channelUri, userUri, accessToken));
        }
        return VX_E_NO_EXIST;
    }

    VCSStatus MuteAll(const AccountName &accountName, const Uri &channelUri, bool set_muted,  const char *accessToken)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> s = FindLogin(accountName);
        if (s) {
            return NextState(s->MuteAll(channelUri, set_muted, accessToken));
        }
        return VX_E_NO_EXIST;
    }

    void RequestAudioInputDevices()
    {
        vx_req_aux_get_capture_devices_t *capture_req;
        vx_req_aux_get_capture_devices_create(&capture_req);
        issueRequest(&capture_req->base);
    }

    void RequestAudioOutputDevices()
    {
        vx_req_aux_get_render_devices_t *render_req;
        vx_req_aux_get_render_devices_create(&render_req);
        issueRequest(&render_req->base);
    }

    const std::vector<AudioDeviceId> &GetAudioInputDevices() const
    {
        return m_audioInputDeviceList;
    }

    AudioDeviceId GetAudioInputDevice() const
    {
        bool defaultSystem =
            AudioDevicePolicy::vx_audio_device_policy_default_system ==
            m_currentAudioInputDevicePolicy.GetAudioDevicePolicy();
        bool defaultCommunication =
            AudioDevicePolicy::vx_audio_device_policy_default_communication ==
            m_currentAudioInputDevicePolicy.GetAudioDevicePolicy();
        if (defaultSystem || defaultCommunication) {
            return AudioDeviceId();
        } else {
            return m_currentAudioInputDevicePolicy.GetSpecificAudioDevice();
        }
    }

    const AudioDeviceId &GetDefaultSystemAudioInputDevice() const
    {
        return m_defaultSystemAudioInputDevice;
    }

    const AudioDeviceId &GetDefaultCommunicationAudioInputDevice() const
    {
        return m_defaultCommunicationAudioInputDevice;
    }

    const AudioDevicePolicy &GetAudioInputDevicePolicy() const
    {
        return m_currentAudioInputDevicePolicy;
    }

    VCSStatus SetAudioInputDevice(const AudioDeviceId &deviceName)
    {
        CHECK_RET1(deviceName.IsValid(), VX_E_INVALID_ARGUMENT);
        /// find in vector or return device not found
        AudioDeviceId validDevice;
        for (std::vector<AudioDeviceId>::const_iterator i = m_audioInputDeviceList.begin(); i != m_audioInputDeviceList.end(); ++i) {
            if (*i == deviceName) {
                validDevice = *i;
                break;
            }
        }
        CHECK_RET1(validDevice.IsValid(), VX_E_NO_EXIST);
        AudioDevicePolicy newPolicy(deviceName);
        if (!(m_desiredAudioInputDevicePolicy == newPolicy)) {
            m_desiredAudioInputDevicePolicy.SetSpecificAudioDevice(deviceName);
            NextState();
        }
        return 0;
    }

    void UseDefaultSystemAudioInputDevice()
    {
        if (m_desiredAudioInputDevicePolicy.GetAudioDevicePolicy() != AudioDevicePolicy::vx_audio_device_policy_default_system) {
            m_desiredAudioInputDevicePolicy.SetUseDefaultSystemAudioDevice();
            NextState();
        }
    }

    void UseDefaultCommunicationAudioInputDevice()
    {
        if (m_desiredAudioInputDevicePolicy.GetAudioDevicePolicy() != AudioDevicePolicy::vx_audio_device_policy_default_communication) {
            m_desiredAudioInputDevicePolicy.SetUseDefaultCommunicationAudioDevice();
            NextState();
        }
    }

    bool IsUsingDefaultSystemAudioInputDevice() const
    {
        return m_desiredAudioInputDevicePolicy.GetAudioDevicePolicy() == AudioDevicePolicy::vx_audio_device_policy_default_system;
    }

    bool IsUsingDefaultCommunicationAudioInputDevice() const
    {
        return m_desiredAudioInputDevicePolicy.GetAudioDevicePolicy() == AudioDevicePolicy::vx_audio_device_policy_default_communication;
    }

    const std::vector<AudioDeviceId> &GetAudioOutputDevices() const
    {
        return m_audioOutputDeviceList;
    }

    AudioDeviceId GetAudioOutputDevice() const
    {
        bool defaultSystem =
            AudioDevicePolicy::vx_audio_device_policy_default_system == m_currentAudioOutputDevicePolicy.GetAudioDevicePolicy();
        bool defaultCommunication =
            AudioDevicePolicy::vx_audio_device_policy_default_communication == m_currentAudioOutputDevicePolicy.GetAudioDevicePolicy();
        if (defaultSystem || defaultCommunication) {
            return AudioDeviceId();
        } else {
            return m_currentAudioOutputDevicePolicy.GetSpecificAudioDevice();
        }
    }

    const AudioDeviceId &GetDefaultSystemAudioOutputDevice() const
    {
        return m_defaultSystemAudioOutputDevice;
    }

    const AudioDeviceId &GetDefaultCommunicationAudioOutputDevice() const
    {
        return m_defaultCommunicationAudioOutputDevice;
    }

    bool IsUsingDefaultSystemAudioOutputDevice() const
    {
        return m_currentAudioOutputDevicePolicy.GetAudioDevicePolicy() == AudioDevicePolicy::vx_audio_device_policy_default_system;
    }

    bool IsUsingDefaultCommunicationAudioOutputDevice() const
    {
        return m_currentAudioOutputDevicePolicy.GetAudioDevicePolicy() == AudioDevicePolicy::vx_audio_device_policy_default_communication;
    }

    VCSStatus SetAudioOutputDevice(const AudioDeviceId &deviceName)
    {
        CHECK_RET1(deviceName.IsValid(), VX_E_INVALID_ARGUMENT);
        /// find in vector or return device not found
        AudioDeviceId validDevice;
        for (std::vector<AudioDeviceId>::const_iterator i = m_audioOutputDeviceList.begin(); i != m_audioOutputDeviceList.end(); ++i) {
            if (*i == deviceName) {
                validDevice = *i;
                break;
            }
        }
        CHECK_RET1(validDevice.IsValid(), VX_E_NO_EXIST);
        if (m_desiredAudioOutputDevicePolicy.GetAudioDevicePolicy() != AudioDevicePolicy::vx_audio_device_policy_specific_device ||
            m_desiredAudioOutputDevicePolicy.GetSpecificAudioDevice() != deviceName)
        {
            m_desiredAudioOutputDevicePolicy.SetSpecificAudioDevice(deviceName);
            NextState();
        }
        return 0;
    }

    void UseDefaultSystemAudioOutputDevice()
    {
        if (m_desiredAudioOutputDevicePolicy.GetAudioDevicePolicy() != AudioDevicePolicy::vx_audio_device_policy_default_system) {
            m_desiredAudioOutputDevicePolicy.SetUseDefaultSystemAudioDevice();
            NextState();
        }
    }

    void UseDefaultCommunicationAudioOutputDevice()
    {
        if (m_desiredAudioOutputDevicePolicy.GetAudioDevicePolicy() != AudioDevicePolicy::vx_audio_device_policy_default_communication) {
            m_desiredAudioOutputDevicePolicy.SetUseDefaultCommunicationAudioDevice();
            NextState();
        }
    }

    const int GetMasterAudioInputDeviceVolume() const
    {
        return m_masterAudioInputDeviceVolume;
    }

    VCSStatus SetMasterAudioInputDeviceVolume(int volume)
    {
        if (volume == m_desiredAudioInputDeviceVolume) {
            return 0;
        }
        CHECK_RET1(volume >= VIVOX_MIN_VOL && volume <= VIVOX_MAX_VOL, VX_E_INVALID_ARGUMENT);
        m_desiredAudioInputDeviceVolume = volume;
        NextState();
        return 0;
    }

    int GetMasterAudioOutputDeviceVolume() const
    {
        return m_masterAudioOutputDeviceVolume;
    }

    VCSStatus SetMasterAudioOutputDeviceVolume(int volume)
    {
        CHECK_RET1(volume >= VIVOX_MIN_VOL && volume <= VIVOX_MAX_VOL, VX_E_INVALID_ARGUMENT);
        if (volume == m_desiredAudioOutputDeviceVolume) {
            return 0;
        }
        m_desiredAudioOutputDeviceVolume = volume;
        NextState();
        return 0;
    }

    int GetChannelAudioOutputDeviceVolume(const AccountName &accountName, const Uri &channelUri)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> s = FindLogin(accountName);
        if (s) {
            return NextState(s->GetChannelAudioOutputDeviceVolume(channelUri));
        }
        return 50;     /// default value
    }

    VCSStatus SetChannelAudioOutputDeviceVolume(const AccountName &accountName, const Uri &channelUri, int volume)
    {
        CHECK_RET1(volume >= VIVOX_MIN_VOL && volume <= VIVOX_MAX_VOL, VX_E_INVALID_ARGUMENT);
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> s = FindLogin(accountName);
        if (s) {
            return NextState(s->SetChannelAudioOutputDeviceVolume(channelUri, volume));
        }
        return VX_E_NO_EXIST;
    }

    int GetParticipantAudioOutputDeviceVolumeForMe(const AccountName &accountName, const Uri &target, const Uri &channelUri)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> s = FindLogin(accountName);
        if (s) {
            return NextState(s->GetParticipantAudioOutputDeviceVolumeForMe(target, channelUri));
        }
        return 50;     /// default value
    }

    VCSStatus SetParticipantAudioOutputDeviceVolumeForMe(const AccountName &accountName, const Uri &target, const Uri &channelUri, int volume)
    {
        CHECK_RET1(volume >= VIVOX_MIN_VOL && volume <= VIVOX_MAX_VOL, VX_E_INVALID_ARGUMENT);
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> s = FindLogin(accountName);
        if (s) {
            return NextState(s->SetParticipantAudioOutputDeviceVolumeForMe(target, channelUri, volume));
        }
        return VX_E_NO_EXIST;
    }

    VCSStatus SetParticipantMutedForAll(const AccountName &accountName, const Uri &target, const Uri &channelUri, bool muted, const char *accessToken)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> s = FindLogin(accountName);
        if (s) {
            return NextState(s->SetParticipantMutedForAll(target, channelUri, muted, accessToken));
        }
        return VX_E_NO_EXIST;
    }

    bool GetParticipantMutedForAll(const AccountName &accountName, const Uri &targetUser, const Uri &channelUri)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> s = FindLogin(accountName);
        if (s) {
            return s->GetParticipantMutedForAll(targetUser, channelUri);
        }
        return false;
    }


    VCSStatus SetParticipantMutedForMe(const AccountName &accountName, const Uri &target, const Uri &channelUri, bool muted)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> s = FindLogin(accountName);
        if (s) {
            return NextState(s->SetParticipantMutedForMe(target, channelUri, muted));
        }
        return VX_E_NO_EXIST;
    }

    ChannelTransmissionPolicy GetChannelTransmissionPolicy(const AccountName &accountName)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> s = FindLogin(accountName);
        if (s) {
            return s->GetChannelTransmissionPolicy();
        }
        return ChannelTransmissionPolicy();     /// default value
    }

    VCSStatus SetTransmissionToSpecificChannel(const AccountName &accountName, const Uri &channelUri)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> s = FindLogin(accountName);
        if (s) {
            return NextState(s->SetTransmissionToSpecificChannel(channelUri));
        }
        return VX_E_NO_EXIST;
    }

    VCSStatus Set3DPosition(const AccountName &accountName, const Uri &channelUri, double x, double y, double z, double at_x, double at_y, double at_z)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> s = FindLogin(accountName);
        if (s) {
            return NextState(s->Set3DPosition(channelUri, x, y, z, at_x, at_y, at_z));
        }
        return VX_E_NO_EXIST;
    }

    VCSStatus SetTransmissionToAll(const AccountName &accountName)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> s = FindLogin(accountName);
        if (s) {
            return NextState(s->SetTransmissionToAll());
        }
        return VX_E_NO_EXIST;
    }

    VCSStatus SetTransmissionToNone(const AccountName &accountName)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> s = FindLogin(accountName);
        if (s) {
            return NextState(s->SetTransmissionToNone());
        }
        return VX_E_NO_EXIST;
    }

    VCSStatus SetSttTranscriptionOn(const AccountName &accountName, const Uri &channel, bool on, const char *accessToken)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> s = FindLogin(accountName);
        if (s) {
            return NextState(s->SetSttTranscriptionOn(channel, on, accessToken));
        }
        return VX_E_NO_EXIST;
    }

private:
    VCSStatus NextState(VCSStatus status)
    {
        NextState();
        return status;
    }

    void NextState()
    {
        // if we are where we want to be don't do anything
        if (m_desiredServer == m_currentServer && m_desiredState == m_currentState) {
        } else {
            if (m_desiredState == ConnectorStateInitialized) {
                CHECK_RET(m_desiredServer.IsValid());
                if (m_currentState == ConnectorStateUninitialized) {
                    CHECK_RET(m_connectorHandle.empty());
                    CHECK_RET(!m_currentServer.IsValid());
                    vx_req_connector_create_t *req;
                    vx_req_connector_create_create(&req);
                    req->acct_mgmt_server = vx_strdup(m_desiredServer.ToString());
                    req->application = vx_strdup(m_application.c_str());
                    req->base.cookie = GetNextRequestId(NULL, "C");
                    req->connector_handle = vx_strdup(req->base.cookie);
                    req->configured_codecs = m_codecMask;
                    if (m_desiredMinUdpPort < m_desiredMaxUdpPort) {
                        req->minimum_port = m_desiredMinUdpPort;
                        req->maximum_port = m_desiredMaxUdpPort;
                    }
                    m_connectorHandle = req->connector_handle;
                    m_currentState = ConnectorStateInitializing;
                    m_currentServer = m_desiredServer;
                    m_currentMinUdpPort = m_desiredMinUdpPort;
                    m_currentMaxUdpPort = m_desiredMaxUdpPort;
                    issueRequest(&req->base);
                }
            } else if (m_desiredState == ConnectorStateUninitialized) {
                CHECK_RET(!m_desiredServer.IsValid());
                if (m_currentState == ConnectorStateInitialized) {
                    CHECK_RET(m_currentServer.IsValid());
                    CHECK_RET(!m_connectorHandle.empty());
                    vx_req_connector_initiate_shutdown *req;
                    vx_req_connector_initiate_shutdown_create(&req);
                    req->connector_handle = vx_strdup(m_connectorHandle.c_str());
                    m_currentState = ConnectorStateUninitializing;
                    issueRequest(&req->base);
                }
            }
        }
        // if we are connected to the right backend...
        if (m_desiredState == ConnectorStateInitialized && m_currentState == ConnectorStateInitialized && m_desiredServer == m_currentServer) {
            std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
            for (std::map<AccountName, std::shared_ptr<SingleLoginMultiChannelManager> >::const_iterator i = m_logins.begin(); i != m_logins.end(); ++i) {
                i->second->NextState();
            }
        }
        // audio device and master volume states
        if (!(m_currentAudioInputDevicePolicy == m_desiredAudioInputDevicePolicy)) {
            /// This only puts in a change request if the effective device would change
            vx_req_aux_set_capture_device_t *req;
            vx_req_aux_set_capture_device_create(&req);

            req->base.vcookie = new AudioDevicePolicy(m_desiredAudioInputDevicePolicy);

            req->capture_device_specifier = vx_strdup(AudioDeviceIdString(m_desiredAudioInputDevicePolicy.GetSpecificAudioDevice()).c_str());
            issueRequest(&req->base);
            m_currentAudioInputDevicePolicy = m_desiredAudioInputDevicePolicy;
        }
        if (!(m_currentAudioOutputDevicePolicy == m_desiredAudioOutputDevicePolicy)) {
            /// This only puts in a change request if the effective device would change
            vx_req_aux_set_render_device_t *req;
            vx_req_aux_set_render_device_create(&req);

            req->base.vcookie = new AudioDevicePolicy(m_desiredAudioOutputDevicePolicy);

            req->render_device_specifier = vx_strdup(AudioDeviceIdString(m_desiredAudioOutputDevicePolicy.GetSpecificAudioDevice()).c_str());
            issueRequest(&req->base);
            m_currentAudioOutputDevicePolicy = m_desiredAudioOutputDevicePolicy;
        }
        if (m_masterAudioInputDeviceVolume != m_desiredAudioInputDeviceVolume) {
            if (!m_masterAudioInputDeviceVolumeRequestInProgress) {
                vx_req_aux_set_mic_level_t *req;
                vx_req_aux_set_mic_level_create(&req);
                req->level = m_desiredAudioInputDeviceVolume;
                issueRequest(&req->base);
                m_masterAudioInputDeviceVolumeRequestInProgress = true;
                m_masterAudioInputDeviceVolume = req->level;
            }
        }
        if (m_masterAudioOutputDeviceVolume != m_desiredAudioOutputDeviceVolume) {
            if (!m_masterAudioOutputDeviceVolumeRequestInProgress) {
                vx_req_aux_set_speaker_level_t *req;
                vx_req_aux_set_speaker_level_create(&req);
                req->level = m_desiredAudioOutputDeviceVolume;
                issueRequest(&req->base);
                m_masterAudioOutputDeviceVolumeRequestInProgress = true;
                m_masterAudioOutputDeviceVolume = req->level;
            }
        }
    }

    std::shared_ptr<SingleLoginMultiChannelManager> FindLogin(const AccountName &name) const
    {
        std::map<AccountName, std::shared_ptr<SingleLoginMultiChannelManager> >::const_iterator i = m_logins.find(name);
        if (i == m_logins.end()) {
            return nullptr;
        }
        return i->second;
    }

    std::shared_ptr<SingleLoginMultiChannelManager> FindLogin(const AccountName &name, const char *accessToken)
    {
        std::map<AccountName, std::shared_ptr<SingleLoginMultiChannelManager> >::const_iterator i = m_logins.find(name);
        if (i == m_logins.end()) {
            if (accessToken) {
                std::shared_ptr<SingleLoginMultiChannelManager> s = std::make_shared<SingleLoginMultiChannelManager>(
                        m_app,
                        m_connectorHandle,
                        name,
                        m_multiChannel);
                m_logins[name] = s;
                return s;
            } else {
                return nullptr;
            }
        }
        return i->second;
    }

    void ClearLoginsMap()
    {
        m_logins.clear();
    }

    std::shared_ptr<SingleLoginMultiChannelManager> FindLoginBySessionHandle(const char *sessionHandle) const
    {
        for (std::map<AccountName, std::shared_ptr<SingleLoginMultiChannelManager> >::const_iterator i = m_logins.begin(); i != m_logins.end(); ++i) {
            if (i->second->IsUsingSessionHandle(sessionHandle)) {
                return i->second;
            }
        }
        return NULL;
    }

    std::shared_ptr<SingleLoginMultiChannelManager> FindLoginBySessionGroupHandle(const char *sessionGroupHandle) const
    {
        if (NULL != sessionGroupHandle) {
            for (std::map<AccountName, std::shared_ptr<SingleLoginMultiChannelManager> >::const_iterator i = m_logins.begin(); i != m_logins.end(); ++i) {
                if (i->second->GetSessionGroupHandle() == sessionGroupHandle) {
                    return i->second;
                }
            }
        }
        return NULL;
    }

    std::shared_ptr<SingleLoginMultiChannelManager> FindLogin(const char *accountHandle) const
    {
        for (std::map<AccountName, std::shared_ptr<SingleLoginMultiChannelManager> >::const_iterator i = m_logins.begin(); i != m_logins.end(); ++i) {
            if (i->second->GetAccountHandle() == accountHandle) {
                return i->second;
            }
        }
        return nullptr;
    }

    static void sOnLogMessageFromSdk(void *callbackHandle, vx_log_level level, const char *source, const char *message)
    {
        ClientConnectionImpl *pThis = reinterpret_cast<ClientConnectionImpl *>(callbackHandle);
        pThis->OnLogMessage(level, source, message);
    }

    static void sOnResponseOrEventFromSdk(void *callbackHandle)
    {
        ClientConnectionImpl *pThis = reinterpret_cast<ClientConnectionImpl *>(callbackHandle);
        pThis->OnResponseOrEventFromSdk();
    }

    static void f_on_audio_unit_started(void *callbackHandle, const char *session_group_handle, const char *initial_target_uri)
    {
        ClientConnectionImpl *pThis = reinterpret_cast<ClientConnectionImpl *>(callbackHandle);
        pThis->OnAudioUnitStarted(session_group_handle, initial_target_uri);
    }

    static void f_on_audio_unit_stopped(void *callbackHandle, const char *session_group_handle, const char *initial_target_uri)
    {
        ClientConnectionImpl *pThis = reinterpret_cast<ClientConnectionImpl *>(callbackHandle);
        pThis->OnAudioUnitStopped(session_group_handle, initial_target_uri);
    }

    static void f_on_audio_unit_after_capture_audio_read(void *callbackHandle, const char *session_group_handle, const char *initial_target_uri, short *pcm_frames, int pcm_frame_count, int audio_frame_rate, int channels_per_frame)
    {
        ClientConnectionImpl *pThis = reinterpret_cast<ClientConnectionImpl *>(callbackHandle);
        pThis->OnAudioUnitAfterCaptureAudioRead(session_group_handle, initial_target_uri, pcm_frames, pcm_frame_count, audio_frame_rate, channels_per_frame);
    }

    static void f_on_audio_unit_before_capture_audio_sent(void *callbackHandle, const char *session_group_handle, const char *initial_target_uri, short *pcm_frames, int pcm_frame_count, int audio_frame_rate, int channels_per_frame, int is_speaking)
    {
        ClientConnectionImpl *pThis = reinterpret_cast<ClientConnectionImpl *>(callbackHandle);
        pThis->OnAudioUnitBeforeCaptureAudioSent(session_group_handle, initial_target_uri, pcm_frames, pcm_frame_count, audio_frame_rate, channels_per_frame, is_speaking);
    }

    static void f_on_audio_unit_before_recv_audio_rendered(void *callbackHandle, const char *session_group_handle, const char *initial_target_uri, short *pcm_frames, int pcm_frame_count, int audio_frame_rate, int channels_per_frame, int is_silence)
    {
        ClientConnectionImpl *pThis = reinterpret_cast<ClientConnectionImpl *>(callbackHandle);
        pThis->OnAudioUnitBeforeRecvAudioRendered(session_group_handle, initial_target_uri, pcm_frames, pcm_frame_count, audio_frame_rate, channels_per_frame, is_silence);
    }

    AccountName GetAccountName(const char *session_group_handle)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> l = FindLoginBySessionGroupHandle(session_group_handle);
        if (l == NULL) {
            return AccountName();
        }
        return l->GetName();
    }

    void OnAudioUnitStarted(const char *session_group_handle, const char *initial_target_uri)
    {
        if (m_app != NULL) {
            m_app->onAudioUnitStarted(GetAccountName(session_group_handle), Uri(initial_target_uri));
        }
    }

    void OnAudioUnitStopped(const char *session_group_handle, const char *initial_target_uri)
    {
        if (m_app != NULL) {
            m_app->onAudioUnitStopped(GetAccountName(session_group_handle), Uri(initial_target_uri));
        }
    }

    void OnAudioUnitAfterCaptureAudioRead(const char *session_group_handle, const char *initial_target_uri, short *pcm_frames, int pcm_frame_count, int audio_frame_rate, int channels_per_frame)
    {
        if (m_app != NULL) {
            m_app->onAudioUnitAfterCaptureAudioRead(GetAccountName(session_group_handle), Uri(initial_target_uri), pcm_frames, pcm_frame_count, audio_frame_rate, channels_per_frame);
        }
    }

    void OnAudioUnitBeforeCaptureAudioSent(const char *session_group_handle, const char *initial_target_uri, short *pcm_frames, int pcm_frame_count, int audio_frame_rate, int channels_per_frame, int is_speaking)
    {
        if (m_app != NULL) {
            m_app->onAudioUnitBeforeCaptureAudioSent(GetAccountName(session_group_handle), Uri(initial_target_uri), pcm_frames, pcm_frame_count, audio_frame_rate, channels_per_frame, is_speaking);
        }
    }

    void OnAudioUnitBeforeRecvAudioRendered(const char *session_group_handle, const char *initial_target_uri, short *pcm_frames, int pcm_frame_count, int audio_frame_rate, int channels_per_frame, int is_silence)
    {
        if (m_app != NULL) {
            m_app->onAudioUnitBeforeRecvAudioRendered(GetAccountName(session_group_handle), Uri(initial_target_uri), pcm_frames, pcm_frame_count, audio_frame_rate, channels_per_frame, is_silence);
        }
    }

    void OnLogMessage(vx_log_level level, const char *source, const char *message)
    {
        if (m_app != NULL) {
            std::stringstream ss;
            ss << source << " - " << message;
            FILETIME ft;
            GetSystemTimeAsFileTime(&ft);
            ULARGE_INTEGER ul;
            ul.HighPart = ft.dwHighDateTime;
            ul.LowPart = ft.dwLowDateTime;
            m_app->onLogStatementEmitted((IClientApiEventHandler::LogLevel)level, ul.QuadPart, GetCurrentThreadId(), ss.str().c_str());
        }
    }

    void OnResponseOrEventFromSdk()
    {
        if (m_app != NULL) {
            m_app->InvokeOnUIThread(&sOnResponseOrEventFromSdkUiThread, this);
        }
    }

    static void sOnResponseOrEventFromSdkUiThread(void *callbackHandle)
    {
        ClientConnectionImpl *pThis = reinterpret_cast<ClientConnectionImpl *>(callbackHandle);
        pThis->OnResponseOrEventFromSdkUiThread();
    }

    void HandleResponse(vx_resp_connector_create *resp)
    {
        vx_req_connector_create_t *req = reinterpret_cast<vx_req_connector_create_t *>(resp->base.request);
        Uri server(req->acct_mgmt_server);
        if (server == m_currentServer) {
            if (resp->base.return_code == 0) {
                m_currentState = ConnectorStateInitialized;
            }
        }
        if (m_desiredState == ConnectorStateInitialized && m_desiredServer == m_currentServer) {
            // case 1 - app is still waiting to connect
            if (resp->base.return_code == 1) {
                m_desiredState = ConnectorStateUninitialized;
                m_currentState = ConnectorStateUninitialized;
                m_connectorHandle.clear();     // to allow another Connect() call after onConnectFailed()
                m_desiredServer.Clear();
                m_desiredMinUdpPort = m_desiredMaxUdpPort = 0;
                m_currentServer.Clear();
                m_app->onConnectFailed(server, resp->base.status_code);
            } else {
                m_app->onConnectCompleted(server);
                m_app->onConnectCompletedEx(
                        server,
                        static_cast<vx_backend_type>(resp->backend_type),
                        m_connectorHandle.c_str());
            }
        }
        NextState();
    }

    void HandleResponse(vx_resp_connector_initiate_shutdown *resp)
    {
        CHECK_RET(resp->base.return_code == 0);
        if (m_desiredState == ConnectorStateUninitialized) {
            m_currentState = m_desiredState;
            m_connectorHandle.clear();     // to allow another Connect() call after onDisconnect()
            m_desiredServer.Clear();
            m_desiredMinUdpPort = m_desiredMaxUdpPort = 0;
            Uri currentServer = m_currentServer;
            m_currentServer.Clear();
            m_app->onDisconnected(currentServer, 0);
        }
        NextState();
    }

    void HandleResponse(vx_resp_account_anonymous_login *resp)
    {
        vx_req_account_anonymous_login *req = reinterpret_cast<vx_req_account_anonymous_login *>(resp->base.request);
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> login = FindLogin(req->account_handle);
        if (login != NULL) {
            login->HandleResponse(resp);
        }

        NextState();
    }

    void HandleResponse(vx_resp_account_logout *resp)
    {
        vx_req_account_logout *req = reinterpret_cast<vx_req_account_logout *>(resp->base.request);
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> login = FindLogin(req->account_handle);
        if (login != NULL) {
            login->HandleResponse(resp);
        }

        NextState();
    }

    void HandleResponse(vx_resp_channel_kick_user *resp)
    {
        vx_req_channel_kick_user *req = reinterpret_cast<vx_req_channel_kick_user *>(resp->base.request);
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> login = FindLogin(req->account_handle);
        if (login != NULL) {
            login->HandleResponse(resp);
        }

        NextState();
    }

    void HandleResponse(vx_resp_sessiongroup_get_stats *resp)
    {
        if (resp->base.return_code != 0) {
            LOG_ERR("Cannot Process vx_resp_sessiongroup_get_stats due to error: (%d) %s", resp->base.status_code, vx_get_error_string(resp->base.status_code));
        }
        CHECK_RET(resp->base.return_code == 0);
        m_app->onGetStats(resp);
    }

    void HandleResponse(vx_resp_sessiongroup_add_session *resp)
    {
        vx_req_sessiongroup_add_session *req = reinterpret_cast<vx_req_sessiongroup_add_session *>(resp->base.request);
#ifdef _DEBUG
        LOG_INFO("%s: got vx_resp_sessiongroup_add_session (return_code %d) for %s\n", NowString().c_str(), resp->base.return_code, req->uri);
#endif
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> login = FindLoginBySessionGroupHandle(req->sessiongroup_handle);
        CHECK_RET(login != NULL);
        login->HandleResponse(resp);

        NextState();
    }

    void HandleResponse(vx_resp_sessiongroup_remove_session *resp)
    {
        if (resp->base.return_code != 0) {
            LOG_ERR("Cannot Process vx_resp_sessiongroup_remove_session due to error: (%d) %s", resp->base.status_code, vx_get_error_string(resp->base.status_code));
        } else {
            vx_req_sessiongroup_remove_session *req = reinterpret_cast<vx_req_sessiongroup_remove_session *>(resp->base.request);
            std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
            std::shared_ptr<SingleLoginMultiChannelManager> login = FindLoginBySessionGroupHandle(req->sessiongroup_handle);
            CHECK_RET(login != NULL);
            login->HandleResponse(resp);
        }
        NextState();
    }

    void HandleResponse(vx_resp_sessiongroup_control_audio_injection *resp)
    {
        vx_req_sessiongroup_control_audio_injection_t *req = (vx_req_sessiongroup_control_audio_injection_t *)resp->base.request;

        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> login = FindLoginBySessionGroupHandle(req->sessiongroup_handle);
        CHECK_RET(login != NULL);
        login->HandleResponse(resp);

        NextState();
    }

    void HandleResponse(vx_resp_account_control_communications *resp)
    {
        vx_req_account_control_communications_t *req = reinterpret_cast<vx_req_account_control_communications_t *>(resp->base.request);
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> login = FindLogin(req->account_handle);
        CHECK_RET(login != NULL);
        login->HandleResponse(resp);
        m_clock = clock();
        NextState();
    }

    void HandleResponse(vx_resp_aux_get_capture_devices *resp)
    {
        if (resp->base.status_code == 0) {
            std::vector<AudioDeviceId> oldDevices = m_audioInputDeviceList;
            bool defaultSystemDeviceChanged = false;
            bool defaultCommunicationDeviceChanged = false;
            bool deviceListChanged = false;
            m_audioInputDeviceList.clear();

            for (int i = 0; i < resp->count; ++i) {
                if (resp->capture_devices[i]->device_type == vx_device_type_specific_device) {
                    m_audioInputDeviceList.push_back(AudioDeviceIdFromCodePage(resp->capture_devices[i]->device, resp->capture_devices[i]->display_name));
                }
            }
            if (oldDevices != m_audioInputDeviceList) {
                deviceListChanged = true;
            }

            AudioDeviceId defaultSystemAudioInputDevice =
                AudioDeviceIdFromCodePage(resp->default_capture_device->device, resp->default_capture_device->display_name);
            if (m_defaultSystemAudioInputDevice != defaultSystemAudioInputDevice) {
                defaultSystemDeviceChanged = true;
                m_defaultSystemAudioInputDevice = defaultSystemAudioInputDevice;
            }

            AudioDeviceId defaultCommunicationAudioInputDevice =
                AudioDeviceIdFromCodePage(
                        resp->default_communication_capture_device->device,
                        resp->default_communication_capture_device->display_name);
            if (m_defaultCommunicationAudioInputDevice != defaultCommunicationAudioInputDevice) {
                defaultCommunicationDeviceChanged = true;
                m_defaultCommunicationAudioInputDevice = defaultCommunicationAudioInputDevice;
            }

            if (deviceListChanged) {
                m_app->onAvailableAudioDevicesChanged();
            }

            if (defaultSystemDeviceChanged) {
                m_app->onDefaultSystemAudioInputDeviceChanged(m_defaultSystemAudioInputDevice);
            }

            if (defaultCommunicationDeviceChanged) {
                m_app->onDefaultCommunicationAudioInputDeviceChanged(m_defaultCommunicationAudioInputDevice);
            }

            m_audioInputDeviceListPopulated = true;
        }
    }

    void HandleResponse(vx_resp_aux_get_render_devices *resp)
    {
        if (resp->base.status_code == 0) {
            std::vector<AudioDeviceId> oldDevices = m_audioOutputDeviceList;
            bool defaultSystemDeviceChanged = false;
            bool defaultCommunicationDeviceChanged = false;
            bool deviceListChanged = false;
            m_audioOutputDeviceList.clear();

            for (int i = 0; i < resp->count; ++i) {
                if (resp->render_devices[i]->device_type == vx_device_type_specific_device) {
                    m_audioOutputDeviceList.push_back(AudioDeviceIdFromCodePage(resp->render_devices[i]->device, resp->render_devices[i]->display_name));
                }
            }
            if (oldDevices != m_audioOutputDeviceList) {
                deviceListChanged = true;
            }

            AudioDeviceId defaultSystemAudioOutputDevice =
                AudioDeviceIdFromCodePage(resp->default_render_device->device, resp->default_render_device->display_name);
            if (m_defaultSystemAudioOutputDevice != defaultSystemAudioOutputDevice) {
                defaultSystemDeviceChanged = true;
                m_defaultSystemAudioOutputDevice = defaultSystemAudioOutputDevice;
            }

            AudioDeviceId defaultCommunicationAudioOutputDevice =
                AudioDeviceIdFromCodePage(resp->default_communication_render_device->device, resp->default_communication_render_device->display_name);
            if (m_defaultCommunicationAudioOutputDevice != defaultCommunicationAudioOutputDevice) {
                defaultCommunicationDeviceChanged = true;
                m_defaultCommunicationAudioOutputDevice = defaultCommunicationAudioOutputDevice;
            }

            if (deviceListChanged) {
                m_app->onAvailableAudioDevicesChanged();
            }

            if (defaultSystemDeviceChanged) {
                m_app->onDefaultSystemAudioOutputDeviceChanged(m_defaultSystemAudioOutputDevice);
            }

            if (defaultCommunicationDeviceChanged) {
                m_app->onDefaultCommunicationAudioOutputDeviceChanged(m_defaultCommunicationAudioOutputDevice);
            }

            m_audioOutputDeviceListPopulated = true;
        }
    }

    void HandleResponse(vx_resp_aux_set_capture_device *resp)
    {
        vx_req_aux_set_capture_device_t *req = reinterpret_cast<vx_req_aux_set_capture_device_t *>(resp->base.request);
        AudioDevicePolicy *requestedDevicePolicy = (AudioDevicePolicy *)req->base.vcookie;
        if (resp->base.return_code != 0) {
            // setting "use default system device" should never fail
            CHECK_RET(requestedDevicePolicy->GetAudioDevicePolicy() != AudioDevicePolicy::vx_audio_device_policy_default_system);
            // setting "use default communication device" should never fail
            CHECK_RET(requestedDevicePolicy->GetAudioDevicePolicy() != AudioDevicePolicy::vx_audio_device_policy_default_communication);
            // if we do fail, set the desired to the current
            m_desiredAudioInputDevicePolicy = m_currentAudioInputDevicePolicy;
            m_app->onSetAudioOutputDeviceFailed(requestedDevicePolicy->GetSpecificAudioDevice(), resp->base.status_code);
        } else {
            m_currentAudioInputDevicePolicy = *requestedDevicePolicy;
            if (requestedDevicePolicy->GetAudioDevicePolicy() != AudioDevicePolicy::vx_audio_device_policy_default_system &&
                requestedDevicePolicy->GetAudioDevicePolicy() != AudioDevicePolicy::vx_audio_device_policy_default_communication)
            {
                m_app->onSetAudioInputDeviceCompleted(requestedDevicePolicy->GetSpecificAudioDevice());
            }
        }
        NextState();
        delete requestedDevicePolicy;
    }

    void HandleResponse(vx_resp_aux_set_mic_level_t *resp)
    {
        CHECK(resp->base.return_code == 0);
        m_masterAudioInputDeviceVolumeRequestInProgress = false;
        NextState();
    }

    void HandleResponse(vx_resp_aux_set_speaker_level_t *resp)
    {
        CHECK(resp->base.return_code == 0);
        m_masterAudioOutputDeviceVolumeRequestInProgress = false;
        NextState();
    }

    void HandleResponse(vx_resp_aux_set_render_device *resp)
    {
        vx_req_aux_set_render_device_t *req = reinterpret_cast<vx_req_aux_set_render_device_t *>(resp->base.request);
        AudioDevicePolicy *requestedDevicePolicy = (AudioDevicePolicy *)req->base.vcookie;
        if (resp->base.return_code != 0) {
            // setting "use default system device" should never fail
            CHECK_RET(requestedDevicePolicy->GetAudioDevicePolicy() != AudioDevicePolicy::vx_audio_device_policy_default_system);
            // setting "use default communication device" should never fail
            CHECK_RET(requestedDevicePolicy->GetAudioDevicePolicy() != AudioDevicePolicy::vx_audio_device_policy_default_communication);
            // if we do fail, set the desired to the current
            m_desiredAudioOutputDevicePolicy = m_currentAudioOutputDevicePolicy;
            m_app->onSetAudioOutputDeviceFailed(requestedDevicePolicy->GetSpecificAudioDevice(), resp->base.status_code);
        } else {
            m_currentAudioOutputDevicePolicy = *requestedDevicePolicy;
            if (requestedDevicePolicy->GetAudioDevicePolicy() != AudioDevicePolicy::vx_audio_device_policy_default_system &&
                requestedDevicePolicy->GetAudioDevicePolicy() != AudioDevicePolicy::vx_audio_device_policy_default_communication)
            {
                m_app->onSetAudioOutputDeviceCompleted(m_currentAudioOutputDevicePolicy.GetSpecificAudioDevice());
            }
        }
        NextState();
        delete requestedDevicePolicy;
    }

    void HandleResponse(vx_resp_session_set_local_speaker_volume *resp)
    {
        vx_req_session_set_local_speaker_volume *req = reinterpret_cast<vx_req_session_set_local_speaker_volume *>(resp->base.request);
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> login = FindLoginBySessionHandle(req->session_handle);
        CHECK_RET(login != NULL);
        login->HandleResponse(resp);

        NextState();
    }

    void HandleResponse(vx_resp_session_set_local_render_volume *resp)
    {
        vx_req_session_set_local_render_volume *req = reinterpret_cast<vx_req_session_set_local_render_volume *>(resp->base.request);
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> login = FindLoginBySessionHandle(req->session_handle);
        CHECK_RET(login != NULL);
        login->HandleResponse(resp);

        NextState();
    }

    void HandleResponse(vx_resp_session_set_participant_volume_for_me *resp)
    {
        vx_req_session_set_participant_volume_for_me *req = reinterpret_cast<vx_req_session_set_participant_volume_for_me *>(resp->base.request);
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> login = FindLoginBySessionHandle(req->session_handle);
        CHECK_RET(login != NULL);
        login->HandleResponse(resp);

        NextState();
    }

    void HandleResponse(vx_resp_channel_mute_user *resp)
    {
        vx_req_channel_mute_user *req = reinterpret_cast<vx_req_channel_mute_user *>(resp->base.request);
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> login = FindLogin(req->account_handle);
        CHECK_RET(login != NULL);
        login->HandleResponse(resp);

        NextState();
    }

    void HandleResponse(vx_resp_channel_mute_all_users *resp)
    {
        vx_req_channel_mute_all_users *req = reinterpret_cast<vx_req_channel_mute_all_users *>(resp->base.request);
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> login = FindLogin(req->account_handle);
        CHECK_RET(login != NULL);
        login->HandleResponse(resp);

        NextState();
    }

    void HandleResponse(vx_resp_session_set_participant_mute_for_me *resp)
    {
        vx_req_session_set_participant_mute_for_me *req = reinterpret_cast<vx_req_session_set_participant_mute_for_me *>(resp->base.request);
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> login = FindLoginBySessionHandle(req->session_handle);
        CHECK_RET(login != NULL);
        login->HandleResponse(resp);

        NextState();
    }

    void HandleResponse(vx_resp_sessiongroup_set_tx_session *resp)
    {
        vx_req_sessiongroup_set_tx_session_t *req = (vx_req_sessiongroup_set_tx_session_t *)resp->base.request;
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> login = FindLoginBySessionHandle(req->session_handle);
        CHECK_RET(login != NULL);
        login->HandleResponse(resp);

        NextState();
    }

    void HandleResponse(vx_resp_sessiongroup_set_tx_all_sessions *resp)
    {
        vx_req_sessiongroup_set_tx_all_sessions_t *req = (vx_req_sessiongroup_set_tx_all_sessions_t *)resp->base.request;
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> login = FindLoginBySessionGroupHandle(req->sessiongroup_handle);
        CHECK_RET(login != NULL);
        login->HandleResponse(resp);

        NextState();
    }

    void HandleResponse(vx_resp_sessiongroup_set_tx_no_session *resp)
    {
        vx_req_sessiongroup_set_tx_no_session_t *req = (vx_req_sessiongroup_set_tx_no_session_t *)resp->base.request;
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> login = FindLoginBySessionGroupHandle(req->sessiongroup_handle);
        CHECK_RET(login != NULL);
        login->HandleResponse(resp);

        NextState();
    }

    void HandleResponse(vx_resp_aux_render_audio_start_t *resp)
    {
        CHECK_RET(resp->base.return_code != 1);
    }

    void HandleResponse(vx_resp_aux_render_audio_stop_t *resp)
    {
        CHECK_RET(resp->base.return_code != 1);
    }

    void DispatchResponse(vx_resp_base_t *resp)
    {
        switch (resp->type) {
            case resp_connector_create:
                return HandleResponse(reinterpret_cast<vx_resp_connector_create *>(resp));
            case resp_connector_initiate_shutdown:
                return HandleResponse(reinterpret_cast<vx_resp_connector_initiate_shutdown *>(resp));
            case resp_account_anonymous_login:
                return HandleResponse(reinterpret_cast<vx_resp_account_anonymous_login *>(resp));
            case resp_account_logout:
                return HandleResponse(reinterpret_cast<vx_resp_account_logout *>(resp));
            case resp_channel_kick_user:
                return HandleResponse(reinterpret_cast<vx_resp_channel_kick_user *>(resp));
            case resp_sessiongroup_get_stats:
                return HandleResponse(reinterpret_cast<vx_resp_sessiongroup_get_stats *>(resp));
            case resp_sessiongroup_add_session:
                return HandleResponse(reinterpret_cast<vx_resp_sessiongroup_add_session *>(resp));
            case resp_sessiongroup_remove_session:
                return HandleResponse(reinterpret_cast<vx_resp_sessiongroup_remove_session *>(resp));
            case resp_sessiongroup_control_audio_injection:
                return HandleResponse(reinterpret_cast<vx_resp_sessiongroup_control_audio_injection *>(resp));
            case resp_account_control_communications:
                return HandleResponse(reinterpret_cast<vx_resp_account_control_communications *>(resp));
            case resp_aux_get_capture_devices:
                return HandleResponse(reinterpret_cast<vx_resp_aux_get_capture_devices *>(resp));
            case resp_aux_get_render_devices:
                return HandleResponse(reinterpret_cast<vx_resp_aux_get_render_devices *>(resp));
            case resp_aux_set_capture_device:
                return HandleResponse(reinterpret_cast<vx_resp_aux_set_capture_device *>(resp));
            case resp_aux_set_render_device:
                return HandleResponse(reinterpret_cast<vx_resp_aux_set_render_device *>(resp));
            case resp_aux_set_mic_level:
                return HandleResponse(reinterpret_cast<vx_resp_aux_set_mic_level *>(resp));
            case resp_aux_set_speaker_level:
                return HandleResponse(reinterpret_cast<vx_resp_aux_set_speaker_level *>(resp));
            case resp_session_set_local_speaker_volume:
                return HandleResponse(reinterpret_cast<vx_resp_session_set_local_speaker_volume *>(resp));
            case resp_session_set_local_render_volume:
                return HandleResponse(reinterpret_cast<vx_resp_session_set_local_render_volume *>(resp));
            case resp_session_set_participant_volume_for_me:
                return HandleResponse(reinterpret_cast<vx_resp_session_set_participant_volume_for_me *>(resp));
            case resp_channel_mute_user:
                return HandleResponse(reinterpret_cast<vx_resp_channel_mute_user *>(resp));
            case resp_channel_mute_all_users:
                return HandleResponse(reinterpret_cast<vx_resp_channel_mute_all_users *>(resp));
            case resp_session_set_participant_mute_for_me:
                return HandleResponse(reinterpret_cast<vx_resp_session_set_participant_mute_for_me *>(resp));
            case resp_sessiongroup_set_tx_session:
                return HandleResponse(reinterpret_cast<vx_resp_sessiongroup_set_tx_session *>(resp));
            case resp_sessiongroup_set_tx_all_sessions:
                return HandleResponse(reinterpret_cast<vx_resp_sessiongroup_set_tx_all_sessions *>(resp));
            case resp_sessiongroup_set_tx_no_session:
                return HandleResponse(reinterpret_cast<vx_resp_sessiongroup_set_tx_no_session *>(resp));
            case resp_aux_render_audio_start:
                return HandleResponse(reinterpret_cast<vx_resp_aux_render_audio_start_t *>(resp));
            case resp_aux_render_audio_stop:
                return HandleResponse(reinterpret_cast<vx_resp_aux_render_audio_stop_t *>(resp));
            case resp_aux_start_buffer_capture:
                return;
            case resp_aux_capture_audio_stop:
                return;
            case resp_aux_play_audio_buffer:
                return;
            case resp_connector_mute_local_mic:
                return;
            case resp_connector_mute_local_speaker:
                return;
            case resp_aux_notify_application_state_change:
                return;
            case resp_session_transcription_control:
                return;
            default:
                CHECK_RET(resp == NULL);
        }
    }

    void DispatchEvent(vx_evt_account_login_state_change *evt)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> login = FindLogin(evt->account_handle);
        if (login != NULL) {
            login->HandleEvent(evt);
        }
    }

    void DispatchEvent(vx_evt_media_stream_updated *evt)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> login = FindLoginBySessionGroupHandle(evt->sessiongroup_handle);
        CHECK_RET(login != NULL);
        login->HandleEvent(evt);
    }

    void DispatchEvent(vx_evt_participant_added *evt)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> login = FindLoginBySessionGroupHandle(evt->sessiongroup_handle);
        CHECK_RET(login != NULL);
        login->HandleEvent(evt);
    }

    void DispatchEvent(vx_evt_participant_updated *evt)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> login = FindLoginBySessionGroupHandle(evt->sessiongroup_handle);
        CHECK_RET(login != NULL);
        login->HandleEvent(evt);
    }

    void DispatchEvent(vx_evt_participant_removed *evt)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> login = FindLoginBySessionGroupHandle(evt->sessiongroup_handle);
        CHECK_RET(login != NULL);
        login->HandleEvent(evt);
    }

    void DispatchEvent(vx_evt_media_completion *evt)
    {
        // aux* requests will have no sessiongroup handle
        if (evt->sessiongroup_handle && evt->sessiongroup_handle[0]) {
            std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
            std::shared_ptr<SingleLoginMultiChannelManager> login = FindLoginBySessionGroupHandle(evt->sessiongroup_handle);
            CHECK_RET(login != NULL);
            login->HandleEvent(evt);
        } else {
            if (evt->completion_type == aux_buffer_audio_render) {
                if (m_audioInputDeviceTestIsPlayingBack) {
                    m_audioInputDeviceTestIsPlayingBack = false;
                    m_app->onAudioInputDeviceTestPlaybackCompleted();
                }
            }
        }
    }

    void DispatchEvent(vx_evt_audio_device_hot_swap *evt)
    {
        switch (evt->event_type) {
            case vx_audio_device_hot_swap_event_type_disabled_due_to_platform_constraints:
                RequestAudioInputDevices();
                RequestAudioOutputDevices();
                break;
            case vx_audio_device_hot_swap_event_type_active_render_device_changed:
                RequestAudioOutputDevices();
                break;
            case vx_audio_device_hot_swap_event_type_active_capture_device_changed:
                RequestAudioInputDevices();
                break;
#ifdef VIVOX_SDK_HAS_DEVICE_ADDED_REMOVED
            case vx_audio_device_hot_swap_event_type_audio_device_added:
            case vx_audio_device_hot_swap_event_type_audio_device_removed:
                RequestAudioInputDevices();
                RequestAudioOutputDevices();
                break;
#endif
            default:
                break;
        }
    }

    void DispatchEvent(vx_evt_sessiongroup_removed *evt)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> login = FindLoginBySessionGroupHandle(evt->sessiongroup_handle);
        CHECK_RET(login != NULL);
        login->HandleEvent(evt);
    }

    void DispatchEvent(vx_evt_transcribed_message *evt)
    {
        std::lock_guard<std::recursive_mutex> lock(m_loginsMutex);
        std::shared_ptr<SingleLoginMultiChannelManager> login = FindLoginBySessionGroupHandle(evt->sessiongroup_handle);
        CHECK_RET(login != NULL);
        login->HandleEvent(evt);
    }

    void DispatchEvent(vx_evt_base_t *evt)
    {
        switch (evt->type) {
            case evt_account_login_state_change:
                return DispatchEvent(reinterpret_cast<vx_evt_account_login_state_change *>(evt));
            case evt_media_stream_updated:
                return DispatchEvent(reinterpret_cast<vx_evt_media_stream_updated *>(evt));
            case evt_participant_added:
                return DispatchEvent(reinterpret_cast<vx_evt_participant_added *>(evt));
            case evt_participant_updated:
                return DispatchEvent(reinterpret_cast<vx_evt_participant_updated *>(evt));
            case evt_participant_removed:
                return DispatchEvent(reinterpret_cast<vx_evt_participant_removed *>(evt));
            case evt_media_completion:
                return DispatchEvent(reinterpret_cast<vx_evt_media_completion *>(evt));
            case evt_audio_device_hot_swap:
                return DispatchEvent(reinterpret_cast<vx_evt_audio_device_hot_swap *>(evt));
            case evt_sessiongroup_added:
                return;
            case evt_sessiongroup_removed:
                return DispatchEvent(reinterpret_cast<vx_evt_sessiongroup_removed *>(evt));
            case evt_session_added:
                return;
            case evt_session_removed:
                return;
            case evt_transcribed_message:
                return DispatchEvent(reinterpret_cast<vx_evt_transcribed_message *>(evt));
            default:
            {
                CHECK_RET(evt != NULL);
                char *xml = NULL;
                vx_event_to_xml(evt, &xml);
                LOG_INFO("Unhandled event: %s\n", xml);
            }
        }
    }

    void OnResponseOrEventFromSdkUiThread()
    {
        for (;;) {
            vx_message_base_t *m = NULL;
            vx_get_message(&m);
            if (m == 0) {
                break;
            }
            if (m->type == msg_response) {
                DispatchResponse(reinterpret_cast<vx_resp_base_t *>(m));
            } else {
                DispatchEvent(reinterpret_cast<vx_evt_base_t *>(m));
            }
            vx_destroy_message(m);
        }
    }

public:
    void SetAudioOutputDeviceMuted(bool value)
    {
        if (value != m_audioOutputDeviceMuted) {
            m_audioOutputDeviceMuted = value;
            vx_req_connector_mute_local_speaker_t *req;
            vx_req_connector_mute_local_speaker_create(&req);
            req->mute_level = value ? 1 : 0;
            vx_issue_request2(&req->base);
        }
    }

    bool GetAudioOutputDeviceMuted() const
    {
        return m_audioOutputDeviceMuted;
    }

    void SetAudioInputDeviceMuted(bool value)
    {
        if (value != m_audioInputDeviceMuted) {
            m_audioInputDeviceMuted = value;
            vx_req_connector_mute_local_mic_t *req;
            vx_req_connector_mute_local_mic_create(&req);
            req->mute_level = value ? 1 : 0;
            vx_issue_request2(&req->base);
        }
    }

    bool GetAudioInputDeviceMuted() const
    {
        return m_audioInputDeviceMuted;
    }

    ///
    /// Called by the application when the application entered the background.
    ///
    /// Mobile platforms only.
    ///
    void EnteredBackground()
    {
        vx_req_aux_notify_application_state_change *req;
        vx_req_aux_notify_application_state_change_create(&req);
        req->notification_type = vx_application_state_notification_type_before_background;
        issueRequest(&req->base);
    }

    ///
    /// Called by the application whether the application is about to enter the foreground
    ///
    /// Mobile platforms only.
    ///
    void WillEnterForeground()
    {
        vx_req_aux_notify_application_state_change *req;
        vx_req_aux_notify_application_state_change_create(&req);
        req->notification_type = vx_application_state_notification_type_after_foreground;
        issueRequest(&req->base);
    }

    ///
    /// Called by the application periodically when the application is in the background
    ///
    /// Mobile platforms only.
    ///
    void OnBackgroundIdleTimeout()
    {
        vx_req_aux_notify_application_state_change *req;
        vx_req_aux_notify_application_state_change_create(&req);
        req->notification_type = vx_application_state_notification_type_periodic_background_idle;
        issueRequest(&req->base);
    }

private:
    void WaitForShutdownResponse()
    {
        for (;;) {
            vx_message_base_t *m = NULL;
            vx_get_message(&m);
            if (m == 0) {
                break;
            }
            if (m->type == msg_response) {
                vx_resp_base_t *r = reinterpret_cast<vx_resp_base_t *>(m);
                if (r->type == resp_connector_initiate_shutdown) {
                    DispatchResponse(r);
                }
            }
            vx_destroy_message(m);
        }
    }

    IClientApiEventHandler *m_app;

    std::string m_application;

    Uri m_desiredServer;
    unsigned short m_desiredMinUdpPort;
    unsigned short m_desiredMaxUdpPort;
    ConnectorState m_desiredState;

    Uri m_currentServer;
    unsigned short m_currentMinUdpPort;
    unsigned short m_currentMaxUdpPort;
    ConnectorState m_currentState;

    std::string m_connectorHandle;

    std::recursive_mutex m_loginsMutex;
    std::map<AccountName, std::shared_ptr<SingleLoginMultiChannelManager> > m_logins;

    bool m_multiChannel;
    bool m_multiLogin;
    IClientApiEventHandler::LogLevel m_loglevel;

    std::vector<AudioDeviceId> m_audioOutputDeviceList;
    std::vector<AudioDeviceId> m_audioInputDeviceList;

    bool m_audioInputDeviceListPopulated;
    bool m_audioOutputDeviceListPopulated;

    AudioDeviceId m_defaultSystemAudioInputDevice;
    AudioDeviceId m_defaultSystemAudioOutputDevice;
    AudioDeviceId m_defaultCommunicationAudioInputDevice;
    AudioDeviceId m_defaultCommunicationAudioOutputDevice;

    AudioDevicePolicy m_currentAudioInputDevicePolicy;
    AudioDevicePolicy m_currentAudioOutputDevicePolicy;
    AudioDevicePolicy m_desiredAudioInputDevicePolicy;
    AudioDevicePolicy m_desiredAudioOutputDevicePolicy;

    int m_masterAudioInputDeviceVolume;
    int m_masterAudioOutputDeviceVolume;
    int m_desiredAudioInputDeviceVolume;
    int m_desiredAudioOutputDeviceVolume;

    bool m_masterAudioInputDeviceVolumeRequestInProgress;
    bool m_masterAudioOutputDeviceVolumeRequestInProgress;

    bool m_audioOutputDeviceTestIsRunning;
    bool m_audioInputDeviceTestIsRecording;
    bool m_audioInputDeviceTestIsPlayingBack;
    bool m_audioInputDeviceTestHasAudioToPlayback;

    bool m_audioInputDeviceMuted;
    bool m_audioOutputDeviceMuted;

    std::atomic<clock_t> m_clock;
    std::atomic<unsigned int> m_codecMask;

private:
    void ResetVariables()
    {
        // TODO: Clean up more variables.

        m_app = NULL;
        m_desiredState = ConnectorStateUninitialized;
        m_currentState = ConnectorStateUninitialized;
        m_multiChannel = false;
        m_multiLogin = false;
        m_audioInputDeviceListPopulated = false;
        m_audioOutputDeviceListPopulated = false;
        m_masterAudioInputDeviceVolume = 50;
        m_masterAudioOutputDeviceVolume = 50;
        m_desiredAudioInputDeviceVolume = 50;
        m_desiredAudioOutputDeviceVolume = 50;
        m_masterAudioInputDeviceVolumeRequestInProgress = false;
        m_masterAudioOutputDeviceVolumeRequestInProgress = false;
        m_audioOutputDeviceTestIsRunning = false;
        m_audioInputDeviceTestIsRecording = false;
        m_audioInputDeviceTestIsPlayingBack = false;
        m_audioInputDeviceTestHasAudioToPlayback = false;
        m_audioInputDeviceMuted = false;
        m_audioOutputDeviceMuted = false;

        // m_codecMask = vx_get_available_codecs_mask();
        m_codecMask = vx_get_default_codecs_mask();
    }
};

const UdpPortRange UdpPortRange::Default = {0, 0};

UdpPortRange::UdpPortRange(unsigned short minUdpPort, unsigned short maxUdpPort) :
    m_minUdpPort(minUdpPort),
    m_maxUdpPort(maxUdpPort)
{
    assert((!m_minUdpPort && !maxUdpPort) || m_minUdpPort < maxUdpPort);
}

ClientConnection::ClientConnection()
{
    m_pImpl = new ClientConnectionImpl();
}

ClientConnection::~ClientConnection()
{
    delete m_pImpl;
}

VCSStatus ClientConnection::Initialize(
        IClientApiEventHandler *app,
        IClientApiEventHandler::LogLevel logLevel,
        bool multiChannel,
        bool multiLogin,
        bool overrideAllocators,
        unsigned int codecMask,
        int &inputBuffers,
        int &outputBuffers,
        bool forceCaptureSilence
        )
{
    return m_pImpl->Initialize(app, logLevel, multiChannel, multiLogin, overrideAllocators, codecMask, inputBuffers, outputBuffers, forceCaptureSilence);
}

VCSStatus ClientConnection::Initialize(
        IClientApiEventHandler *app,
        IClientApiEventHandler::LogLevel logLevel,
        bool multiChannel,
        bool multiLogin,
        bool overrideAllocators,
        bool forceCaptureSilence
        )
{
    int inputBuffers = -1;
    int outputBuffers = -1;
    return m_pImpl->Initialize(app, logLevel, multiChannel, multiLogin, overrideAllocators, 0, inputBuffers, outputBuffers, forceCaptureSilence);
}

void ClientConnection::Uninitialize()
{
    return m_pImpl->Uninitialize();
}

void ClientConnection::SetCodecMask(unsigned int codecsMask)
{
    m_pImpl->SetCodecMask(codecsMask);
}

unsigned int ClientConnection::GetCodecMask() const
{
    return m_pImpl->GetCodecMask();
}

VCSStatus ClientConnection::Connect(const Uri &server, UdpPortRange udpPortRange)
{
    return m_pImpl->Connect(server, udpPortRange.m_minUdpPort, udpPortRange.m_maxUdpPort);
}

VCSStatus ClientConnection::Login(const AccountName &accountName, const char *accessToken, const char *captureDevice, const char *renderDevice, int participantUpdateFrequency)
{
    return m_pImpl->Login(accountName, accessToken, captureDevice, renderDevice, participantUpdateFrequency);
}

VCSStatus ClientConnection::Logout(const AccountName &accountName)
{
    return m_pImpl->Logout(accountName);
}

VCSStatus ClientConnection::JoinChannel(const AccountName &accountName, const Uri &channelUri, const char *channelAccessToken)
{
    return m_pImpl->JoinChannel(accountName, channelUri, channelAccessToken);
}

VCSStatus ClientConnection::LeaveChannel(const AccountName &accountName, const Uri &channelUri)
{
    return m_pImpl->LeaveChannel(accountName, channelUri);
}

VCSStatus ClientConnection::LeaveAll(const AccountName &accountName)
{
    return m_pImpl->LeaveAll(accountName);
}

void ClientConnection::Disconnect(const Uri &server)
{
    return m_pImpl->Disconnect(server);
}

VCSStatus ClientConnection::BlockUsers(const AccountName &accountName, const std::set<Uri> &usersToBlock)
{
    return m_pImpl->BlockUsers(accountName, usersToBlock);
}

VCSStatus ClientConnection::UnblockUsers(const AccountName &accountName, const std::set<Uri> &usersToUnblock)
{
    return m_pImpl->UnblockUsers(accountName, usersToUnblock);
}

VCSStatus ClientConnection::CheckBlockedUser(const AccountName &accountName, const Uri &user)
{
    return m_pImpl->CheckBlockedUser(accountName, user);
}

clock_t ClientConnection::GetClock()
{
    return m_pImpl->GetClock();
}

VCSStatus ClientConnection::IssueGetStats(const AccountName &accountName, bool reset)
{
    return m_pImpl->IssueGetStats(accountName, reset);
}

VCSStatus ClientConnection::StartPlayFileIntoChannels(const AccountName &accountName, const char *filename)
{
    return m_pImpl->StartPlayFileIntoChannels(accountName, filename);
}

VCSStatus ClientConnection::StopPlayFileIntoChannels(const AccountName &accountName)
{
    return m_pImpl->StopPlayFileIntoChannels(accountName);
}

VCSStatus ClientConnection::KickUser(const AccountName &accountName, const Uri &channelUri, const Uri &userUri, const char *accessToken)
{
    return m_pImpl->KickUser(accountName, channelUri, userUri, accessToken);
}

VCSStatus ClientConnection::MuteAll(const AccountName &accountName, const Uri &channelUri, bool set_muted,  const char *accessToken)
{
    return m_pImpl->MuteAll(accountName, channelUri, set_muted, accessToken);
}

// Audio Input Functions

const std::vector<AudioDeviceId> &ClientConnection::GetAvailableAudioInputDevices() const
{
    return m_pImpl->GetAudioInputDevices();
}

AudioDeviceId ClientConnection::GetAudioInputDevice() const
{
    return m_pImpl->GetAudioInputDevice();
}

const AudioDeviceId &ClientConnection::GetDefaultSystemAudioInputDevice() const
{
    return m_pImpl->GetDefaultSystemAudioInputDevice();
}

const AudioDeviceId &ClientConnection::GetDefaultCommunicationAudioInputDevice() const
{
    return m_pImpl->GetDefaultCommunicationAudioInputDevice();
}

VCSStatus ClientConnection::SetAudioInputDevice(const AudioDeviceId &deviceName)
{
    return m_pImpl->SetAudioInputDevice(deviceName);
}

void ClientConnection::UseDefaultSystemAudioInputDevice()
{
    m_pImpl->UseDefaultSystemAudioInputDevice();
}

void ClientConnection::UseDefaultCommunicationAudioInputDevice()
{
    m_pImpl->UseDefaultCommunicationAudioInputDevice();
}

bool ClientConnection::IsUsingDefaultSystemAudioInputDevice() const
{
    return m_pImpl->IsUsingDefaultSystemAudioInputDevice();
}

bool ClientConnection::IsUsingDefaultCommunicationAudioInputDevice() const
{
    return m_pImpl->IsUsingDefaultCommunicationAudioInputDevice();
}

// Audio Output Devices

const std::vector<AudioDeviceId> &ClientConnection::GetAvailableAudioOutputDevices() const
{
    return m_pImpl->GetAudioOutputDevices();
}

AudioDeviceId ClientConnection::GetAudioOutputDevice() const
{
    return m_pImpl->GetAudioOutputDevice();
}

const AudioDeviceId &ClientConnection::GetDefaultSystemAudioOutputDevice() const
{
    return m_pImpl->GetDefaultSystemAudioOutputDevice();
}

const AudioDeviceId &ClientConnection::GetDefaultCommunicationAudioOutputDevice() const
{
    return m_pImpl->GetDefaultCommunicationAudioOutputDevice();
}

bool ClientConnection::IsUsingDefaultSystemAudioOutputDevice() const
{
    return m_pImpl->IsUsingDefaultSystemAudioOutputDevice();
}

bool ClientConnection::IsUsingDefaultCommunicationAudioOutputDevice() const
{
    return m_pImpl->IsUsingDefaultCommunicationAudioOutputDevice();
}

VCSStatus ClientConnection::SetAudioOutputDevice(const AudioDeviceId &deviceName)
{
    return m_pImpl->SetAudioOutputDevice(deviceName);
}

void ClientConnection::UseDefaultSystemAudioOutputDevice()
{
    m_pImpl->UseDefaultSystemAudioOutputDevice();
}

void ClientConnection::UseDefaultCommunicationAudioOutputDevice()
{
    m_pImpl->UseDefaultCommunicationAudioOutputDevice();
}

int ClientConnection::GetMasterAudioInputDeviceVolume() const
{
    return m_pImpl->GetMasterAudioInputDeviceVolume();
}
VCSStatus ClientConnection::SetMasterAudioInputDeviceVolume(int volume)
{
    return m_pImpl->SetMasterAudioInputDeviceVolume(volume);
}
int ClientConnection::GetMasterAudioOutputDeviceVolume() const
{
    return m_pImpl->GetMasterAudioOutputDeviceVolume();
}
VCSStatus ClientConnection::SetMasterAudioOutputDeviceVolume(int volume)
{
    return m_pImpl->SetMasterAudioOutputDeviceVolume(volume);
}

int ClientConnection::GetChannelAudioOutputDeviceVolume(const AccountName &accountName, const Uri &channelUri) const
{
    return m_pImpl->GetChannelAudioOutputDeviceVolume(accountName, channelUri);
}

VCSStatus ClientConnection::SetChannelAudioOutputDeviceVolume(const AccountName &accountName, const Uri &channelUri, int volume)
{
    return m_pImpl->SetChannelAudioOutputDeviceVolume(accountName, channelUri, volume);
}

int ClientConnection::GetParticipantAudioOutputDeviceVolumeForMe(const AccountName &accountName, const Uri &target, const Uri &channelUri) const
{
    return m_pImpl->GetParticipantAudioOutputDeviceVolumeForMe(accountName, target, channelUri);
}

VCSStatus ClientConnection::SetParticipantAudioOutputDeviceVolumeForMe(const AccountName &accountName, const Uri &target, const Uri &channelUri, int volume)
{
    return m_pImpl->SetParticipantAudioOutputDeviceVolumeForMe(accountName, target, channelUri, volume);
}

VCSStatus ClientConnection::SetParticipantMutedForAll(const AccountName &accountName, const Uri &target, const Uri &channelUri, bool muted, const char *accessToken)
{
    return m_pImpl->SetParticipantMutedForAll(accountName, target, channelUri, muted, accessToken);
}

bool ClientConnection::GetParticipantMutedForAll(const AccountName &accountName, const Uri &targetUser, const Uri &channelUri) const
{
    return m_pImpl->GetParticipantMutedForAll(accountName, targetUser, channelUri);
}


VCSStatus ClientConnection::SetParticipantMutedForMe(const AccountName &accountName, const Uri &target, const Uri &channelUri, bool muted)
{
    return m_pImpl->SetParticipantMutedForMe(accountName, target, channelUri, muted);
}

ChannelTransmissionPolicy ClientConnection::GetChannelTransmissionPolicy(const AccountName &accountName) const
{
    return m_pImpl->GetChannelTransmissionPolicy(accountName);
}

VCSStatus ClientConnection::SetTransmissionToSpecificChannel(const AccountName &accountName, const Uri &channelUri)
{
    return m_pImpl->SetTransmissionToSpecificChannel(accountName, channelUri);
}

VCSStatus ClientConnection::Set3DPosition(const AccountName &accountName, const Uri &channel, double x, double y, double z, double at_x, double at_y, double at_z)
{
    return m_pImpl->Set3DPosition(accountName, channel, x, y, z, at_x, at_y, at_z);
}

VCSStatus ClientConnection::SetTransmissionToAll(const AccountName &accountName)
{
    return m_pImpl->SetTransmissionToAll(accountName);
}

VCSStatus ClientConnection::SetTransmissionToNone(const AccountName &accountName)
{
    return m_pImpl->SetTransmissionToNone(accountName);
}

VCSStatus ClientConnection::StartAudioOutputDeviceTest(const char *filename)
{
    return m_pImpl->StartAudioOutputDeviceTest(filename);
}

void ClientConnection::StopAudioOutputDeviceTest()
{
    return m_pImpl->StopAudioOutputDeviceTest();
}

bool ClientConnection::AudioOutputDeviceTestIsRunning() const
{
    return m_pImpl->AudioOutputDeviceTestIsRunning();
}

VCSStatus ClientConnection::StartAudioInputDeviceTestRecord()
{
    return m_pImpl->StartAudioInputDeviceTestRecord();
}

void ClientConnection::StopAudioInputDeviceTestRecord()
{
    return m_pImpl->StopAudioInputDeviceTestRecord();
}

VCSStatus ClientConnection::StartAudioInputDeviceTestPlayback()
{
    return m_pImpl->StartAudioInputDeviceTestPlayback();
}

void ClientConnection::StopAudioInputDeviceTestPlayback()
{
    return m_pImpl->StopAudioInputDeviceTestPlayback();
}

bool ClientConnection::AudioInputDeviceTestIsRecording() const
{
    return m_pImpl->AudioInputDeviceTestIsRecording();
}

bool ClientConnection::AudioInputDeviceTestIsPlayingBack() const
{
    return m_pImpl->AudioInputDeviceTestIsPlayingBack();
}

bool ClientConnection::AudioInputDeviceTestHasAudioToPlayback() const
{
    return m_pImpl->AudioInputDeviceTestHasAudioToPlayback();
}

void ClientConnection::SetAudioOutputDeviceMuted(bool value)
{
    return m_pImpl->SetAudioOutputDeviceMuted(value);
}

bool ClientConnection::GetAudioOutputDeviceMuted() const
{
    return m_pImpl->GetAudioOutputDeviceMuted();
}

void ClientConnection::SetAudioInputDeviceMuted(bool value)
{
    return m_pImpl->SetAudioInputDeviceMuted(value);
}

bool ClientConnection::GetAudioInputDeviceMuted() const
{
    return m_pImpl->GetAudioInputDeviceMuted();
}

void ClientConnection::EnteredBackground()
{
    m_pImpl->EnteredBackground();
}

void ClientConnection::WillEnterForeground()
{
    m_pImpl->WillEnterForeground();
}

void ClientConnection::OnBackgroundIdleTimeout()
{
    m_pImpl->OnBackgroundIdleTimeout();
}

VCSStatus ClientConnection::SetSttTranscriptionOn(const AccountName &accountName, const Uri &channel, bool on, const char *accessToken)
{
    return m_pImpl->SetSttTranscriptionOn(accountName, channel, on, accessToken);
}
}
