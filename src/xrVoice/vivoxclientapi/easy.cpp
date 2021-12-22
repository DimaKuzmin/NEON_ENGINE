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

#include <chrono>
#include <iomanip>
#include <sstream>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "easy.h"

#include <Windows.h>

#   define LOG_ERR(...) fprintf(stderr, __VA_ARGS__)
#   define LOG_INFO(...) fprintf(stdout, __VA_ARGS__)

std::string NowString()
{
    char buf[80];
    SYSTEMTIME lt;
    GetLocalTime(&lt);
    sprintf(buf, "%02d:%02d:%02d.%03d", lt.wHour, lt.wMinute, lt.wSecond, lt.wMilliseconds);
    return buf;
}

void doSleep(size_t us)
{
    SleepEx((DWORD)(us / 1000), TRUE);
}

const char *EasyApp::ParticipantLeftString(ParticipantLeftReason code)
{
    switch (code) {
        case 0:
            return "Left";
        case 1:
            return "Network";
        case 2:
            return "Kicked";
        case 3:
            return "Banned";
        default:
            return "Unknown";
    }
}

// Initialize Call
int EasyApp::Initialize(IClientApiEventHandler::LogLevel logLevel)
{
    VCSStatus status = m_sdk.Initialize(this, logLevel, true, true, true, m_forceCaptureSilence);
    if (status != 0) {
        LOG_ERR("m_sdk.Initialize() returned (%d) %s\n", status, GetErrorString(status));
        return status;
    }
    return 0;
}

// Unitialize Call
void EasyApp::Uninitialize()
{
    m_sdk.Uninitialize();
}

// Connect to Server
int EasyApp::Connect(const char *serverName, UdpPortRange udpPortRange)
{
    Uri server(serverName);
    LOG_INFO("%s: Connecting to %s\n", NowString().c_str(), server.ToString());

    VCSStatus status = m_sdk.Connect(server, udpPortRange);
    if (status != 0) {
        LOG_ERR("m_sdk.Connect(%s) returned (%d) %s\n", server.ToString(), status, GetErrorString(status));
        return status;
    }
    return 0;
}

// Disconnect from Server
void EasyApp::Disconnect(const char *serverName)
{
    Uri server(serverName);
    LOG_INFO("%s: Disconnecting from %s\n", NowString().c_str(), server.ToString());

    m_sdk.Disconnect(server);
}

// Login User
int EasyApp::Login(const char *accountName, const char *credentials, const char *captureDevice, const char *renderDevice, int participantUpdateFrequency)
{
    AccountName account(accountName);
    VCSStatus status = m_sdk.Login(account, credentials, captureDevice, renderDevice, participantUpdateFrequency);
    if (status != 0) {
        LOG_ERR("m_sdk.Login(%s) returned (%d) %s\n", account.ToString(), status, GetErrorString(status));
        return status;
    }
    return 0;
}

// Logout User
int EasyApp::Logout(const char *accountName)
{
    AccountName account(accountName);
    VCSStatus status = m_sdk.Logout(account);
    if (status != 0) {
        LOG_ERR("m_sdk.Logout(%s) returned (%d) %s\n", account.ToString(), status, GetErrorString(status));
        return status;
    }
    return 0;
}

// Join a Channel
int EasyApp::Join(const char *accountName, const char *channelName, const char *accessToken)
{
    AccountName account(accountName);
    Uri channel(channelName);
    VCSStatus status = m_sdk.JoinChannel(account, channel, accessToken);
    if (status != 0) {
        LOG_ERR("m_sdk.JoinChannel(%s) returned (%d) %s\n", channel.ToString(), status, GetErrorString(status));
        return status;
    }
    return 0;
}

// Leave a Channel
int EasyApp::Leave(const char *accountName, const char *channelName)
{
    AccountName account(accountName);
    Uri channel(channelName);
    VCSStatus status = m_sdk.LeaveChannel(account, channel);
    if (status != 0) {
        LOG_ERR("m_sdk.LeaveChannel(%s) returned (%d) %s\n", channel.ToString(), status, GetErrorString(status));
        return status;
    }
    return 0;
}

