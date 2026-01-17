#include "room.hpp"
#include "rtc_user.hpp"
#include "webrtc_session.hpp"
#include "webrtc_server.hpp"
#include "ws_message/ws_protoo_info.hpp"
#include "rtp_session.hpp"
#include "format/rtc_sdp/rtc_sdp.hpp"
#include "format/rtc_sdp/rtc_sdp_filter.hpp"
#include "utils/uuid.hpp"
#include "utils/event_log.hpp"
#include "config/config.hpp"
#include "rtc_recv_relay.hpp"
#include "rtc_send_relay.hpp"

extern std::unique_ptr<cpp_streamer::EventLog> g_rtc_event_log;

namespace cpp_streamer {

extern std::vector<RtpSessionParam> GetRtpSessionParamsFromSdp(const RtcSdp& sdp);

Room::Room(const std::string& room_id, 
    PilotClientI* pilot_client,
    uv_loop_t* loop, 
    Logger* logger) : TimerInterface(1000)
{
    room_id_ = room_id;
    pilot_client_ = pilot_client;
    logger_ = logger;
    loop_ = loop;
    LogInfof(logger_, "Room construct, room_id:%s", room_id_.c_str());

    last_alive_ms_ = now_millisec();
    StartTimer();
}

Room::~Room() {
    LogInfof(logger_, "Room destruct, room_id:%s", room_id_.c_str());
    Close();
}

bool Room::OnTimer() {
    // Check heartbeat of users
    if (!users_.empty()) {
        last_alive_ms_ = now_millisec();
    }
    std::vector<std::string> rm_user_ids;
    for (const auto& pair : users_) {
        auto user_ptr = pair.second;
        if (user_ptr->IsRemote()) {
            continue;
        }
        if (!user_ptr->IsAlive()) {
            LogWarnf(logger_, "User heartbeat timeout, removing user, user_id:%s, room_id:%s",
                user_ptr->GetUserId().c_str(), room_id_.c_str());
            rm_user_ids.push_back(pair.first);
        }
    }
    for (const auto& user_id : rm_user_ids) {
        ReleaseUserResources(user_id);
    }

    // Check heartbeat of RtcRecvRelay
    std::vector<std::string> rm_pusher_ids;
    std::vector<std::string> rm_puller_user_ids;
    for (auto& item :pusherId2recvRelay_) {
        auto relay_ptr = item.second;
        if (relay_ptr->IsAlive()) {
            continue;
        }
        rm_pusher_ids.push_back(item.first);
        rm_puller_user_ids.push_back(relay_ptr->GetPushUserId());
    }
    for (size_t i = 0; i < rm_pusher_ids.size(); ++i) {
        LogWarnf(logger_, "RtcRecvRelay heartbeat timeout, removing relay, pusher_id:%s, room_id:%s",
            rm_pusher_ids[i].c_str(), room_id_.c_str());
        pusherId2recvRelay_.erase(rm_pusher_ids[i]);
    }
    for (size_t i = 0; i < rm_puller_user_ids.size(); ++i) {
        LogWarnf(logger_, "Removing pusher2pullers_ entry for puller_user_id:%s, room_id:%s",
            rm_puller_user_ids[i].c_str(), room_id_.c_str());
        pusher_user_id2recvRelay_.erase(rm_puller_user_ids[i]);
    }
    return timer_running_;
}

void Room::Close() {
    if (closed_) {
        return;
    }
    closed_ = true;
    StopTimer();
    LogInfof(logger_, "Room closed, room_id:%s", room_id_.c_str());
}

RTC_USER_TYPE Room::GetUserType(const std::string& user_id) {
    auto it = users_.find(user_id);
    if (it == users_.end()) {
        return UNKNOWN_USER_TYPE;
    }
    auto user_ptr = it->second;
    if (user_ptr->IsRemote()) {
        return REMOTE_RTC_USER;
    }
    return LOCAL_RTC_USER;
}

void Room::ReleaseUserResources(const std::string& user_id) {
    try {
        UserLeave(user_id);
    } catch (std::exception& e) {
        LogErrorf(logger_, "Exception caught in DisconnectUser for user_id:%s, room_id:%s, error:%s",
            user_id.c_str(), room_id_.c_str(), e.what());
    }
    // Release resources related to the user
    auto it = users_.find(user_id);
    if (it == users_.end()) {
        return;
    }
    auto user_ptr = it->second;
    users_.erase(it);
    LogInfof(logger_, "Released RtcUser for user_id:%s, room_id:%s", 
        user_id.c_str(), room_id_.c_str());

    std::vector<std::string> rm_pusher_ids;
    for (auto item : pusherId2pusher_) {
        auto pusher_ptr = item.second;
        if (pusher_ptr->GetUserId() == user_id) {
            rm_pusher_ids.push_back(item.first);
        }
    }
    //remote pusherId2pusher_ entries
    for (const auto& pusher_id : rm_pusher_ids) {
        LogInfof(logger_, "remove pusherId2pusher_ entry, pusher_id:%s, room_id:%s",
            pusher_id.c_str(), room_id_.c_str());
        pusherId2pusher_.erase(pusher_id);
    }

    auto pusher_it = pusher_user_id2sendRelay_.find(user_id);
    if (pusher_it != pusher_user_id2sendRelay_.end()) {
        LogInfof(logger_, "Removing sendRelay for pushing user_id:%s, room_id:%s, pusher_id:%s",
            user_id.c_str(), room_id_.c_str(), pusher_it->second->GetPusherId().c_str());
        pusher_user_id2sendRelay_.erase(user_id);
    }

    for (auto& item : pusher2pullers_) {
        auto& puller_map = item.second;
        for (auto puller_it = puller_map.begin(); puller_it != puller_map.end(); ) {
            auto puller_ptr = puller_it->second;
            if (puller_ptr->GetPulllerUserId() == user_id) {
                LogInfof(logger_, "Removing MediaPuller for user_id:%s, room_id:%s, puller_id:%s",
                    user_id.c_str(), room_id_.c_str(), puller_ptr->GetPullerId().c_str());
                puller_it = puller_map.erase(puller_it);
            } else {
                ++puller_it;
            }
        }
    }
    pusher2pullers_.erase(user_id);
}

int Room::UserJoin(const std::string& user_id, 
    const std::string& user_name,
    int id,
    ProtooResponseI* resp_cb) {
    std::vector<std::shared_ptr<RtcUser>> user_list;
    if (closed_) {
        LogErrorf(logger_, "Room is closed, cannot join, room_id:%s", room_id_.c_str());
        return -1;
    }

    last_alive_ms_ = now_millisec();
    std::shared_ptr<RtcUser> new_user;
    auto it = users_.find(user_id);
    if (it != users_.end()) {
        LogWarnf(logger_, "User already in room, user_id:%s, room_id:%s", 
            user_id.c_str(), room_id_.c_str());
        if (g_rtc_event_log) {
            json evt_data;
            evt_data["event"] = "join";
            evt_data["room_id"] = room_id_;
            evt_data["user_id"] = user_id;
            evt_data["reconnect"] = true;
            g_rtc_event_log->Log("join", evt_data);
        }
        return ReConnect(it->second, id, resp_cb);
    } else {
        LogInfof(logger_, "New user joining room, user_id:%s, room_id:%s", 
            user_id.c_str(), room_id_.c_str());
        new_user = std::make_shared<RtcUser>(room_id_, user_id, user_name, resp_cb, logger_);
        users_[user_id] = new_user;
    }
    if (g_rtc_event_log) {
        json evt_data;
        evt_data["event"] = "join";
        evt_data["room_id"] = room_id_;
        evt_data["user_id"] = user_id;
        evt_data["reconnect"] = false;
        g_rtc_event_log->Log("join", evt_data);
    }
    LogInfof(logger_, "User joined room, user_id:%s, user_name:%s, room_id:%s",
        user_id.c_str(), user_name.c_str(), room_id_.c_str());

    for (const auto& pair : users_) {
        if (pair.first == user_id) {
            continue;
        }
        user_list.push_back(pair.second);
    }
    // Process join request logic
    json resp_json = json::object();

    resp_json["code"] = 0;
    resp_json["message"] = "join success";
    resp_json["users"] = json::array();

    for (const auto& user : user_list) {
        json user_json = json::object();
        user_json["userId"] = user->GetUserId();
        user_json["userName"] = user->GetUserName();
        
        std::map<std::string, PushInfo> pusher_map = user->GetPushers();
        user_json["pushers"] = json::array();
        for (const auto& pair : pusher_map) {
            json pusher_json = json::object();
            pair.second.DumpJson(pusher_json);
            user_json["pushers"].push_back(pusher_json);
        }
        resp_json["users"].push_back(user_json);
    }

    Join2PilotCenter(new_user);
    ProtooResponse resp(id, 0, "", resp_json);
    resp_cb->OnProtooResponse(resp);

    //notify other users
    NotifyNewUser(user_id, user_name);
    return 0;
}

void Room::NotifyNewUser(const std::string& user_id, const std::string& user_name) {
    //notify other users
    auto it = users_.find(user_id);
    if (it == users_.end()) {
        LogErrorf(logger_, "NotifyNewUser failed, user not found, user_id:%s, room_id:%s",
            user_id.c_str(), room_id_.c_str());
        return;
    }
    auto new_user = it->second;
    last_alive_ms_ = now_millisec();

    json user_json = json::object();
    user_json["userId"] = user_id;
    user_json["userName"] = user_name;
    user_json["pushers"] = json::array();
    auto pushers_it = user_json.find("pushers");
    std::map<std::string, PushInfo> pusher_map = new_user->GetPushers();
    for (const auto& pair : pusher_map) {
        json pusher_json = json::object();
        pair.second.DumpJson(pusher_json);
        pushers_it->push_back(pusher_json);
    }
    for (const auto& pair : users_) {
        json notify_array = json::array();
        if (pair.first == user_id) {
            continue;
        }
        if (pair.second->IsRemote()) {
            continue;
        }
        ProtooResponseI* notify_cb = pair.second->GetRespCb();
        if (notify_cb == nullptr) {
            continue;
        }
        
        notify_array.push_back(user_json);

        LogInfof(logger_, "notify new user, data:%s", notify_array.dump().c_str());
        if (g_rtc_event_log) {
            json evt_data;
            evt_data["event"] = "newUser";
            evt_data["room_id"] = room_id_;
            evt_data["notify_user_id"] = pair.first;
            evt_data["new_user_id"] = user_id;
            g_rtc_event_log->Log("newUser", evt_data);
        }
        notify_cb->Notification("newUser", notify_array);
    }
}

void Room::NotifyNewPusher(const std::string& pusher_user_id, 
    const std::string& pusher_user_name,
    const std::vector<PushInfo>& push_infos) {
    last_alive_ms_ = now_millisec();
    json pusher_json = json::object();
    pusher_json["userId"] = pusher_user_id;
    pusher_json["userName"] = pusher_user_name;
    pusher_json["roomId"] = room_id_;
    pusher_json["pushers"] = json::array();
    auto pushers_it = pusher_json.find("pushers");
    for (const auto& push_info : push_infos) {
        json info_json = json::object();
        push_info.DumpJson(info_json);
        pushers_it->push_back(info_json);
    }
    LogInfof(logger_, "notify new pusher, data:%s", pusher_json.dump().c_str());
    for (const auto& pair : users_) {
        if (pair.first == pusher_user_id) {
            continue;
        }
        if (pair.second->IsRemote()) {
            continue;
        }
        
        ProtooResponseI* notify_cb = pair.second->GetRespCb();
        if (notify_cb == nullptr) {
            continue;
        }
        if (g_rtc_event_log) {
            json evt_data;
            evt_data["event"] = "newPusher";
            evt_data["room_id"] = room_id_;
            evt_data["notify_user_id"] = pair.first;
            evt_data["pusher_user_id"] = pusher_user_id;
            evt_data["push_info"] = pusher_json["pushers"];
            g_rtc_event_log->Log("newPusher", evt_data);
        }
        notify_cb->Notification("newPusher", pusher_json);
    }
}
int Room::UserLeave(const std::string& user_id) {
    auto it = users_.find(user_id);
    if (it == users_.end()) {
        LogErrorf(logger_, "User not found in room, cannot leave, user_id:%s, room_id:%s",
            user_id.c_str(), room_id_.c_str());
        return -1;
    }

    it->second->SetRespCb(nullptr);

    LogInfof(logger_, "User left room, user_id:%s, room_id:%s",
        user_id.c_str(), room_id_.c_str());

    if (g_rtc_event_log) {
        json evt_data;
        evt_data["event"] = "userLeave";
        evt_data["room_id"] = room_id_;
        evt_data["user_id"] = user_id;
        g_rtc_event_log->Log("userLeave", evt_data);
    }
    //notify other users
    json notify_json = json::object();
    notify_json["userId"] = user_id;
    notify_json["roomId"] = room_id_;
    for (const auto& pair : users_) {
        if (pair.first == user_id) {
            continue;
        }
        if (pair.second->IsRemote()) {
            continue;
        }
        
        ProtooResponseI* notify_cb = pair.second->GetRespCb();
        if (notify_cb == nullptr) {
            continue;
        }
        LogInfof(logger_, "notify user leave, data:%s", notify_json.dump().c_str());
        notify_cb->Notification("userLeave", notify_json);
    }

    //notify userLeave to pilot center
    UserLeave2PilotCenter(user_id);
    return 0;
}

int Room::DisconnectUser(const std::string& user_id) {
    auto it = users_.find(user_id);
    if (it == users_.end()) {
        LogErrorf(logger_, "User not found in room, cannot disconnect, user_id:%s, room_id:%s",
            user_id.c_str(), room_id_.c_str());
        return -1;
    }
    it->second->SetRespCb(nullptr);
    LogInfof(logger_, "User disconnected from room, user_id:%s, room_id:%s",
        user_id.c_str(), room_id_.c_str());
    if (g_rtc_event_log) {
        json evt_data;
        evt_data["event"] = "userDisconnect";
        evt_data["room_id"] = room_id_;
        evt_data["user_id"] = user_id;
        g_rtc_event_log->Log("userDisconnect", evt_data);
    }
    json notify_json = json::object();
    notify_json["userId"] = user_id;
    notify_json["roomId"] = room_id_;
    for (const auto& pair : users_) {
        if (pair.first == user_id) {
            continue;
        }
        if (pair.second->IsRemote()) {
            continue;
        }
        
        ProtooResponseI* notify_cb = pair.second->GetRespCb();
        if (notify_cb == nullptr) {
            continue;
        }

        LogInfof(logger_, "notify user disconnect, data:%s", notify_json.dump().c_str());
        notify_cb->Notification("userDisconnect", notify_json);
    }

    //notify userDisconnect to pilot center
    UserDisconnect2PilotCenter(user_id);

    return 0;
}

int Room::HandlePushSdp(const std::string& user_id, 
    const std::string& sdp_type, 
    const std::string& sdp_str, 
    int id,
    ProtooResponseI* resp_cb) {
    std::string answer_sdp_str;
    std::string pusher_id;
    std::string session_id;
    last_alive_ms_ = now_millisec();
    auto sdp_ptr = RtcSdp::ParseSdp(sdp_type, sdp_str);
    
    LogDebugf(logger_, "HandlePushSdp, user_id:%s, room_id:%s, sdp dump:\r\n%s",
        user_id.c_str(), room_id_.c_str(), sdp_ptr->DumpSdp().c_str());
    if (g_rtc_event_log) {
        json evt_data;
        evt_data["event"] = "pushSdp";
        evt_data["room_id"] = room_id_;
        evt_data["user_id"] = user_id;
        g_rtc_event_log->Log("pushSdp", evt_data);
    }
    auto webrtc_session_ptr = std::make_shared<WebRtcSession>(SRtpType::SRTP_SESSION_TYPE_RECV, 
        room_id_, user_id, this, this, loop_, logger_);
    webrtc_session_ptr->DtlsInit(Role::ROLE_SERVER, sdp_ptr->finger_print_);
    std::string local_ufrag = webrtc_session_ptr->GetIceUfrag();
    std::string local_pwd = webrtc_session_ptr->GetIcePwd();
    std::string local_fp = webrtc_session_ptr->GetLocalFingerPrint();

    WebRtcServer::SetUserName2Session(local_ufrag, webrtc_session_ptr);
    auto answer_sdp = sdp_ptr->GenAnswerSdp(g_sdp_answer_filter, 
        RTC_SETUP_PASSIVE, 
        DIRECTION_RECVONLY,
        local_ufrag,
        local_pwd,
        local_fp
        );
    if (answer_sdp == nullptr) {
        LogErrorf(logger_, "Generate answer SDP failed, user_id:%s, room_id:%s",
            user_id.c_str(), room_id_.c_str());
        return -1;
    }
    try {
        for (auto & candidate : Config::Instance().rtc_candidates_) {
            IceCandidate ice_candidate;
            ice_candidate.ip_ = candidate.candidate_ip_;
            ice_candidate.port_ = candidate.port_;
            ice_candidate.foundation_ = cpp_streamer::UUID::GetRandomUint(10000001, 99999999);
            ice_candidate.priority_ = 10001;
            ice_candidate.net_type_ = candidate.net_type_;
            answer_sdp->ice_candidates_.push_back(ice_candidate);
        }
    } catch(const std::exception& e) {
        LogErrorf(logger_, "No RTC candidate found in config, user_id:%s, room_id:%s, error:%s",
            user_id.c_str(), room_id_.c_str(), e.what());
        return -1;
    }
    
    try {
        auto rtp_params = GetRtpSessionParamsFromSdp(*answer_sdp);
        if (rtp_params.empty()) {
            LogErrorf(logger_, "No valid RTP session params found in SDP, user_id:%s, room_id:%s",
                user_id.c_str(), room_id_.c_str());
            return -1;
        }
        for (const auto& param : rtp_params) {
            std::string pusher_id;
            LogInfof(logger_, "Adding RTP pusher session, user_id:%s, room_id:%s, rtp_param:%s",
                user_id.c_str(), room_id_.c_str(), param.Dump().c_str());
            webrtc_session_ptr->AddPusherRtpSession(param, pusher_id);
            auto it = users_.find(user_id);
            if (it != users_.end()) {
                PushInfo push_info;
                push_info.pusher_id_ = pusher_id;
                push_info.param_ = param;
                it->second->UpdateHeartbeat();
                it->second->AddPusher(pusher_id, push_info);
            }
            auto media_pushers = webrtc_session_ptr->GetMediaPushers();
            for (const auto& media_pusher : media_pushers) {
                pusherId2pusher_[media_pusher->GetPusherId()] = media_pusher;
            }
        }
    } catch(const std::exception& e) {
        LogErrorf(logger_, "Failed to add RTP sessions from SDP, user_id:%s, room_id:%s, error:%s",
            user_id.c_str(), room_id_.c_str(), e.what());
        return -1;
    }

    try {
        LogDebugf(logger_, "Generated answer SDP, user_id:%s, room_id:%s, sdp dump:\r\n%s",
            user_id.c_str(), room_id_.c_str(), answer_sdp->DumpSdp().c_str());

        answer_sdp_str = answer_sdp->GenSdpString();
        LogInfof(logger_, "Generated answer SDP string, user_id:%s, room_id:%s, sdp:\r\n%s",
            user_id.c_str(), room_id_.c_str(), answer_sdp_str.c_str());

        json resp_json = json::object();

        resp_json["code"] = 0;
        resp_json["message"] = "push success";
        resp_json["sdp"] = answer_sdp_str;
        ProtooResponse resp(id, 0, "", resp_json);
        resp_cb->OnProtooResponse(resp);
    } catch(const std::exception& e) {
        LogErrorf(logger_, "Failed to generate answer SDP string, user_id:%s, room_id:%s, error:%s",
            user_id.c_str(), room_id_.c_str(), e.what());
        return -1;
    }

    std::vector<PushInfo> push_infos;
    auto user_it = users_.find(user_id);
    if (user_it == users_.end()) {
        LogErrorf(logger_, "User not found when notify new pusher to pilot center, user_id:%s, room_id:%s",
            user_id.c_str(), room_id_.c_str());
        return -1;
    }
    std::map<std::string, PushInfo> pusher_map = user_it->second->GetPushers();
    for (const auto& pair : pusher_map) {
        push_infos.push_back(pair.second);
    }
    std::string user_name = user_it->second->GetUserName();

    //notify to local other users
    NotifyNewPusher(user_id, user_name, push_infos);

    NewPusher2PilotCenter(user_id, push_infos);
    return 0;
}

int Room::HandleRemotePullSdp(const std::string& pusher_user_id,
        const PullRequestInfo& pull_info, 
        const std::string& sdp_type, 
        const std::string& sdp_str, 
        int id,
        ProtooResponseI* resp_cb) {
    int ret = 0;
    try {
        last_alive_ms_ = now_millisec();
        LogInfof(logger_, "HandleRemotePullSdp pusher_user_id:%s, called: %s", pusher_user_id.c_str(), pull_info.Dump().c_str());
        
        if (g_rtc_event_log) {
            json evt_data;
            json pull_info_json = json::object();
            pull_info.Dump(pull_info_json);
            evt_data["event"] = "remotePullSdp";
            evt_data["room_id"] = room_id_;
            evt_data["pusher_user_id"] = pusher_user_id;
            evt_data["pull_info"] = pull_info_json;
            g_rtc_event_log->Log("remotePullSdp", evt_data);
        }
        for (const auto& push_info : pull_info.pushers_) {
            auto user_it = users_.find(pusher_user_id);
            if (user_it == users_.end()) {
                LogErrorf(logger_, "Target pusher user not found in room, user_id:%s, room_id:%s",
                    pusher_user_id.c_str(), room_id_.c_str());
                continue;
            }
            PushInfo full_push_info;
            bool r = user_it->second->GetPusher(push_info.pusher_id_, full_push_info);
            if (!r) {
                LogErrorf(logger_, "Pusher not found for remote pull, pusher_id:%s, user_id:%s, room_id:%s",
                    push_info.pusher_id_.c_str(), pusher_user_id.c_str(), room_id_.c_str());
                continue;
            }
            int ret = PullRemotePusher(pull_info.target_user_id_, full_push_info);
            if (ret < 0) {
                LogErrorf(logger_, "PullRemotePusher failed, target_user_id:%s, pusher_id:%s, room_id:%s",
                    pusher_user_id.c_str(), full_push_info.pusher_id_.c_str(), room_id_.c_str());
            }
        }
        auto pull_sdp_ptr = RtcSdp::ParseSdp(sdp_type, sdp_str);
        // Build the answer SDP based on pull_info
        auto webrtc_session_ptr = std::make_shared<WebRtcSession>(SRtpType::SRTP_SESSION_TYPE_SEND, 
            room_id_, pull_info.src_user_id_, this, this, loop_, logger_);
        webrtc_session_ptr->DtlsInit(Role::ROLE_SERVER, pull_sdp_ptr->finger_print_);
        std::string local_ufrag = webrtc_session_ptr->GetIceUfrag();
        std::string local_pwd = webrtc_session_ptr->GetIcePwd();
        std::string local_fp = webrtc_session_ptr->GetLocalFingerPrint();
        
        WebRtcServer::SetUserName2Session(local_ufrag, webrtc_session_ptr);
        auto answer_sdp = pull_sdp_ptr->GenAnswerSdp(g_sdp_answer_filter, 
            RTC_SETUP_PASSIVE, 
            DIRECTION_SENDONLY,
            local_ufrag,
            local_pwd,
            local_fp);
        if (answer_sdp == nullptr) {
            LogErrorf(logger_, "Generate remote pull answer SDP failed, user_id:%s, room_id:%s",
                pull_info.src_user_id_.c_str(), room_id_.c_str());
            return -1;
        }
        try {
            for (auto candidate : Config::Instance().rtc_candidates_) {
                IceCandidate ice_candidate;
                ice_candidate.ip_ = candidate.candidate_ip_;
                ice_candidate.port_ = candidate.port_;
                ice_candidate.foundation_ = cpp_streamer::UUID::GetRandomUint(10000001, 99999999);
                ice_candidate.priority_ = 10001;
                ice_candidate.net_type_ = candidate.net_type_;
                answer_sdp->ice_candidates_.push_back(ice_candidate);
            }
        } catch(const std::exception& e) {
            LogErrorf(logger_, "when remote pull request, No RTC candidate found in config, user_id:%s, room_id:%s, error:%s",
                pull_info.src_user_id_.c_str(), room_id_.c_str(), e.what());
            return -1;
        }
        for (const auto& push_info : pull_info.pushers_) {
            std::string id = push_info.pusher_id_;
            auto it = pusherId2recvRelay_.find(id);
            if (it == pusherId2recvRelay_.end()) {
                LogErrorf(logger_, "Pusher RTP recv relay not found for remote pull, pusher_id:%s, user_id:%s, room_id:%s",
                    id.c_str(), pull_info.src_user_id_.c_str(), room_id_.c_str());
                continue;
            }
            auto recv_relay_ptr = it->second;
            PushInfo relay_push_info;
            bool r = recv_relay_ptr->GetPushInfo(id, relay_push_info);
            if (!r) {
                LogErrorf(logger_, "GetPushInfo failed from RTP recv relay for remote pull, pusher_id:%s, user_id:%s, room_id:%s",
                    id.c_str(), pull_info.src_user_id_.c_str(), room_id_.c_str());
                continue;
            }
            std::string puller_id;
            ret = webrtc_session_ptr->AddPullerRtpSession(relay_push_info.param_, 
                pull_info.target_user_id_,
                relay_push_info.pusher_id_,
                puller_id);
            if (ret < 0) {
                LogErrorf(logger_, "Failed to add puller RTP session, pusher_id:%s, user_id:%s, room_id:%s",
                    id.c_str(), pull_info.src_user_id_.c_str(), room_id_.c_str());
                return ret;
            }
        }
        std::vector<std::shared_ptr<MediaPuller>> media_pullers = webrtc_session_ptr->GetMediaPullers();
        int ret = UpdateRtcSdpByPullers(media_pullers, answer_sdp);
        if (ret != 0) {
            LogErrorf(logger_, "UpdateRtcSdpByPullers error, userid:%s, room_id:%s",
                pull_info.src_user_id_.c_str(), room_id_.c_str());
            return ret;
        }
        for (const auto& media_puller : media_pullers) {
            pusher2pullers_[media_puller->GetPusherId()][media_puller->GetPullerId()] = media_puller;
        }
        LogInfof(logger_, "Generated remote pull answer SDP, user_id:%s, room_id:%s, sdp dump:\r\n%s",
            pull_info.src_user_id_.c_str(), room_id_.c_str(), answer_sdp->DumpSdp().c_str());
        std::string answer_sdp_str = answer_sdp->GenSdpString();
        LogInfof(logger_, "Generated remote pull answer SDP string, user_id:%s,room_id:%s, sdp:\r\n%s",
            pull_info.src_user_id_.c_str(), room_id_.c_str(), answer_sdp_str.c_str());
        json resp_json = json::object();
        resp_json["code"] = 0;
        resp_json["message"] = "pull success";
        resp_json["sdp"] = answer_sdp_str;
        ProtooResponse resp(id, 0, "", resp_json);
        resp_cb->OnProtooResponse(resp);
    } catch(const std::exception& e) {
        LogErrorf(logger_, "HandleRemotePullSdp exception, src_user_id:%s, target_user_id:%s, room_id:%s, error:%s",
            pull_info.src_user_id_.c_str(), pull_info.target_user_id_.c_str(), room_id_.c_str(), e.what());
        return -1;
    }
    
    return 0;
}

int Room::HandlePullSdp(const PullRequestInfo& pull_info, 
    const std::string& sdp_type, 
    const std::string& sdp_str, 
    int id,
    ProtooResponseI* resp_cb) {
    // Implementation for handling pull SDP requests goes here.
    int ret = 0;
    std::string answer_sdp_str;
    last_alive_ms_ = now_millisec();
    LogInfof(logger_, "HandlePullSdp called: %s", pull_info.Dump().c_str());
    if (g_rtc_event_log) {
        json evt_data;
        json pull_info_json = json::object();
        pull_info.Dump(pull_info_json);
        evt_data["event"] = "pullSdp";
        evt_data["room_id"] = room_id_;
        evt_data["pull_info"] = pull_info_json;
        g_rtc_event_log->Log("pullSdp", evt_data);
    }
    auto pusher_user_it = users_.find(pull_info.target_user_id_);
    if (pusher_user_it == users_.end()) {
        LogErrorf(logger_, "Target pusher user not found in room, user_id:%s, room_id:%s",
            pull_info.target_user_id_.c_str(), room_id_.c_str());
        return -1;
    }

    try {
        auto pull_sdp_ptr = RtcSdp::ParseSdp(sdp_type, sdp_str);
        // Build the answer SDP based on pull_info
        auto webrtc_session_ptr = std::make_shared<WebRtcSession>(SRtpType::SRTP_SESSION_TYPE_SEND, 
            room_id_, pull_info.src_user_id_, this, this, loop_, logger_);
        webrtc_session_ptr->DtlsInit(Role::ROLE_SERVER, pull_sdp_ptr->finger_print_);
        std::string local_ufrag = webrtc_session_ptr->GetIceUfrag();
        std::string local_pwd = webrtc_session_ptr->GetIcePwd();
        std::string local_fp = webrtc_session_ptr->GetLocalFingerPrint();
        
        WebRtcServer::SetUserName2Session(local_ufrag, webrtc_session_ptr);
        auto answer_sdp = pull_sdp_ptr->GenAnswerSdp(g_sdp_answer_filter, 
            RTC_SETUP_PASSIVE, 
            DIRECTION_SENDONLY,
            local_ufrag,
            local_pwd,
            local_fp);
        if (answer_sdp == nullptr) {
            LogErrorf(logger_, "Generate pull answer SDP failed, user_id:%s, room_id:%s",
                pull_info.src_user_id_.c_str(), room_id_.c_str());
            return -1;
        }
        try {
            for (auto candidate : Config::Instance().rtc_candidates_) {
                IceCandidate ice_candidate;
                ice_candidate.ip_ = candidate.candidate_ip_;
                ice_candidate.port_ = candidate.port_;
                ice_candidate.foundation_ = cpp_streamer::UUID::GetRandomUint(10000001, 99999999);
                ice_candidate.priority_ = 10001;
                ice_candidate.net_type_ = candidate.net_type_;
                answer_sdp->ice_candidates_.push_back(ice_candidate);
            }
        } catch(const std::exception& e) {
            LogErrorf(logger_, "when pull request, No RTC candidate found in config, user_id:%s, room_id:%s, error:%s",
                pull_info.src_user_id_.c_str(), room_id_.c_str(), e.what());
            return -1;
        }
        for (const auto& push_info : pull_info.pushers_) {
            RtpSessionParam param;

            std::string id = push_info.pusher_id_;
            auto it = pusherId2pusher_.find(id);
            if (it == pusherId2pusher_.end()) {
                LogErrorf(logger_, "Pusher not found for pull request, pusher_id:%s, user_id:%s, room_id:%s",
                    id.c_str(), pull_info.src_user_id_.c_str(), room_id_.c_str());
                continue;
            }
            std::shared_ptr<MediaPusher> media_pusher = it->second;
            param.av_type_ = media_pusher->GetMediaType();

            std::string puller_id;
            ret = webrtc_session_ptr->AddPullerRtpSession(media_pusher->GetRtpSessionParam(), 
                pull_info.target_user_id_,
                media_pusher->GetPusherId(),
                puller_id);
            if (ret != 0) {
                LogErrorf(logger_, "Failed to add puller RTP session, pusher_id:%s, user_id:%s, room_id:%s",
                    id.c_str(), pull_info.src_user_id_.c_str(), room_id_.c_str());
                return ret;
            }
        }
        std::vector<std::shared_ptr<MediaPuller>> media_pullers = webrtc_session_ptr->GetMediaPullers();
        ret = UpdateRtcSdpByPullers(media_pullers, answer_sdp);
        if (ret != 0) {
            LogErrorf(logger_, "UpdateRtcSdpByPullers error, userid:%s, room_id:%s",
                pull_info.src_user_id_.c_str(), room_id_.c_str());
            return ret;
        }
        for (const auto& media_puller : media_pullers) {
            pusher2pullers_[media_puller->GetPusherId()][media_puller->GetPullerId()] = media_puller;
        }
        LogInfof(logger_, "Generated pull answer SDP, user_id:%s, room_id:%s, sdp dump:\r\n%s",
            pull_info.src_user_id_.c_str(), room_id_.c_str(), answer_sdp->DumpSdp().c_str());
        answer_sdp_str = answer_sdp->GenSdpString();
        LogInfof(logger_, "Generated pull answer SDP string, user_id:%s, room_id:%s, sdp:\r\n%s",
            pull_info.src_user_id_.c_str(), room_id_.c_str(), answer_sdp_str.c_str());

        json resp_json = json::object();
        resp_json["code"] = 0;
        resp_json["message"] = "pull success";
        resp_json["sdp"] = answer_sdp_str;
        ProtooResponse resp(id, 0, "", resp_json);
        resp_cb->OnProtooResponse(resp);
    } catch(const std::exception& e) {
        LogErrorf(logger_, "Failed to handle pull SDP, user_id:%s, room_id:%s, error:%s",
            pull_info.src_user_id_.c_str(), room_id_.c_str(), e.what());
        return -1;
    }
    
    return 0;
}

int Room::PullRemotePusher(const std::string& pusher_user_id, const PushInfo& push_info) {
    last_alive_ms_ = now_millisec();
    std::shared_ptr<RtcRecvRelay> relay_ptr = CreateOrGetRecvRtcRelay(pusher_user_id, push_info);
    if (relay_ptr == nullptr) {
        LogErrorf(logger_, "PullRemotePusher failed, pusher_user_id:%s, room_id:%s",
            pusher_user_id.c_str(), room_id_.c_str());
        return -1;
    }
    int ret = relay_ptr->AddVirtualPusher(push_info);
    if (ret < 0) {
        LogErrorf(logger_, "AddVirtualPusher failed in CreateOrGetRecvRtcRelay, room_id:%s, user_id:%s, pusher_id:%s",
            room_id_.c_str(), pusher_user_id.c_str(), push_info.pusher_id_.c_str());
        return -2;
    }
    // send pull request to pilot center
    ret = SendPullRequestToPilotCenter(pusher_user_id, push_info, relay_ptr);
    if (ret < 0) {
        LogErrorf(logger_, "SendPullRequestToPilotCenter failed, pusher_user_id:%s, room_id:%s",
            pusher_user_id.c_str(), room_id_.c_str());
        return -3;
    }

    return 0;
}

int Room::SendPullRequestToPilotCenter(const std::string& pusher_user_id, 
    const PushInfo& push_info,
    std::shared_ptr<RtcRecvRelay> relay_ptr) {
    last_alive_ms_ = now_millisec();
    try {
        json pull_request_json = json::object();
        pull_request_json["roomId"] = room_id_;
        pull_request_json["pusher_user_id"] = pusher_user_id;
        pull_request_json["udp_ip"] = relay_ptr->GetListenUdpIp();
        pull_request_json["udp_port"] = relay_ptr->GetListenUdpPort();

        if (push_info.param_.av_type_ == MEDIA_PKT_TYPE::MEDIA_VIDEO_TYPE) {
            pull_request_json["mediaType"] = "video";
        } else if (push_info.param_.av_type_ == MEDIA_PKT_TYPE::MEDIA_AUDIO_TYPE) {
            pull_request_json["mediaType"] = "audio";
        } else {
            pull_request_json["mediaType"] = "unknown";
        }
        pull_request_json["pushInfo"] = json::object();
        push_info.DumpJson(pull_request_json["pushInfo"]);

        if (g_rtc_event_log) {
            json evt_data;
            evt_data["event"] = "pullRemoteStream";
            evt_data["room_id"] = room_id_;
            evt_data["pusher_user_id"] = pusher_user_id;
            evt_data["pull_request"] = pull_request_json["pushInfo"];
            g_rtc_event_log->Log("pullRemoteStream", evt_data);
        }
        pilot_client_->AsyncNotification("pullRemoteStream", pull_request_json);
    } catch (const std::exception& e) {
        LogErrorf(logger_, "SendPullRequestToPilotCenter exception, pusher_user_id:%s, room_id:%s, error:%s",
            pusher_user_id.c_str(), room_id_.c_str(), e.what());
        return -1;
    }
    return 0;
}

void Room::OnRtpPacketFromRtcPusher(const std::string& user_id, const std::string& session_id,
        const std::string& pusher_id, RtpPacket* rtp_packet) {
    LogDebugf(logger_, "OnRtpPacketFromRtcPusher, room_id:%s, user_id:%s, session_id:%s, pusher_id:%s, len:%zu, ssrc:%u, pt:%d, seq:%d",
        room_id_.c_str(), user_id.c_str(), session_id.c_str(), pusher_id.c_str(), 
        rtp_packet->GetDataLength(), rtp_packet->GetSsrc(), rtp_packet->GetPayloadType(), rtp_packet->GetSeq());
    last_alive_ms_ = now_millisec();
    auto it = users_.find(user_id);
    if (it != users_.end()) {
        it->second->UpdateHeartbeat();
    }
    
    auto pullers_it = pusher2pullers_.find(pusher_id);
    if (pullers_it != pusher2pullers_.end()) {
        for (const auto& puller_pair : pullers_it->second) {
            auto media_puller = puller_pair.second;
            media_puller->OnTransportSendRtp(rtp_packet);

            std::string puller_user_id = media_puller->GetPulllerUserId();
            auto user_it = users_.find(puller_user_id);
            if (user_it != users_.end()) {
                user_it->second->UpdateHeartbeat();
            }
        }
    }

    auto relay_it = pusher_user_id2sendRelay_.find(user_id);
    if (relay_it != pusher_user_id2sendRelay_.end()) {
        relay_it->second->SendRtpPacket(rtp_packet);
    }
}

void Room::OnRtpPacketFromRemoteRtcPusher(const std::string& pusher_user_id,
        const std::string& pusher_id, RtpPacket* rtp_packet) {
    last_alive_ms_ = now_millisec();
    auto it = users_.find(pusher_user_id);
    if (it != users_.end()) {
        it->second->UpdateHeartbeat();
    }
    LogDebugf(logger_, "OnRtpPacketFromRemoteRtcPusher, room_id:%s, pusher_user_id:%s, pusher_id:%s, len:%zu, ssrc:%u, pt:%d, seq:%d, pullers:%zu",
        room_id_.c_str(), pusher_user_id.c_str(), pusher_id.c_str(), 
        rtp_packet->GetDataLength(), rtp_packet->GetSsrc(), rtp_packet->GetPayloadType(), rtp_packet->GetSeq(), pusher2pullers_.size());
    auto pullers_it = pusher2pullers_.find(pusher_id);
    if (pullers_it != pusher2pullers_.end()) {
        for (const auto& puller_pair : pullers_it->second) {
            auto media_puller = puller_pair.second;
            media_puller->OnTransportSendRtp(rtp_packet);

            std::string puller_user_id = media_puller->GetPulllerUserId();
            auto user_it = users_.find(puller_user_id);
            if (user_it != users_.end()) {
                user_it->second->UpdateHeartbeat();
            }
        }
    } else {
        LogErrorf(logger_, "OnRtpPacketFromRemoteRtcPusher: no pullers found for pusher_id:%s, pusher_user_id:%s, room_id:%s",
            pusher_id.c_str(), pusher_user_id.c_str(), room_id_.c_str());
    }
}

int Room::HandleWsHeartbeat(const std::string& user_id) {
    last_alive_ms_ = now_millisec();
    auto it = users_.find(user_id);
    if (it == users_.end()) {
        LogErrorf(logger_, "Heartbeat from unknown user, user_id:%s, room_id:%s",
            user_id.c_str(), room_id_.c_str());
        return -1;
    }
    LogDebugf(logger_, "Heartbeat received from user, user_id:%s, room_id:%s",
        user_id.c_str(), room_id_.c_str());
    it->second->UpdateHeartbeat();
    return 0;
}

int Room::UpdateRtcSdpByPullers(std::vector<std::shared_ptr<MediaPuller>>& media_pullers, std::shared_ptr<RtcSdp> answer_sdp) {
    for (const auto& media_puller : media_pullers) {
        MEDIA_PKT_TYPE media_type = media_puller->GetMediaType();
        
        for (auto it = answer_sdp->media_sections_.begin(); 
            it != answer_sdp->media_sections_.end(); ++it) {
            if (it->second->media_type_ == media_type) {
                it->second->direction_ = DIRECTION_SENDONLY;
                it->second->ssrc_infos_.clear();
                RtpSessionParam param = media_puller->GetRtpSessionParam();
                auto ssrc_info = std::make_shared<SsrcInfo>();
                ssrc_info->ssrc_ = param.ssrc_;
                ssrc_info->is_main_ = true;
                ssrc_info->cname_ = "cname_" + std::to_string(param.ssrc_);
                ssrc_info->stream_id_ = cpp_streamer::UUID::MakeUUID2();

                it->second->ssrc_infos_[param.ssrc_] = ssrc_info;
                if (param.rtx_ssrc_ != 0) {
                    auto rtx_ssrc_info = std::make_shared<SsrcInfo>();
                    rtx_ssrc_info->ssrc_ = param.rtx_ssrc_;
                    rtx_ssrc_info->is_main_ = false;
                    rtx_ssrc_info->cname_ = "cname_" + std::to_string(param.rtx_ssrc_);
                    rtx_ssrc_info->stream_id_ = ssrc_info->stream_id_;
                    it->second->ssrc_infos_[param.rtx_ssrc_] = rtx_ssrc_info;
                }
                auto main_codec_ptr = std::make_shared<RtcSdpMediaCodec>();
       
                main_codec_ptr->codec_name_ = param.codec_name_;
                main_codec_ptr->is_rtx_ = false;
                main_codec_ptr->payload_type_ = param.payload_type_;
                main_codec_ptr->rate_ = param.clock_rate_;
                main_codec_ptr->channel_ = param.channel_;
                main_codec_ptr->fmtp_param_ = param.fmtp_param_;
                main_codec_ptr->rtx_payload_type_ = param.rtx_payload_type_;
                main_codec_ptr->rtcp_features_ = param.rtcp_features_;

                it->second->media_codecs_[param.payload_type_] = main_codec_ptr;
            } else {
                continue;
            }
        }
    }
    return 0;
}

void Room::OnPushClose(const std::string& pusher_id) {
    LogInfof(logger_, "OnPushClose called, room_id:%s, pusher_id:%s",
        room_id_.c_str(), pusher_id.c_str());
    auto it = pusherId2pusher_.find(pusher_id);
    if (it != pusherId2pusher_.end()) {
        pusherId2pusher_.erase(it);
    }
}

void Room::OnPullClose(const std::string& puller_id) {
    LogInfof(logger_, "OnPullClose called, room_id:%s, puller_id:%s",
        room_id_.c_str(), puller_id.c_str());
    for (auto& pusher_pair : pusher2pullers_) {
        auto it = pusher_pair.second.find(puller_id);
        if (it != pusher_pair.second.end()) {
            pusher_pair.second.erase(it);
            break;
        }
    }
}

void Room::OnKeyFrameRequest(const std::string& pusher_id, 
    const std::string& puller_user_id, 
    const std::string& pusher_user_id,
    uint32_t ssrc) {
    LogInfof(logger_, "OnKeyFrameRequest called, room_id:%s, pusher_id:%s, puller_user_id:%s, pusher_user_id:%s, ssrc:%u",
        room_id_.c_str(), pusher_id.c_str(), puller_user_id.c_str(), pusher_user_id.c_str(), ssrc);
    auto user = users_.find(pusher_user_id);
    
    if (user->second->IsRemote()) {
        //todo: call recv_relay to send key frame request to remote pilot center
        auto it = pusher_user_id2recvRelay_.find(pusher_user_id);
        if (it != pusher_user_id2recvRelay_.end()) {
            it->second->RequestKeyFrame(ssrc);
        } else {
            LogErrorf(logger_, "RtcRecvRelay not found in OnKeyFrameRequest, room_id:%s, pusher_user_id:%s",
                room_id_.c_str(), pusher_user_id.c_str());
        }
        return;
    }
    auto it = pusherId2pusher_.find(pusher_id);
    if (it != pusherId2pusher_.end()) {
        it->second->RequestKeyFrame(ssrc);
    } else {
        LogErrorf(logger_, "Pusher not found in OnKeyFrameRequest, room_id:%s, pusher_id:%s",
            room_id_.c_str(), pusher_id.c_str());
    }
    
}

void Room::NewPusher2PilotCenter(const std::string& pusher_user_id, const std::vector<PushInfo>& push_infos) {
    if (!pilot_client_) {
        return;
    }
    last_alive_ms_ = now_millisec();
    try {
        auto push_user_it = users_.find(pusher_user_id);
        if (push_user_it == users_.end()) {
            LogErrorf(logger_, "NewPusher2PilotCenter failed, user not found, room_id:%s, user_id:%s",
                room_id_.c_str(), pusher_user_id.c_str());
            return;
        }
        std::string name = push_user_it->second->GetUserName();
        json push_data = json::object();
        push_data["roomId"] = room_id_;
        push_data["userId"] = pusher_user_id;
        push_data["userName"] = name;
        push_data["publishers"] = json::array();
        auto publishers_it = push_data.find("publishers");
        for (const auto& push_info : push_infos) {
            json publisher_json = json::object();
            push_info.DumpJson(publisher_json);
            publishers_it->push_back(publisher_json);
        }
        if (g_rtc_event_log) {
            json evt_data;
            evt_data["event"] = "newPusher2PilotCenter";
            evt_data["room_id"] = room_id_;
            evt_data["pusher_user_id"] = pusher_user_id;
            evt_data["push_data"] = push_data["publishers"];
            g_rtc_event_log->Log("newPusher2PilotCenter", evt_data);
        }
        pilot_client_->AsyncNotification("push", push_data);
    } catch(const std::exception& e) {
        LogErrorf(logger_, "NewPusher2PilotCenter exception, room_id:%s, user_id:%s, error:%s",
            room_id_.c_str(), pusher_user_id.c_str(), e.what());
    }
    return;
}

void Room::Join2PilotCenter(std::shared_ptr<RtcUser> user_ptr) {
    if (!pilot_client_) {
        return;
    }
    last_alive_ms_ = now_millisec();
    try {
        json join_data = json::object();
        join_data["roomId"] = room_id_;
        join_data["userId"] = user_ptr->GetUserId();
        join_data["userName"] = user_ptr->GetUserName();
        if (g_rtc_event_log) {
            json evt_data;
            evt_data["event"] = "join2PilotCenter";
            evt_data["room_id"] = room_id_;
            evt_data["user_id"] = user_ptr->GetUserId();
            evt_data["user_name"] = user_ptr->GetUserName();
            g_rtc_event_log->Log("join2PilotCenter", evt_data);
        }
        (void)pilot_client_->AsyncRequest("join", join_data, this);
    } catch(const std::exception& e) {
        LogErrorf(logger_, "Join2PilotCenter exception, room_id:%s, user_id:%s, error:%s",
            room_id_.c_str(), user_ptr->GetUserId().c_str(), e.what());
    }
    return;
}

void Room::UserDisconnect2PilotCenter(const std::string& user_id) {
    if (!pilot_client_) {
        return;
    }
    last_alive_ms_ = now_millisec();
    try {
        json leave_data = json::object();
        leave_data["roomId"] = room_id_;
        leave_data["userId"] = user_id;
        LogInfof(logger_, "UserDisconnect2PilotCenter, room_id:%s, user_id:%s",
            room_id_.c_str(), user_id.c_str());
        if (g_rtc_event_log) {
            json evt_data;
            evt_data["event"] = "userDisconnect2PilotCenter";
            evt_data["room_id"] = room_id_;
            evt_data["user_id"] = user_id;
            g_rtc_event_log->Log("userDisconnect2PilotCenter", evt_data);
        }
        pilot_client_->AsyncNotification("userDisconnect", leave_data);
    } catch(const std::exception& e) {
        LogErrorf(logger_, "UserDisconnect2PilotCenter exception, room_id:%s, user_id:%s, error:%s",
            room_id_.c_str(), user_id.c_str(), e.what());
    }
    return;
}

void Room::UserLeave2PilotCenter(const std::string& user_id) {
    if (!pilot_client_) {
        return;
    }
    last_alive_ms_ = now_millisec();
    try {
        json leave_data = json::object();
        leave_data["roomId"] = room_id_;
        leave_data["userId"] = user_id;
        LogInfof(logger_, "UserLeave2PilotCenter, room_id:%s, user_id:%s",
            room_id_.c_str(), user_id.c_str());

        if (g_rtc_event_log) {
            json evt_data;
            evt_data["event"] = "userLeave2PilotCenter";
            evt_data["room_id"] = room_id_;
            evt_data["user_id"] = user_id;
            g_rtc_event_log->Log("userLeave2PilotCenter", evt_data);
        }
        pilot_client_->AsyncNotification("userLeave", leave_data);
    } catch(const std::exception& e) {
        LogErrorf(logger_, "UserLeave2PilotCenter exception, room_id:%s, user_id:%s, error:%s",
            room_id_.c_str(), user_id.c_str(), e.what());
    }
    return;
}
void Room::OnAsyncRequestResponse(int id, const std::string& method, json& resp_json) {
    LogInfof(logger_, "OnAsyncRequestResponse called, room_id:%s, method:%s, id:%d, resp_json:%s",
        room_id_.c_str(), method.c_str(), id, resp_json.dump().c_str());

    try {
        if (method == "join") {
            JoinResponseFromPilotCenter(method, resp_json);
        } else {
            LogErrorf(logger_, "Unknown method in OnAsyncRequestResponse, room_id:%s, method:%s",
                room_id_.c_str(), method.c_str());
        }
    } catch(const std::exception& e) {
        std::cerr << e.what() << '\n';
    }
    
    return;
}

void Room::JoinResponseFromPilotCenter(const std::string& method, json& resp_json) {
    try {
        std::string roomId = resp_json["roomId"];
        json users_json = resp_json["users"];

        if (roomId != room_id_) {
            LogErrorf(logger_, "Room ID mismatch in OnAsyncRequestResponse, room_id:%s, resp_room_id:%s",
                room_id_.c_str(), roomId.c_str());
            return;
        }
        if (!users_json.is_array()) {
            LogErrorf(logger_, "Invalid users data in OnAsyncRequestResponse, room_id:%s",
                room_id_.c_str());
            return;
        }
        for (const auto& user_json : users_json) {
            std::string user_id = user_json["userId"];
            std::string user_name = user_json["userName"];
            auto it = users_.find(user_id);
            if (it != users_.end()) {
                continue;
            }
            std::shared_ptr<RtcUser> new_user = std::make_shared<RtcUser>(room_id_, user_id, user_name, nullptr, logger_);
            new_user->SetRemote(true);
            users_[user_id] = new_user;
            // todo: handle pushers info
            json pushers_json = user_json["pushers"];
            if (!pushers_json.empty() && pushers_json.is_array()) {
                for (const auto& pusher_json : pushers_json) {
                    PushInfo push_info;
                    push_info.pusher_id_ = pusher_json["pusherId"].get<std::string>();
                    auto rtp_param_it = pusher_json.find("rtpParam");
                    if (rtp_param_it == pusher_json.end() || !rtp_param_it->is_object()) {
                        LogErrorf(logger_, "No rtpParam found in join response, room_id:%s, user_id:%s, pusher_id:%s",
                            room_id_.c_str(), user_id.c_str(), push_info.pusher_id_.c_str());
                        continue;
                    }
                    RtpSessionParam rtp_param;
                    rtp_param.FromJson(*rtp_param_it);
                    push_info.param_ = rtp_param;
                    std::string push_dump = push_info.Dump();
                    LogInfof(logger_, "JoinResponseFromPilotCenter, adding remote pusher, room_id:%s, user_id:%s, pusher_info:%s",
                        room_id_.c_str(), user_id.c_str(), push_dump.c_str());
                    new_user->AddPusher(push_info.pusher_id_, push_info);
                }
            }
            NotifyNewUser(user_id, user_name);
        }
    } catch(const std::exception& e) {
        LogErrorf(logger_, "JoinResponseFromPilotCenter exception, room_id:%s, error:%s",
            room_id_.c_str(), e.what());
    }
}

void Room::HandleNewUserNotificationFromCenter(json& data_json) {
    last_alive_ms_ = now_millisec();
    try {
        std::string user_id = data_json["userId"].get<std::string>();
        std::string user_name = data_json["userName"].get<std::string>();
        auto it = users_.find(user_id);
        if (it != users_.end()) {
            LogErrorf(logger_, "HandleNewUserNotificationFromCenter failed, user already exists, room_id:%s, user_id:%s",
                room_id_.c_str(), user_id.c_str());
            return;
        }
        if (g_rtc_event_log) {
            json evt_data;
            evt_data["event"] = "newUserFromCenter";
            evt_data["room_id"] = room_id_;
            evt_data["user_id"] = user_id;
            evt_data["user_name"] = user_name;
            g_rtc_event_log->Log("newUserFromCenter", evt_data);
        }
        std::shared_ptr<RtcUser> new_user = std::make_shared<RtcUser>(room_id_, user_id, user_name, nullptr, logger_);
        new_user->SetRemote(true);
        users_[user_id] = new_user;
        LogInfof(logger_, "HandleNewUserNotificationFromCenter, new remote user added, room_id:%s, user_id:%s, user_name:%s",
            room_id_.c_str(), user_id.c_str(), user_name.c_str());
        NotifyNewUser(user_id, user_name);
    } catch(const std::exception& e) {
        LogErrorf(logger_, "HandleNewUserNotificationFromCenter exception, room_id:%s, error:%s",
            room_id_.c_str(), e.what());
    }
}

void Room::HandleNewPusherNotificationFromCenter(json& data_json) {
    LogInfof(logger_, "HandleNewPusherNotificationFromCenter called, room_id:%s, data_json:%s",
        room_id_.c_str(), data_json.dump().c_str());
    last_alive_ms_ = now_millisec();
    try {
        auto remote_user_id = data_json["userId"].get<std::string>();
        auto remote_user_it = users_.find(remote_user_id);
        if (remote_user_it == users_.end()) {
            LogErrorf(logger_, "HandleNewPusherNotificationFromCenter failed, user not found, room_id:%s, user_id:%s",
                room_id_.c_str(), remote_user_id.c_str());
            return;
        }
        remote_user_it->second->SetRemote(true);

        std::string remote_user_name = remote_user_it->second->GetUserName();
        auto remote_user = remote_user_it->second;
        auto pushers_json = data_json["pushers"];
        std::vector<PushInfo> push_infos;
        for (const auto& pusher_json : pushers_json) {
            PushInfo push_info;
            push_info.pusher_id_ = pusher_json["pusherId"].get<std::string>();
            auto rtp_param_it = pusher_json.find("rtpParam");
            if (rtp_param_it == pusher_json.end() || !rtp_param_it->is_object()) {
                LogErrorf(logger_, "No rtpParam found in new pusher notification, room_id:%s, pusher_id:%s",
                    room_id_.c_str(), push_info.pusher_id_.c_str());
                continue;
            }
            RtpSessionParam rtp_param;
            rtp_param.FromJson(*rtp_param_it);
            push_info.param_ = rtp_param;
            push_infos.push_back(push_info);
            remote_user->AddPusher(push_info.pusher_id_, push_info);
        }
        //notify local users about new pusher
        json notify_json = json::object();
        notify_json["pushers"] = json::array();
        auto pushers_it = notify_json.find("pushers");

        for (const auto& push_info : push_infos) {
            json pusher_json = json::object();
            push_info.DumpJson(pusher_json);
            pushers_it->push_back(pusher_json);
        }
        notify_json["userId"] = remote_user_id;
        notify_json["userName"] = remote_user_name;
        notify_json["roomId"] = room_id_;
        for (auto notify_user_pair : users_) {
            if (notify_user_pair.first == remote_user_id) {
                continue;
            }

            auto notify_user = notify_user_pair.second;

            if (notify_user->GetRespCb()) {
                LogInfof(logger_, "Notify new pusher to local user, room_id:%s, newPusher data:%s",
                    room_id_.c_str(), notify_json.dump().c_str());
                notify_user->GetRespCb()->Notification("newPusher", notify_json);
            }
        }
        if (g_rtc_event_log) {
            json evt_data;
            evt_data["event"] = "newPusherFromCenter";
            evt_data["room_id"] = room_id_;
            evt_data["user_id"] = remote_user_id;
            evt_data["user_name"] = remote_user_name;
            evt_data["pushers"] = notify_json["pushers"];
            g_rtc_event_log->Log("newPusherFromCenter", evt_data);
        }
    } catch(const std::exception& e) {
        LogErrorf(logger_, "HandleNewPusherNotificationFromCenter exception, room_id:%s, error:%s",
            room_id_.c_str(), e.what());
    }
}

void Room::HandlePullRemoteStreamNotificationFromCenter(json& data_json) {
    last_alive_ms_ = now_millisec();

    try {
        std::string remote_udp_ip = data_json["udp_ip"].get<std::string>();
        int remote_udp_port = data_json["udp_port"].get<int>();

        std::string pusher_user_id = data_json["pusher_user_id"].get<std::string>();
        auto push_info_json = data_json["pushInfo"];
        if (g_rtc_event_log) {
            json evt_data;
            evt_data["event"] = "pullFromCenter";
            evt_data["room_id"] = room_id_;
            evt_data["pusher_user_id"] = pusher_user_id;
            evt_data["pull_info"] = push_info_json;
            g_rtc_event_log->Log("pullFromCenter", evt_data);
        }
        PushInfo push_info;
        push_info.pusher_id_ = push_info_json["pusherId"].get<std::string>();
        auto rtp_param_it = push_info_json.find("rtpParam");
        if (rtp_param_it == push_info_json.end() || !rtp_param_it->is_object()) {
            LogErrorf(logger_, "No rtpParam found in pull remote stream notification, room_id:%s, pusher_id:%s",
                room_id_.c_str(), push_info.pusher_id_.c_str());
            return;
        }
        RtpSessionParam rtp_param;
        rtp_param.FromJson(*rtp_param_it);
        push_info.param_ = rtp_param;

        //create send relay
        auto send_relay_it = pusher_user_id2sendRelay_.find(pusher_user_id);;
        std::shared_ptr<RtcSendRelay> send_relay_ptr;
        if (send_relay_it == pusher_user_id2sendRelay_.end()) {
            send_relay_ptr = std::make_shared<RtcSendRelay>(room_id_, 
                pusher_user_id, remote_udp_ip, remote_udp_port, this, loop_, logger_);
            pusher_user_id2sendRelay_[pusher_user_id] = send_relay_ptr;
        } else {
            send_relay_ptr = send_relay_it->second;
        }
        send_relay_ptr->AddPushInfo(push_info);
    } catch(const std::exception& e) {
        LogErrorf(logger_, "HandlePullRemoteStreamNotificationFromCenter exception, room_id:%s, error:%s",
            room_id_.c_str(), e.what());
    }
}

void Room::HandleUserDisconnectNotificationFromCenter(json& data_json) {
    try {
        std::string user_id = data_json["userId"].get<std::string>();
        LogInfof(logger_, "HandleUserDisconnectNotificationFromCenter called, room_id:%s, user_id:%s",
            room_id_.c_str(), user_id.c_str());
        auto user_it = users_.find(user_id);
        if (user_it == users_.end()) {
            LogErrorf(logger_, "HandleUserDisconnectNotificationFromCenter failed, user not found, room_id:%s, user_id:%s",
                room_id_.c_str(), user_id.c_str());
            return;
        }
        if (user_it->second->IsRemote()) {
            LogInfof(logger_, "HandleUserDisconnectNotificationFromCenter: remote user disconnected, room_id:%s, user_id:%s",
                room_id_.c_str(), user_id.c_str());
        } else {
            LogErrorf(logger_, "HandleUserDisconnectNotificationFromCenter failed: local user disconnect notification received, room_id:%s, user_id:%s",
                room_id_.c_str(), user_id.c_str());
            return;
        }
        if (g_rtc_event_log) {
            json evt_data;
            evt_data["event"] = "userDisconnectFromCenter";
            evt_data["room_id"] = room_id_;
            evt_data["user_id"] = user_id;
            g_rtc_event_log->Log("userDisconnectFromCenter", evt_data);
        }
        //notify local users about user disconnect
        json notify_json = json::object();
        notify_json["userId"] = user_id;
        notify_json["roomId"] = room_id_;
        for (auto notify_user_pair : users_) {
            auto notify_user = notify_user_pair.second;
            auto resp_cb = notify_user->GetRespCb();
            if (resp_cb) {
                LogInfof(logger_, "Notify user disconnect to local user, room_id:%s, remote userId:%s",
                    room_id_.c_str(), user_id.c_str());
                resp_cb->Notification("userDisconnect", notify_json);
            }
        }
    } catch(const std::exception& e) {
        LogErrorf(logger_, "HandleUserDisconnectNotificationFromCenter exception, room_id:%s, error:%s",
            room_id_.c_str(), e.what());
    }
    
}

void Room::HandleUserLeaveNotificationFromCenter(json& data_json) {
    try {
        std::string user_id = data_json["userId"].get<std::string>();
        LogInfof(logger_, "HandleUserLeaveNotificationFromCenter called, room_id:%s, user_id:%s",
            room_id_.c_str(), user_id.c_str());
        auto user_it = users_.find(user_id);
        if (user_it == users_.end()) {
            LogErrorf(logger_, "HandleUserLeaveNotificationFromCenter failed, user not found, room_id:%s, user_id:%s",
                room_id_.c_str(), user_id.c_str());
            return;
        }
        if (user_it->second->IsRemote()) {
            LogInfof(logger_, "HandleUserLeaveNotificationFromCenter: remote user left, room_id:%s, user_id:%s",
                room_id_.c_str(), user_id.c_str());
        } else {
            LogErrorf(logger_, "HandleUserLeaveNotificationFromCenter failed: local user leave notification received, room_id:%s, user_id:%s",
                room_id_.c_str(), user_id.c_str());
            return;
        }
        if (g_rtc_event_log) {
            json evt_data;
            evt_data["event"] = "userLeaveFromCenter";
            evt_data["room_id"] = room_id_;
            evt_data["user_id"] = user_id;
            g_rtc_event_log->Log("userLeaveFromCenter", evt_data);
        }
        //notify local users about user leave
        json notify_json = json::object();
        notify_json["userId"] = user_id;
        notify_json["roomId"] = room_id_;
        for (auto notify_user_pair : users_) {
            auto notify_user = notify_user_pair.second;
            if (notify_user->GetUserId() == user_id) {
                continue;
            }
            if (notify_user->IsRemote()) {
                continue;
            }
            auto resp_cb = notify_user->GetRespCb();
            if (resp_cb) {
                LogInfof(logger_, "Notify user leave to local user, room_id:%s, remote userId:%s",
                    room_id_.c_str(), user_id.c_str());
                resp_cb->Notification("userLeave", notify_json);
            }
        }
    } catch(const std::exception& e) {
        LogErrorf(logger_, "HandleUserLeaveNotificationFromCenter exception, room_id:%s, error:%s",
            room_id_.c_str(), e.what());
    }
}

void Room::HandleNotifyTextMessageFromCenter(json& data_json) {
    try {
        std::string from_user_id = data_json["userId"].get<std::string>();
        std::string from_user_name = data_json["userName"].get<std::string>();
        std::string message = data_json["message"].get<std::string>();
        LogInfof(logger_, "HandleNotifyTextMessageFromCenter called, room_id:%s, from_user_id:%s, from_user_name:%s, message:%s",
            room_id_.c_str(), from_user_id.c_str(), from_user_name.c_str(), message.c_str());
        NotifyTextMessage2LocalUsers(from_user_id, from_user_name, message);
    } catch(const std::exception& e) {
        LogErrorf(logger_, "HandleNotifyTextMessageFromCenter exception, room_id:%s, error:%s",
            room_id_.c_str(), e.what());
    }
}

std::shared_ptr<RtcRecvRelay> Room::CreateOrGetRecvRtcRelay(const std::string& pusher_user_id, const PushInfo& push_info) {
    std::shared_ptr<RtcRecvRelay> rtc_relay_ptr;

    auto user2relay_it = pusher_user_id2recvRelay_.find(pusher_user_id);
    if (user2relay_it != pusher_user_id2recvRelay_.end()) {
        rtc_relay_ptr = user2relay_it->second;
    } else {
        rtc_relay_ptr = std::make_shared<RtcRecvRelay>(room_id_, pusher_user_id, this, loop_, logger_);
        pusher_user_id2recvRelay_[pusher_user_id] = rtc_relay_ptr;
    }
    pusherId2recvRelay_[push_info.pusher_id_] = rtc_relay_ptr;

    return rtc_relay_ptr;
}

bool Room::IsAlive() {
    const int64_t ROOM_TIMEOUT_MS = 90*1000; //90 seconds
    int64_t now_ms = now_millisec();
    return (now_ms - last_alive_ms_) < ROOM_TIMEOUT_MS;
}

int Room::ReConnect(std::shared_ptr<RtcUser> new_user, int id, ProtooResponseI* resp_cb) {
    try {
        //notify existing users about the reconnection
        json notify_json = json::object();
        notify_json["userId"] = new_user->GetUserId();
        notify_json["userName"] = new_user->GetUserName();
        notify_json["roomId"] = room_id_;
        for (auto notify_user_pair : users_) {
            auto notify_user = notify_user_pair.second;
            auto notify_resp_cb = notify_user->GetRespCb();
            if (notify_user->GetUserId() == new_user->GetUserId()) {
                continue;
            }
            if (notify_user->IsRemote()) {
                continue;
            }
            
            if (notify_resp_cb) {
                LogInfof(logger_, "Notify user reconnection to local user, room_id:%s, userId:%s",
                    room_id_.c_str(), new_user->GetUserId().c_str());
                notify_resp_cb->Notification("userReConnect", notify_json);
            }
        }

        //send userReConect notification to pilot center
        if (pilot_client_) {
            json reconnect_data = json::object();
            reconnect_data["roomId"] = room_id_;
            reconnect_data["userId"] = new_user->GetUserId();
            reconnect_data["userName"] = new_user->GetUserName();
            LogInfof(logger_, "UserReConnect2PilotCenter, room_id:%s, user_id:%s",
                room_id_.c_str(), new_user->GetUserId().c_str());
            pilot_client_->AsyncNotification("userReConnect", reconnect_data);
        }

        //send reconnection response to the user
        json resp_json = json::object();
        resp_json["code"] = 0;
        resp_json["message"] = "join success";
        resp_json["users"] = json::array();

        for (const auto& pair : users_) {
            if (pair.first == new_user->GetUserId()) {
                continue;
            }
            auto user = pair.second;
            json user_json = json::object();
            user_json["userId"] = user->GetUserId();
            user_json["userName"] = user->GetUserName();

            std::map<std::string, PushInfo> pusher_map = user->GetPushers();
            user_json["pushers"] = json::array();
            for (const auto& pair : pusher_map) {
                json pusher_json = json::object();
                pair.second.DumpJson(pusher_json);
                user_json["pushers"].push_back(pusher_json);
            }
            resp_json["users"].push_back(user_json);
        }
        ProtooResponse resp(id, 0, "", resp_json);
        resp_cb->OnProtooResponse(resp);
    } catch(const std::exception& e) {
        LogErrorf(logger_, "ReConnect exception, room_id:%s, user_id:%s, error:%s",
            room_id_.c_str(), new_user->GetUserId().c_str(), e.what());
        return -1;
    }
    
    return 0;
}

void Room::NotifyTextMessage2PilotCenter(const std::string& from_user_id, const std::string& from_user_name, const std::string& message) {
    if (!pilot_client_) {
        return;
    }
    last_alive_ms_ = now_millisec();
    try {
        json notify_json = json::object();
        notify_json["userId"] = from_user_id;
        notify_json["userName"] = from_user_name;
        notify_json["message"] = message;
        notify_json["roomId"] = room_id_;
        LogInfof(logger_, "Notify Text Message to pilot center, room_id:%s, from_userId:%s, message:%s",
            room_id_.c_str(), from_user_id.c_str(), message.c_str());
        pilot_client_->AsyncNotification("textMessage", notify_json);
    } catch (const std::exception& e) {
        LogErrorf(logger_, "Notify Text Message to pilot center exception, room_id:%s, user_id:%s, error:%s",
            room_id_.c_str(), from_user_id.c_str(), e.what());
    }
}

void Room::NotifyTextMessage2LocalUsers(const std::string& from_user_id, const std::string& from_user_name, const std::string& message) {
    //notify all users in the room
    json notify_json = json::object();
    notify_json["userId"] = from_user_id;
    notify_json["userName"] = from_user_name;
    notify_json["message"] = message;
    notify_json["roomId"] = room_id_;
    for (auto notify_user_pair : users_) {
        auto notify_user = notify_user_pair.second;
        auto resp_cb = notify_user->GetRespCb();

        if (notify_user->GetUserId() == from_user_id) {
            continue;
        }
        if (resp_cb) {
            LogInfof(logger_, "Notify text message to user, room_id:%s, from_userId:%s, to_userId:%s, message:%s",
                room_id_.c_str(), from_user_id.c_str(), notify_user->GetUserId().c_str(), message.c_str());
            resp_cb->Notification("textMessage", notify_json);
        }
    }
}

} // namespace cpp_streamer