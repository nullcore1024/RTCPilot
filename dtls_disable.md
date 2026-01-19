# 禁用WebRTC媒体流加密（RTP/RTCP无加密传输）方案

## 1. 概述

WebRTC默认使用DTLS（Datagram Transport Layer Security）加密RTP和RTCP媒体流。在某些特定场景下（如安全的内部网络环境），可能需要禁用加密以提高性能或简化部署。本文将详细介绍如何在RTCPilot项目中实现RTP/RTCP的无加密传输。

## 2. 加密控制的核心机制

WebRTC的媒体流加密主要通过SDP（Session Description Protocol）信令交互来协商和控制。要禁用加密，需要在SDP交互阶段进行以下关键修改：

### 2.1 SDP协议字段修改

**加密传输（默认）：**
```
m=audio 9 UDP/TLS/RTP/SAVPF 111 63 9 0 8 13 110 126
m=video 9 UDP/TLS/RTP/SAVPF 96 97 103 104 107 108 109 114 115 116 117 118 39 40 45 46 98 99 100 101 119 120 123 124 125
```

**无加密传输（修改后）：**
```
m=audio 9 UDP/RTP/SAVPF 111 63 9 0 8 13 110 126
m=video 9 UDP/RTP/SAVPF 96 97 103 104 107 108 109 114 115 116 117 118 39 40 45 46 98 99 100 101 119 120 123 124 125
```

**关键区别：** 将`UDP/TLS/RTP/SAVPF`协议改为`UDP/RTP/SAVPF`，移除了`TLS`部分。

### 2.2 移除DTLS相关字段

需要从SDP中移除以下与DTLS加密相关的字段：

1. **a=fingerprint**：DTLS证书指纹
   ```
a=fingerprint:sha-256 77:B0:05:EC:26:1B:9F:85:B7:83:69:0A:57:2F:55:81:9C:60:1A:F7:A6:54:CC:A7:DF:16:61:E1:F8:72:39:F0
```

2. **a=setup**：DTLS角色
   ```
a=setup:passive
```

## 3. RTCPilot项目实现方案

### 3.1 配置文件修改

在项目配置文件中添加一个开关，用于控制是否启用加密：

```yaml
webrtc:
  enable_dtls: false  # false表示禁用DTLS加密
```

### 3.2 代码修改点

#### 3.2.1 SDP生成模块（rtc_sdp.cpp）

修改`GenAnswerSdp`函数，根据配置决定是否添加DTLS相关字段和使用TLS协议：

```cpp
// 在rtc_sdp.cpp中
RTCSdpInfo* RTCSdpInfo::GenAnswerSdp(RTCSdpFilter* filter, uint8_t setup, uint8_t direction, 
                                      const std::string& local_ufrag, const std::string& local_pwd,
                                      const std::string& local_fp) {
    // ... 现有代码
    
    // 根据配置决定是否使用加密协议
    bool enable_dtls = Config::Instance().webrtc_cfg_.enable_dtls;
    std::string proto = enable_dtls ? "UDP/TLS/RTP/SAVPF" : "UDP/RTP/SAVPF";
    
    // 为每个媒体流设置协议
    for (auto& media : answer->medias) {
        media.proto = proto;
        
        // 仅当启用DTLS时，才添加DTLS相关字段
        if (enable_dtls) {
            // 添加指纹
            media.fingerprints.push_back(RTCSdpFingerprint{"sha-256", local_fp});
            
            // 添加setup
            media.setup = setup;
        } else {
            // 禁用DTLS时，确保没有DTLS相关字段
            media.fingerprints.clear();
        }
    }
    
    // ... 现有代码
    
    return answer;
}
```

#### 3.2.2 SDP解析模块（rtc_sdp.cpp）

修改SDP解析逻辑，确保客户端的Offer SDP中即使有DTLS相关字段，服务器也能正确处理：

```cpp
// 在rtc_sdp.cpp的SDP解析函数中
if (line.find("a=fingerprint") != std::string::npos && Config::Instance().webrtc_cfg_.enable_dtls) {
    // 仅当启用DTLS时，才解析指纹
    ParseFingerprint(line, current_media);
}

if (line.find("a=setup") != std::string::npos && Config::Instance().webrtc_cfg_.enable_dtls) {
    // 仅当启用DTLS时，才解析setup
    ParseSetup(line, current_media);
}
```