// Leave all Channels
int EasyApp::LeaveAll(const char *accountName)
{
    AccountName account(accountName);
    VCSStatus status = m_sdk.LeaveAll(account);
    if (status != 0) {
        LOG_ERR("m_sdk.LeaveAll(%s) returned (%d) %s\n", account.ToString(), status, GetErrorString(status));
        return status;
    }
    return 0;
}

EasyApp::Delegate EasyApp::fetchNextDelegateUnlocked()
{
    Delegate d = { 0 };
    if (!m_delegateQueue.empty()) {
        d = m_delegateQueue.front();
        m_delegateQueue.pop_front();
    }
    return d;
}

void EasyApp::callAllPendingDelegates()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    while (isDelegatePendingUnlocked()) {
        Delegate d = fetchNextDelegateUnlocked();
        lock.unlock();
        d.pf_func(d.arg0);
        lock.lock();
    }
}

// Main Update Loop
int EasyApp::UpdateLoop(int /* argc */, char * /* argv */[], bool once)
{
    while (m_running) {
        callAllPendingDelegates();
        if (once) {
            break;
        }
        doSleep(30);
    }
    return 0;
}

// Block a List of Users
int EasyApp::BlockUsers(const char *accountName, const std::set<Uri> &usersToBlock)
{
    AccountName account(accountName);
    VCSStatus status = m_sdk.BlockUsers(account, usersToBlock);
    if (status != 0) {
        LOG_ERR("m_sdk.BlockUsers(%s) returned (%d) %s\n", accountName, status, GetErrorString(status));
        return status;
    }
    return 0;
}

// Unblock a List of Users
int EasyApp::UnblockUsers(const char *accountName, const std::set<Uri> &usersToBlock)
{
    AccountName account(accountName);
    VCSStatus status = m_sdk.UnblockUsers(account, usersToBlock);
    if (status != 0) {
        LOG_ERR("m_sdk.UnblockUsers(%s) returned (%d) %s\n", accountName, status, GetErrorString(status));
        return status;
    }
    return 0;
}

// Check if a user is on an account's blocked list
int EasyApp::CheckBlockedUser(const char *accountName, const char *userUri)
{
    AccountName account(accountName);
    VCSStatus status = m_sdk.CheckBlockedUser(account, Uri(userUri));
    if (status != 0 && status != 1) {
        LOG_ERR("m_sdk.CheckBlockedUser(%s) returned (%d) %s\n", accountName, status, GetErrorString(status));
    }
    return status;
}

// Play an audio file into connected channels, originating From a User
int EasyApp::PlayFile(const char *accountName, const char *fileName)
{
    AccountName account(accountName);
    VCSStatus status = m_sdk.StartPlayFileIntoChannels(account, fileName);
    if (status != 0) {
        LOG_ERR("m_sdk.StartPlayFileIntoChannel(%s) returned (%d) %s\n", fileName, status, GetErrorString(status));
        return status;
    }
    return 0;
}

// Stop all playing audio files originating From a User
int EasyApp::StopPlayingFiles(const char *accountName)
{
    AccountName account(accountName);
    VCSStatus status = m_sdk.StopPlayFileIntoChannels(account);
    if (status != 0) {
        LOG_ERR("m_sdk.StopPlayFileIntoChannel(%s) returned (%d) %s\n", accountName, status, GetErrorString(status));
        return status;
    }
    return 0;
}

// Kick a user from a channel
int EasyApp::Kick(const char *accountName, const char *channelName, const char *kickName, const char *accessToken)
{
    AccountName account(accountName);
    Uri channel(channelName);
    Uri kickUri(kickName);
    VCSStatus status = m_sdk.KickUser(account, channel, kickUri, accessToken);
    if (status != 0) {
        LOG_ERR("m_sdk.KickUser(%s) returned (%d) %s\n", kickName, status, GetErrorString(status));
        return status;
    }
    return 0;
}

// Mute or unmute a user in a channel for all users
int EasyApp::Mute(const char *accountName, const char *channelName, const char *muteName, int set_muted, const char *accessToken)
{
    AccountName account(accountName);
    Uri channel(channelName);
    Uri muteUri(muteName);
    VCSStatus status = m_sdk.SetParticipantMutedForAll(account, muteUri, channel, set_muted != 0, accessToken);
    if (status != 0) {
        LOG_ERR("m_sdk.SetParticipantMutedForAll(%s) returned (%d) %s\n", muteName, status, GetErrorString(status));
        return status;
    }
    return 0;
}

