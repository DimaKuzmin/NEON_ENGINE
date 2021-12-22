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

#include "vivoxclientsdk.h"
#include <deque>
#include <mutex>

#define ARGUNUSED(x)

std::string NowString();
void doSleep(size_t us);

using namespace VivoxClientApi;

class EasyApp :
    public IClientApiEventHandler
{
public:
    typedef struct {
        void (*pf_func)(void *arg0);
        void *arg0;
    } Delegate;

    // Constructor
    EasyApp()
    {
        m_running = true;
        m_forceCaptureSilence = false;
    }

    virtual const char *ParticipantLeftString(ParticipantLeftReason reason);

    // New Streamlined Functions For Connecting and Logging In
    virtual int Initialize(IClientApiEventHandler::LogLevel logLevel = LogLevelNone);
    virtual void Uninitialize();
    void SetCodecMask(unsigned int codecsMask) { m_sdk.SetCodecMask(codecsMask); }
    unsigned int GetCodecMask() const { return m_sdk.GetCodecMask(); }
    virtual int Connect(const char *serverName, UdpPortRange udpPortRange = UdpPortRange::Default);
    virtual void Disconnect(const char *serverName);
    virtual int Login(const char *accountName, const char *credentials, const char *captureDevice = NULL, const char *renderDevice = NULL, int participantUpdateFrequency = -1);
    virtual int Logout(const char *accountName);
    virtual int Join(const char *accountName, const char *channelName, const char *accessToken = NULL);
    virtual int Leave(const char *accountName, const char *channelName);
    virtual int LeaveAll(const char *accountName);
    virtual int UpdateLoop(int argc, char *argv[], bool once);
    int BlockUsers(const char *accountName, const std::set<Uri> &usersToBlock);
    int UnblockUsers(const char *accountName, const std::set<Uri> &usersToBlock);
    int CheckBlockedUser(const char *accountName, const char *userUri);
    int PlayFile(const char *accountName, const char *fileName);
    int StopPlayingFiles(const char *accountName);
    int Kick(const char *accountName, const char *channelName, const char *kickName, const char *accessToken = NULL);
    int Mute(const char *accountName, const char *channelName, const char *muteName, int set_muted, const char *accessToken = NULL);
    int MuteAll(const char *accountName, const char *channelName, bool set_muted, const char *accessToken = NULL);
    int SetTransmissionToSpecificChannel(const char *accountName, const char *channelUri);
    int SetTransmissionToAll(const char *accountName);
    int SetTransmissionToNone(const char *accountName);

    void InvokeOnUIThread(void (*pf_func)(void *arg0), void *arg0);

    virtual void onLogStatementEmitted(LogLevel level, long long nativeMillisecondsSinceEpoch, long threadId, const char *logMessage);
    virtual void onAssert(const char *filename, int line, const char *message);

    virtual const char *GetAppName() const { return "EASYAPP"; }

    // Service Connection
    virtual void onConnectCompleted(const Uri &server);
    virtual void onConnectCompletedEx(const Uri & /*server*/, vx_backend_type /*backendType*/, const char * /*connectorHandle*/) { /* NOT_IMPLEMENTED */ }
    virtual void onConnectFailed(const Uri &server, VCSStatus status);
    virtual void onDisconnected(const Uri &server, VCSStatus status);

    // Logging/Logging out
    virtual void onLoginCompleted(const AccountName &accountName);
    virtual void onLoginCompletedEx(const AccountName & /*accountName*/, const char * /*accountHandle*/) { /* NOT_IMPLEMENTED */ }
    virtual void onInvalidLoginCredentials(const AccountName &accountName);
    virtual void onLoginFailed(const AccountName &accountName, VCSStatus status);
    virtual void onLogoutCompleted(const AccountName &accountName);
    virtual void onLogoutFailed(const AccountName &accountName, VCSStatus status);

    // Getting into/out of channel
    virtual void onChannelJoined(const AccountName &accountName, const Uri &channelUri);
    virtual void onChannelJoinedEx(const AccountName & /*accountName*/, const Uri & /*channelUri*/, const char * /*sessionGroupHandle*/, const char * /*sessionHandle*/) { /* NOT_IMPLEMENTED */ }
    virtual void onInvalidChannelCredentials(const AccountName &accountName, const Uri &channelUri);
    virtual void onChannelJoinFailed(const AccountName &accountName, const Uri &channelUri, VCSStatus status);
    virtual void onChannelExited(const AccountName &accountName, const Uri &channelUri, VCSStatus status);

    // Roster list maintenance
    virtual void onParticipantAdded(const AccountName &accountName, const Uri &channelUri, const Uri &participantUri, bool isLoggedInUser);
    virtual void onParticipantLeft(const AccountName &accountName, const Uri &channelUri, const Uri &participantUri, bool isLoggedInUser, ParticipantLeftReason reason);
    virtual void onParticipantUpdated(const AccountName &accountName, const Uri &channelUri, const Uri &participantUri, bool isLoggedInUser, bool speaking, double vuMeterEnergy, bool isMutedForAll);

    // Moderation and Audio Injection
    virtual void onParticipantKickedCompleted(const AccountName &accountName, const Uri &channelUri, const Uri &participantUri);
    virtual void onParticipantKickFailed(const AccountName &accountName, const Uri &channelUri, const Uri &participantUri, VCSStatus status);
    virtual void onSetParticipantMutedForAllCompleted(const AccountName &accountName, const Uri &target, const Uri &channelUri, bool muted);
    virtual void onSetParticipantMutedForAllFailed(const AccountName &accountName, const Uri &target, const Uri &channelUri, bool muted, VCSStatus status);
    virtual void onMuteAllFailed(const AccountName &accountName, const Uri &channelUri, bool muted, VCSStatus status);
    virtual void onMuteAllCompleted(const AccountName &accountName, const Uri &channelUri, bool muted);
    virtual void onStartPlayFileIntoChannels(const AccountName &accountName, const char *filename);
    virtual void onStartPlayFileIntoChannelsFailed(const AccountName &accountName, const char *filename, VCSStatus status);
    virtual void onPlayFileIntoChannelsStopped(const AccountName &accountName, const char *filename) { /* NOT_IMPLEMENTED */ (void)accountName; (void)filename; }

    virtual void onGetStats(vx_resp_sessiongroup_get_stats *ARGUNUSED(resp)) { /* NOT_IMPLEMENTED */ }
    virtual void onCallStatsUpdated(const AccountName &ARGUNUSED(accountName), vx_call_stats_t &ARGUNUSED(stats), bool ARGUNUSED(isFinal)) { /* NOT_IMPLEMENTED */ }

    virtual void onSessionGroupRemoved(const AccountName &accountName, const char *sessionGroupHandle);
    virtual void onAvailableAudioDevicesChanged();
    virtual void onDefaultSystemAudioInputDeviceChanged(const AudioDeviceId &deviceId);
    virtual void onDefaultCommunicationAudioInputDeviceChanged(const AudioDeviceId &deviceId);
    virtual void onSetAudioInputDeviceCompleted(const AudioDeviceId &deviceId);
    virtual void onSetAudioInputDeviceFailed(const AudioDeviceId &deviceId, VCSStatus status);
    virtual void onDefaultSystemAudioOutputDeviceChanged(const AudioDeviceId &deviceId);
    virtual void onDefaultCommunicationAudioOutputDeviceChanged(const AudioDeviceId &deviceId);
    virtual void onSetAudioOutputDeviceCompleted(const AudioDeviceId &deviceId);
    virtual void onSetAudioOutputDeviceFailed(const AudioDeviceId &deviceId, VCSStatus status);
    virtual void onSetChannelAudioOutputDeviceVolumeCompleted(const AccountName &accountName, const Uri &channelUri, int volume);
    virtual void onSetChannelAudioOutputDeviceVolumeFailed(const AccountName &accountName, const Uri &channelUri, int volume, VCSStatus status);
    virtual void onSetParticipantAudioOutputDeviceVolumeForMeCompleted(const AccountName &accountName, const Uri &targetUser, const Uri &channelUri, int volume);
    virtual void onSetParticipantAudioOutputDeviceVolumeForMeFailed(const AccountName &accountName, const Uri &targetUser, const Uri &channelUri, int volume, VCSStatus status);
    virtual void onSetParticipantMutedForMeCompleted(const AccountName &accountName, const Uri &target, const Uri &channelUri, bool muted);
    virtual void onSetParticipantMutedForMeFailed(const AccountName &accountName, const Uri &target, const Uri &channelUri, bool muted, VCSStatus status);
    virtual void onSetChannelTransmissionToSpecificChannelCompleted(const AccountName &accountName, const Uri &channelUri);
    virtual void onSetChannelTransmissionToSpecificChannelFailed(const AccountName &accountName, const Uri &channelUri, VCSStatus status);
    virtual void onSetChannelTransmissionToAllCompleted(const AccountName &accountName);
    virtual void onSetChannelTransmissionToAllFailed(const AccountName &accountName, VCSStatus status);
    virtual void onSetChannelTransmissionToNoneCompleted(const AccountName &accountName);
    virtual void onSetChannelTransmissionToNoneFailed(const AccountName &accountName, VCSStatus status);
    virtual void onAudioInputDeviceTestPlaybackCompleted();
    virtual void onSttTranscriptionReceived(const AccountName &accountName, const Uri &userUri, const Uri &channelUri, const char *text, const char *language, const char *displayName);

    void SetCaptureSilence(bool forceCaptureSilence) { m_forceCaptureSilence = forceCaptureSilence; }

protected:
    bool m_running;
    std::deque<Delegate> m_delegateQueue;
    ClientConnection m_sdk;
    void Stop() { m_running = false; }
    std::mutex m_mutex;
    Delegate fetchNextDelegateUnlocked();
    bool isDelegatePendingUnlocked() { return !m_delegateQueue.empty(); }
    void callAllPendingDelegates();
    bool m_forceCaptureSilence;
};