#### 3.2.3 WebRTC会话管理（webrtc_session.cpp）

修改WebRTC会话初始化逻辑，根据配置决定是否创建DTLS会话：

```cpp
// 在webrtc_session.cpp中
bool WebRtcSession::Init() {
    // ... 现有代码
    
    // 仅当启用DTLS时，才创建DTLS会话
    if (Config::Instance().webrtc_cfg_.enable_dtls) {
        dtls_session_ = std::make_shared<DtlsSession>(dtls_role_, cert_file_, key_file_, this);
        if (!dtls_session_->Init()) {
            LogErrorf("Failed to init dtls session");
            return false;
        }
        
        rtp_session_->SetRtpCallback([this](Media_Packet_Ptr pkt_ptr) {
            dtls_session_->SendRtp(pkt_ptr->Buffer, pkt_ptr->Size);
        });
        
        rtp_session_->SetRtcpCallback([this](Media_Packet_Ptr pkt_ptr) {
            dtls_session_->SendRtcp(pkt_ptr->Buffer, pkt_ptr->Size);
        });
    } else {
        // 禁用DTLS时，直接发送RTP/RTCP包
        rtp_session_->SetRtpCallback([this](Media_Packet_Ptr pkt_ptr) {
            SendRtp(pkt_ptr->Buffer, pkt_ptr->Size);
        });
        
        rtp_session_->SetRtcpCallback([this](Media_Packet_Ptr pkt_ptr) {
            SendRtcp(pkt_ptr->Buffer, pkt_ptr->Size);
        });
    }
    
    // ... 现有代码
    
    return true;
}
```

#### 3.2.4 RTP会话处理（rtp_session.cpp）

修改RTP会话接收逻辑，根据配置决定是否需要解密：

```cpp
// 在rtp_session.cpp中
void RtpSession::OnUdpPacket(uint8_t* data, size_t len, 
                             int64_t recv_time, 
                             const std::string& peer_ip, 
                             int peer_port) {
    // ... 现有代码
    
    // 检查是否是RTP或RTCP包
    if (IsRtpPacket(data, len)) {
        if (Config::Instance().webrtc_cfg_.enable_dtls) {
            // 启用DTLS时，需要先解密
            dtls_session_->OnDtlsPacket(data, len);
        } else {
            // 禁用DTLS时，直接处理RTP包
            OnRtpPacket(data, len);
        }
    } else if (IsRtcpPacket(data, len)) {
        if (Config::Instance().webrtc_cfg_.enable_dtls) {
            // 启用DTLS时，需要先解密
            dtls_session_->OnDtlsPacket(data, len);
        } else {
            // 禁用DTLS时，直接处理RTCP包
            OnRtcpPacket(data, len);
        }
    }
}
```

## 4. 客户端兼容性处理

不同的WebRTC客户端对无加密传输的支持程度可能不同。以下是一些兼容性考虑：

### 4.1 主流浏览器支持

- **Chrome/Chromium**：支持无加密传输，但需要在URL中添加特殊参数：
  ```
  chrome://flags/#enable-webrtc-non-secure-local-media
  ```
  启用该选项后，Chrome才允许在非HTTPS页面或无加密的WebRTC连接。

- **Firefox**：支持无加密传输，但需要在about:config中设置：
  ```
  media.peerconnection.ice.no_host=false
  media.peerconnection.ice.no_server=false
  ```

- **Safari**：对无加密传输的支持有限，通常需要额外配置。

### 4.2 客户端SDP协商处理

确保客户端发送的Offer SDP能被服务器正确处理：

1. 客户端Offer SDP中可能仍然包含DTLS相关字段
2. 服务器在生成Answer SDP时，需要忽略这些字段（如果禁用了DTLS）
3. 确保客户端能够接受服务器返回的不含DTLS信息的Answer SDP

## 5. 配置示例

### 5.1 配置文件（config.yaml）