// Mute or unmute all current users in a channel for all users
int EasyApp::MuteAll(const char *accountName, const char *channelName, bool set_muted, const char *accessToken)
{
    AccountName account(accountName);
    Uri channel(channelName);
    VCSStatus status = m_sdk.MuteAll(account, channel, set_muted, accessToken);
    if (status != 0) {
        LOG_ERR("m_sdk.MuteAll() returned (%d) %s\n", status, GetErrorString(status));
        return status;
    }
    return 0;
}

int EasyApp::SetTransmissionToSpecificChannel(const char *accountName, const char *channelUri)
{
    AccountName account(accountName);
    Uri channel(channelUri);
    VCSStatus status = m_sdk.SetTransmissionToSpecificChannel(account, channel);
    if (status != 0) {
        LOG_ERR("m_sdk.SetTransmissionToSpecificChannel() returned (%d) %s\n", status, GetErrorString(status));
        return status;
    }
    return 0;
}

int EasyApp::SetTransmissionToAll(const char *accountName)
{
    AccountName account(accountName);
    VCSStatus status = m_sdk.SetTransmissionToAll(account);
    if (status != 0) {
        LOG_ERR("m_sdk.SetTransmissionToAll() returned (%d) %s\n", status, GetErrorString(status));
        return status;
    }
    return 0;
}

int EasyApp::SetTransmissionToNone(const char *accountName)
{
    AccountName account(accountName);
    VCSStatus status = m_sdk.SetTransmissionToNone(account);
    if (status != 0) {
        LOG_ERR("m_sdk.SetTransmissionToNone() returned (%d) %s\n", status, GetErrorString(status));
        return status;
    }
    return 0;
}

void EasyApp::InvokeOnUIThread(void (*pf_func)(void *arg0), void *arg0)
{
    Delegate d;
    d.pf_func = pf_func;
    d.arg0 = arg0;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_delegateQueue.push_back(d);
}

void EasyApp::onLogStatementEmitted(
        LogLevel level,
        long long nativeMillisecondsSinceEpoch,
        long threadId,
        const char *logMessage)
{
    (void)nativeMillisecondsSinceEpoch;
    (void)threadId;
    LOG_INFO("%s: %d %s\r\n", NowString().c_str(), level, logMessage);
}

void EasyApp::onAssert(const char *filename, int line, const char *message)
{
    LOG_INFO("%s: %s(%d): ASSERT FAILED: %s\n", NowString().c_str(), filename, line, message);
    DebugBreak();
}

// Service Connection
void EasyApp::onConnectCompleted(const Uri &server)
{
    LOG_INFO("%s: Connected to %s\n", NowString().c_str(), server.ToString());
}
void EasyApp::onConnectFailed(const Uri &server, VCSStatus status)
{
    LOG_INFO("%s: Failed to connect to %s, error %s\n", NowString().c_str(), server.ToString(), GetErrorString(status));
}
void EasyApp::onDisconnected(const Uri &server, VCSStatus status)
{
    if (status == 0) {
        LOG_INFO("%s: Disconnected from %s\n", NowString().c_str(), server.ToString());
    } else {
        LOG_INFO("%s: Disconnected from %s, error %s(%d)\n", NowString().c_str(), server.ToString(), GetErrorString(status), status);
    }
}

// Logging/Logging out
void EasyApp::onLoginCompleted(const AccountName &accountName)
{
    LOG_INFO("%s: Logged in %s\n", NowString().c_str(), accountName.ToString());
}
void EasyApp::onInvalidLoginCredentials(const AccountName &accountName)
{
    LOG_INFO("%s: Invalid Login Credentials for %s\n", NowString().c_str(), accountName.ToString());
}
void EasyApp::onLoginFailed(const AccountName &accountName, VCSStatus status)
{
    LOG_INFO("%s: Login Failed for %s, error %s (%d)\n", NowString().c_str(), accountName.ToString(), GetErrorString(status), status);
}
void EasyApp::onLogoutCompleted(const AccountName &accountName)
{
    LOG_INFO("%s: Logged out %s\n", NowString().c_str(), accountName.ToString());
}
void EasyApp::onLogoutFailed(const AccountName &accountName, VCSStatus status)
{
    LOG_INFO("%s: Logout Failed for %s, error %s\n", NowString().c_str(), accountName.ToString(), GetErrorString(status));
}

// Getting into/out of channel
void EasyApp::onChannelJoined(const AccountName &accountName, const Uri &channelUri)
{
    LOG_INFO("%s: %s connected to %s\n", NowString().c_str(), accountName.ToString(), channelUri.ToString());
}
void EasyApp::onInvalidChannelCredentials(const AccountName &accountName, const Uri &channelUri)
{
    LOG_INFO("%s: %s provided invalid credentials for %s\n", NowString().c_str(), accountName.ToString(), channelUri.ToString());
}
void EasyApp::onChannelJoinFailed(const AccountName &accountName, const Uri &channelUri, VCSStatus status)
{
    LOG_INFO("%s: %s failed to join %s, error %s (%d)\n", NowString().c_str(), accountName.ToString(), channelUri.ToString(), GetErrorString(status), status);
}
void EasyApp::onChannelExited(const AccountName &accountName, const Uri &channelUri, VCSStatus status)
{
    LOG_INFO(
            "%s: Channel Exited by %s, channel uri %s, with status (%d) %s\n",
            NowString().c_str(),
            accountName.ToString(),
            channelUri.ToString(),
            status,
            GetErrorString(status));
}
// Roster list maintenance
void EasyApp::onParticipantAdded(const AccountName &accountName, const Uri &channelUri, const Uri &participantUri, bool isLoggedInUser)
{
    LOG_INFO("%s: %s saw %s join %s (isLoggedInUser = %s)\n", NowString().c_str(), accountName.ToString(), participantUri.ToString(), channelUri.ToString(), isLoggedInUser ? "true" : "false");
}
void EasyApp::onParticipantLeft(const AccountName &accountName, const Uri &channelUri, const Uri &participantUri, bool isLoggedInUser, ParticipantLeftReason reason)
{
    LOG_INFO("%s: %s saw %s leave %s for reason %s (isLoggedInUser = %s)\n", NowString().c_str(), accountName.ToString(), participantUri.ToString(), channelUri.ToString(), ParticipantLeftString(reason), isLoggedInUser ? "true" : "false");
}
void EasyApp::onParticipantUpdated(const AccountName &accountName, const Uri &channelUri, const Uri &participantUri, bool isLoggedInUser, bool speaking, double vuMeterEnergy, bool isMutedForAll)
{
    LOG_INFO(
            "%s: %s saw %s %s speaking in %s, isMutedForAll is %s, vuMeterEnergy is %lf (isLoggedInUser = %s)\n",
            NowString().c_str(),
            accountName.ToString(),
            participantUri.ToString(),
            speaking ? "start" : "stop",
            channelUri.ToString(),
            isMutedForAll ? "true" : "false",
            vuMeterEnergy,
            isLoggedInUser ? "true" : "false"
            );
}

// Moderation
void EasyApp::onParticipantKickedCompleted(const AccountName &accountName, const Uri &channelUri, const Uri &participantUri)
{
    LOG_INFO("%s: %s: Participant %s kicked from %s\n", NowString().c_str(), accountName.ToString(), participantUri.ToString(), channelUri.ToString());
}
void EasyApp::onParticipantKickFailed(const AccountName &accountName, const Uri &channelUri, const Uri &participantUri, VCSStatus status)
{
    LOG_INFO("%s: %s: Participant %s kick from %s failed for reason: (%d) %s\n", NowString().c_str(), accountName.ToString(), participantUri.ToString(), channelUri.ToString(), status, GetErrorString(status));
}
void EasyApp::onSetParticipantMutedForAllCompleted(const AccountName &accountName, const Uri &target, const Uri &channelUri, bool muted)
{
    LOG_INFO("%s: %s: Participant %s %s for all in %s\n", NowString().c_str(), accountName.ToString(), target.ToString(), muted ? "Muted" : "Unmuted", channelUri.ToString());
}
void EasyApp::onSetParticipantMutedForAllFailed(const AccountName &accountName, const Uri &target, const Uri &channelUri, bool muted, VCSStatus status)
{
    LOG_INFO("%s: %s: Participant %s %s for all in %s failed for reason: (%d) %s\n", NowString().c_str(), accountName.ToString(), target.ToString(), muted ? "Mute" : "Unmute", channelUri.ToString(), status, GetErrorString(status));
}
void EasyApp::onMuteAllCompleted(const AccountName &accountName, const Uri &channelUri, bool muted)
{
    LOG_INFO("%s: %s: All Participants in %s %s\n", NowString().c_str(), accountName.ToString(), channelUri.ToString(), muted ? "Muted" : "Unmuted");
}
void EasyApp::onMuteAllFailed(const AccountName &accountName, const Uri &channelUri, bool muted, VCSStatus status)
{
    LOG_INFO("%s: %s: %s All Participants in %s Failed for reason: (%d) %s\n", NowString().c_str(), accountName.ToString(), muted ? "Muting" : "Unmuting", channelUri.ToString(), status, GetErrorString(status));
}