```yaml
webrtc:
  enable_dtls: false  # 禁用DTLS加密
  listen_ip: 0.0.0.0
  listen_port: 8080
  
  # 其他WebRTC配置...

# 其他模块配置...
```

### 5.2 禁用加密后的SDP示例

**Answer SDP（禁用DTLS后）：**
```
v=0
o=- 3509298118 3509298118 IN IP4 0.0.0.0
s=-
t=0 0
a=group:BUNDLE 0
a=msid-semantic: WMS *
m=video 8000 UDP/RTP/SAVPF 109 114
a=rtcp:8001 IN IP4 0.0.0.0
a=rtpmap:109 H264/90000
a=rtpmap:114 rtx/90000
a=fmtp:109 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f
a=fmtp:114 apt=109
a=rtcp-fb:109 goog-remb
a=rtcp-fb:109 transport-cc
a=rtcp-fb:109 ccm fir
a=rtcp-fb:109 nack
a=rtcp-fb:109 nack pli
a=extmap:2 http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time
a=extmap:4 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01
a=extmap:9 urn:ietf:params:rtp-hdrext:sdes:mid
a=mid:0
a=sendonly
a=ice-lite
a=ice-ufrag:0976943898583609
a=ice-pwd:57833878120097553383154149854999
a=candidate:34580690 1 UDP 10001 192.168.1.86 8000 typ host
a=candidate:34580690 2 UDP 10000 192.168.1.86 8001 typ host
a=ssrc-group:FID 2934887430 384920610
a=ssrc:2934887430 cname:cname_384920610
a=ssrc:2934887430 msid:89426f95-33be-6632-cd81-2e4480e12614 b346455a-e3dd-e68c-17b3-c19ca6ef7508
a=ssrc:384920610 cname:cname_384920610
a=ssrc:384920610 msid:89426f95-33be-6632-cd81-2e4480e12614 b346455a-e3dd-e68c-17b3-c19ca6ef7508
a=rtcp-mux
a=rtcp-rsize
```

**关键变化：**
- 协议从`UDP/TLS/RTP/SAVPF`改为`UDP/RTP/SAVPF`
- 移除了`a=fingerprint`和`a=setup`字段
- 保留了ICE相关的`a=ice-ufrag`和`a=ice-pwd`字段用于连接建立

## 6. 安全注意事项

禁用DTLS加密会带来以下安全风险：

1. **媒体流窃听风险**：RTP/RTCP数据包在网络上以明文传输，任何人都可以捕获和查看
2. **数据篡改风险**：攻击者可以修改或注入媒体流数据
3. **身份验证缺失**：无法验证媒体流的来源和完整性

**强烈建议**仅在以下场景中考虑禁用加密：
- 完全信任的内部网络环境
- 测试和开发环境
- 特殊的性能敏感应用

在生产环境中，特别是公网部署时，强烈建议保持DTLS加密启用状态。

## 7. 性能影响

禁用DTLS加密可以带来以下性能提升：

1. **减少CPU开销**：避免了加密和解密的计算成本
2. **降低延迟**：减少了加密处理的延迟
3. **简化部署**：无需管理和配置TLS证书

具体性能提升取决于网络环境、编解码器类型和数据速率等因素。

## 8. 实现验证

修改完成后，可以通过以下方式验证无加密传输是否成功：

1. **网络抓包分析**：使用Wireshark等工具捕获网络流量，确认RTP/RTCP数据包为明文
2. **日志检查**：确保服务器日志中没有DTLS相关的错误信息
3. **功能测试**：验证媒体流能够正常传输和播放
4. **性能测试**：对比启用和禁用加密时的性能差异

## 9. 总结

RTCPilot项目中实现RTP/RTCP无加密传输的核心是在SDP信令交互阶段就控制加密协商：

1. 在SDP中使用非加密协议（UDP/RTP/SAVPF）
2. 移除或不添加DTLS相关字段
3. 相应地修改服务器端的媒体处理逻辑
4. 确保与客户端的兼容性

通过以上修改，可以实现WebRTC媒体流的无加密传输，满足特定场景的需求。但需要充分考虑安全性和兼容性问题，谨慎决定是否在生产环境中禁用加密。