// Audio Injection
void EasyApp::onStartPlayFileIntoChannels(const AccountName &accountName, const char *filename)
{
    LOG_INFO("%s: %s: Start Play File %s Into Channels was successful\n", NowString().c_str(), accountName.ToString(), filename);
}
void EasyApp::onStartPlayFileIntoChannelsFailed(const AccountName &accountName, const char *filename, VCSStatus status)
{
    LOG_INFO("%s: %s: Start Play File %s Into Channels failed for reason: (%d) %s\n", NowString().c_str(), accountName.ToString(), filename, status, GetErrorString(status));
}

void EasyApp::onSessionGroupRemoved(const AccountName &accountName, const char *sessionGroupHandle)
{
    LOG_INFO("%s: %s: Session Group %s removed\n", NowString().c_str(), accountName.ToString(), sessionGroupHandle);
}

void EasyApp::onAvailableAudioDevicesChanged()
{
    LOG_INFO("%s: Available Audio Devices Changed\n", NowString().c_str());
}

void EasyApp::onDefaultSystemAudioInputDeviceChanged(const AudioDeviceId &deviceId)
{
    LOG_INFO("%s: Default System Audio Input Device Changed to %s\n", NowString().c_str(), deviceId.ToString().c_str());
}

void EasyApp::onDefaultCommunicationAudioInputDeviceChanged(const AudioDeviceId &deviceId)
{
    LOG_INFO("%s: Default Communication Audio Input Device Changed to %s\n", NowString().c_str(), deviceId.ToString().c_str());
}

void EasyApp::onSetAudioInputDeviceCompleted(const AudioDeviceId &deviceId)
{
    LOG_INFO("%s: Set Audio Input Device [%s] completed\n", NowString().c_str(), deviceId.ToString().c_str());
}

void EasyApp::onSetAudioInputDeviceFailed(const AudioDeviceId &deviceId, VCSStatus status)
{
    LOG_INFO("%s: Set Audio Input Device [%s] failed for reason: (%d) %s\n", NowString().c_str(), deviceId.ToString().c_str(), status, GetErrorString(status));
}

void EasyApp::onDefaultSystemAudioOutputDeviceChanged(const AudioDeviceId &deviceId)
{
    LOG_INFO("%s: Default System Audio Output Device Changed to %s\n", NowString().c_str(), deviceId.ToString().c_str());
}

void EasyApp::onDefaultCommunicationAudioOutputDeviceChanged(const AudioDeviceId &deviceId)
{
    LOG_INFO("%s: Default Communication Audio Output Device Changed to %s\n", NowString().c_str(), deviceId.ToString().c_str());
}

void EasyApp::onSetAudioOutputDeviceCompleted(const AudioDeviceId &deviceId)
{
    LOG_INFO("%s: Set Audio Output Device [%s] completed\n", NowString().c_str(), deviceId.ToString().c_str());
}

void EasyApp::onSetAudioOutputDeviceFailed(const AudioDeviceId &deviceId, VCSStatus status)
{
    LOG_INFO("%s: Set Audio Output Device [%s] failed for reason: (%d) %s\n", NowString().c_str(), deviceId.ToString().c_str(), status, GetErrorString(status));
}

void EasyApp::onSetChannelAudioOutputDeviceVolumeCompleted(const AccountName &accountName, const Uri &channelUri, int volume)
{
    LOG_INFO("%s: %s: Set Channel %s Audio Output Device Volume to %d completed\n", NowString().c_str(), accountName.ToString(), channelUri.ToString(), volume);
}

void EasyApp::onSetChannelAudioOutputDeviceVolumeFailed(const AccountName &accountName, const Uri &channelUri, int volume, VCSStatus status)
{
    LOG_INFO("%s: %s: Set Channel %s Audio Output Device Volume to %d failed for reason: (%d) %s\n", NowString().c_str(), accountName.ToString(), channelUri.ToString(), volume, status, GetErrorString(status));
}

void EasyApp::onSetParticipantAudioOutputDeviceVolumeForMeCompleted(const AccountName &accountName, const Uri &targetUser, const Uri &channelUri, int volume)
{
    LOG_INFO("%s: %s: Set Participant %s Audio Output Device Volume to %d For Me in %s completed\n", NowString().c_str(), accountName.ToString(), targetUser.ToString(), volume, channelUri.ToString());
}

void EasyApp::onSetParticipantAudioOutputDeviceVolumeForMeFailed(const AccountName &accountName, const Uri &targetUser, const Uri &channelUri, int volume, VCSStatus status)
{
    LOG_INFO("%s: %s: Set Participant %s Audio Output Device Volume to %d For Me in %s  failed for reason: (%d) %s\n", NowString().c_str(), accountName.ToString(), targetUser.ToString(), volume, channelUri.ToString(), status, GetErrorString(status));
}

void EasyApp::onSetParticipantMutedForMeCompleted(const AccountName &accountName, const Uri &target, const Uri &channelUri, bool muted)
{
    LOG_INFO("%s: %s: Set Participant %s %s For Me in %s completed\n", NowString().c_str(), accountName.ToString(), target.ToString(), muted ? "Muted" : "Unmuted", channelUri.ToString());
}

void EasyApp::onSetParticipantMutedForMeFailed(const AccountName &accountName, const Uri &target, const Uri &channelUri, bool muted, VCSStatus status)
{
    LOG_INFO("%s: %s: Set Participant %s %s For Me in %s  failed for reason: (%d) %s\n", NowString().c_str(), accountName.ToString(), target.ToString(), muted ? "Muted" : "Unmuted", channelUri.ToString(), status, GetErrorString(status));
}

void EasyApp::onSetChannelTransmissionToSpecificChannelCompleted(const AccountName &accountName, const Uri &channelUri)
{
    LOG_INFO("%s: %s: Set Channel Transmission To Specific Channel %s completed\n", NowString().c_str(), accountName.ToString(), channelUri.ToString());
}

void EasyApp::onSetChannelTransmissionToSpecificChannelFailed(const AccountName &accountName, const Uri &channelUri, VCSStatus status)
{
    LOG_INFO("%s: %s: Set Channel Transmission To Specific Channel %s failed for reason: (%d) %s\n", NowString().c_str(), accountName.ToString(), channelUri.ToString(), status, GetErrorString(status));
}

void EasyApp::onSetChannelTransmissionToAllCompleted(const AccountName &accountName)
{
    LOG_INFO("%s: %s: Set Channel Transmission To All completed\n", NowString().c_str(), accountName.ToString());
}

void EasyApp::onSetChannelTransmissionToAllFailed(const AccountName &accountName, VCSStatus status)
{
    LOG_INFO("%s: %s: Set Channel Transmission To All failed for reason: (%d) %s\n", NowString().c_str(), accountName.ToString(), status, GetErrorString(status));
}

void EasyApp::onSetChannelTransmissionToNoneCompleted(const AccountName &accountName)
{
    LOG_INFO("%s: %s: Set Channel Transmission To None completed\n", NowString().c_str(), accountName.ToString());
}

void EasyApp::onSetChannelTransmissionToNoneFailed(const AccountName &accountName, VCSStatus status)
{
    LOG_INFO("%s: %s: Set Channel Transmission To None failed for reason: (%d) %s\n", NowString().c_str(), accountName.ToString(), status, GetErrorString(status));
}

void EasyApp::onAudioInputDeviceTestPlaybackCompleted()
{
    LOG_INFO("%s: Audio Input Device Test Playback completed\n", NowString().c_str());
}

void EasyApp::onSttTranscriptionReceived(const AccountName &accountName, const Uri &userUri, const Uri &channelUri, const char *text, const char *language, const char *displayName)
{
    LOG_INFO("%s: STT: %s\n", NowString().c_str(), text);
}
