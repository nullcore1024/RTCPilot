# SDP (Session Description Protocol) 详解

SDP是一种用于描述多媒体会话的文本协议，在WebRTC中用于协商媒体能力和建立连接。本文将详细介绍SDP Offer和Answer中的各个字段及其用途。

## 1. SDP基本结构

SDP由一系列行组成，每行以一个字符的类型标识开头，后面跟着等号(=)和具体内容。SDP分为会话级(session-level)和媒体级(media-level)两部分：

- **会话级**：描述整个会话的基本信息，如会话名称、时间、加密参数等
- **媒体级**：描述具体的媒体流，如音频、视频，每个媒体流有自己的参数

## 2. 会话级字段说明

### 2.1 v= (协议版本)
```
v=0
```
- **用途**：指定SDP协议版本
- **说明**：目前只有版本0有效

### 2.2 o= (会话发起者)
```
o=- 6453005456405246960 2 IN IP4 127.0.0.1
```
- **格式**：`o=<username> <session id> <version> <network type> <address type> <address>`
- **用途**：唯一标识会话发起者
- **参数说明**：
  - `username`：通常为"-"，表示匿名
  - `session id`：会话唯一标识符
  - `version`：会话版本号，每次修改SDP时递增
  - `network type`：通常为"IN"(Internet)
  - `address type`："IP4"或"IP6"
  - `address`：发起者IP地址

### 2.3 s= (会话名称)
```
s=-
```
- **用途**：指定会话名称
- **说明**：在WebRTC中通常为"-"

### 2.4 t= (会话时间)
```
t=0 0
```
- **格式**：`t=<start time> <stop time>`
- **用途**：指定会话的开始和结束时间
- **说明**：WebRTC中通常都为0，表示会话没有时间限制

### 2.5 a=group:BUNDLE (媒体捆绑)
```
a=group:BUNDLE 0 1
```
- **用途**：指定哪些媒体流应该捆绑在同一个传输通道上
- **说明**：数字表示媒体流的mid(媒体ID)

### 2.6 a=extmap-allow-mixed (扩展头混合)
```
a=extmap-allow-mixed
```
- **用途**：允许混合使用不同版本的RTP扩展头

### 2.7 a=msid-semantic (媒体流标识语义)
```
a=msid-semantic: WMS fa22dd6c-0593-4715-b600-a888210d390d
```
- **用途**：定义媒体流标识的语义
- **说明**："WMS"表示WebRTC Media Stream，后面的UUID是MediaStream的ID

## 3. 媒体级字段说明

### 3.1 m= (媒体描述)
```
m=audio 9 UDP/TLS/RTP/SAVPF 111 63 9 0 8 13 110 126
m=video 9 UDP/TLS/RTP/SAVPF 96 97 103 104 107 108 109 114 115 116 117 118 39 40 45 46 98 99 100 101 119 120 123 124 125
```
- **格式**：`m=<media> <port> <proto> <fmt>...`
- **用途**：描述媒体流的基本信息
- **参数说明**：
  - `media`：媒体类型(audio, video, text等)
  - `port`：传输端口(WebRTC中通常为9，实际端口通过ICE协商)
  - `proto`：传输协议(UDP/TLS/RTP/SAVPF表示安全的RTP传输)
  - `fmt`：支持的媒体格式(payload type)

### 3.2 c= (连接信息)
```
c=IN IP4 0.0.0.0
```
- **格式**：`c=<network type> <address type> <connection address>`
- **用途**：指定媒体流的连接信息
- **说明**：WebRTC中通常为0.0.0.0，实际IP通过ICE协商

### 3.3 a=rtcp (RTCP信息)
```
a=rtcp:9 IN IP4 0.0.0.0
```
- **用途**：指定RTCP(实时传输控制协议)的传输信息

### 3.4 a=ice-ufrag/a=ice-pwd (ICE认证)
```
a=ice-ufrag:cT0d
a=ice-pwd:/y3DCq5BQRaTblvUe0J6LIAE
```
- **用途**：ICE(Interactive Connectivity Establishment)协议的认证信息
- **说明**：用于在NAT环境下建立连接

### 3.5 a=ice-options (ICE选项)
```
a=ice-options:trickle
```
- **用途**：指定ICE的额外选项
- **说明**："trickle"表示支持Trickle ICE，即实时交换ICE候选者

### 3.6 a=fingerprint (DTLS指纹)
```
a=fingerprint:sha-256 77:B0:05:EC:26:1B:9F:85:B7:83:69:0A:57:2F:55:81:9C:60:1A:F7:A6:54:CC:A7:DF:16:61:E1:F8:72:39:F0
```
- **用途**：指定DTLS(Datagram Transport Layer Security)证书的指纹
- **说明**：用于加密媒体流

### 3.7 a=setup (DTLS角色)
```
// Offer中
a=setup:actpass

// Answer中
a=setup:passive
```
- **用途**：指定DTLS的角色
- **说明**：
  - `actpass`：主动/被动模式(Offer中使用)
  - `passive`：被动模式(Answer中使用)
  - `active`：主动模式

### 3.8 a=mid (媒体ID)
```
a=mid:0
a=mid:1
```
- **用途**：为每个媒体流分配唯一标识符
- **说明**：与BUNDLE组中的数字对应

### 3.9 a=extmap (RTP扩展头)
```
a=extmap:1 urn:ietf:params:rtp-hdrext:ssrc-audio-level
a=extmap:2 http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time
a=extmap:3 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01
```
- **格式**：`a=extmap:<id> <uri>`
- **用途**：指定RTP扩展头
- **说明**：用于传输额外的媒体信息，如音频电平、绝对发送时间等

### 3.10 a=sendrecv/sendonly/recvonly/inactive (媒体方向)
```
// Offer中
a=sendrecv

// Answer中
a=sendonly
```
- **用途**：指定媒体流的传输方向
- **说明**：
  - `sendrecv`：发送和接收
  - `sendonly`：只发送
  - `recvonly`：只接收
  - `inactive`：不发送也不接收

### 3.11 a=msid (媒体流ID)
```
a=msid:fa22dd6c-0593-4715-b600-a888210d390d 740d5089-2267-4b3e-b6e6-65bd3808ea6f
```
- **格式**：`a=msid:<media-stream-id> <media-track-id>`
- **用途**：将SDP中的媒体流与WebRTC的MediaStream和MediaTrack关联

### 3.12 a=rtcp-mux (RTCP复用)
```
a=rtcp-mux
```
- **用途**：指定RTCP与RTP共用同一个端口

### 3.13 a=rtcp-rsize (RTCP减小尺寸)
```
a=rtcp-rsize
```
- **用途**：支持减小RTCP包的大小

### 3.14 a=rtpmap (RTP映射)
```
a=rtpmap:111 opus/48000/2
a=rtpmap:96 VP8/90000
a=rtpmap:109 H264/90000
a=rtpmap:97 rtx/90000
```
- **格式**：`a=rtpmap:<payload type> <encoding>/<clock rate>/<channels>`
- **用途**：将payload type映射到具体的编解码器
- **参数说明**：
  - `payload type`：媒体格式编号
  - `encoding`：编解码器名称
  - `clock rate`：时钟频率(Hz)
  - `channels`：声道数(仅音频)

### 3.15 a=rtcp-fb (RTCP反馈)
```
a=rtcp-fb:96 goog-remb
a=rtcp-fb:96 transport-cc
a=rtcp-fb:96 ccm fir
a=rtcp-fb:96 nack
a=rtcp-fb:96 nack pli
```
- **格式**：`a=rtcp-fb:<payload type> <feedback type>`
- **用途**：指定支持的RTCP反馈机制
- **常见反馈类型**：
  - `goog-remb`：Google拥塞控制反馈
  - `transport-cc`：传输层拥塞控制
  - `ccm fir`：完整帧请求
  - `nack`：否定确认
  - `nack pli`：图片丢失指示

### 3.16 a=fmtp (格式参数)
```
a=fmtp:111 minptime=10;useinbandfec=1
a=fmtp:109 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f
a=fmtp:114 apt=109
```
- **格式**：`a=fmtp:<payload type> <params>`
- **用途**：为特定编解码器指定额外参数
- **常见参数**：
  - `minptime`：最小打包时间(opus)
  - `useinbandfec`：是否使用带内FEC(opus)
  - `profile-level-id`：H.264编解码器配置
  - `packetization-mode`：H.264打包模式
  - `apt`：关联的payload type(用于RTX重传)

### 3.17 a=ssrc (同步源)
```
a=ssrc:3816472125 cname:6YGQFLAdDyF8WBK8
a=ssrc:3816472125 msid:fa22dd6c-0593-4715-b600-a888210d390d 740d5089-2267-4b3e-b6e6-65bd3808ea6f
```
- **用途**：指定RTP流的同步源
- **参数说明**：
  - `ssrc`：同步源标识符
  - `cname`：贡献源名称(用于关联同一用户的多个流)
  - `msid`：媒体流ID(与a=msid对应)

### 3.18 a=ssrc-group (SSRC组)
```
a=ssrc-group:FID 3188473065 1684074237
```
- **格式**：`a=ssrc-group:<semantics> <ssrc>...`
- **用途**：将相关的SSRC分组
- **说明**：
  - `FID`：Flow Identification，用于关联媒体流和重传流

### 3.19 a=ice-lite (ICE Lite模式)
```
a=ice-lite
```
- **用途**：表示使用ICE Lite模式
- **说明**：通常在服务器端使用，减少ICE处理的复杂度

### 3.20 a=candidate (ICE候选者)
```
a=candidate:34580690 1 UDP 10001 192.168.1.86 8090 typ host
```
- **格式**：`a=candidate:<foundation> <component> <proto> <priority> <ip> <port> <type>...`
- **用途**：提供ICE候选者信息
- **参数说明**：
  - `foundation`：候选者基础
  - `component`：组件ID(1=RTP, 2=RTCP)
  - `proto`：传输协议(UDP/TCP)
  - `priority`：候选者优先级
  - `ip`：IP地址
  - `port`：端口
  - `type`：候选者类型(host, srflx, prflx, relay)

## 4. Offer与Answer的主要区别

1. **媒体方向**：Offer通常使用`sendrecv`，Answer根据需求选择`sendrecv`、`sendonly`或`recvonly`

2. **DTLS角色**：Offer使用`actpass`，Answer使用`passive`或`active`

3. **媒体格式**：Answer通常只选择Offer中支持的部分媒体格式，而不是全部

4. **ICE候选者**：Answer通常包含实际的网络候选者信息，而Offer可能没有

5. **ssrc值**：Offer和Answer使用不同的ssrc值

## 5. WebRTC特定扩展

WebRTC使用了一些SDP的扩展字段来支持其特有的功能：

- **a=rtcp-fb:XX goog-remb**：Google拥塞控制反馈
- **a=extmap:3 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01**：传输层拥塞控制扩展
- **a=ice-options:trickle**：支持Trickle ICE
- **a=group:BUNDLE**：媒体流捆绑
- **a=msid**：媒体流标识

## 6. 示例分析

### 6.1 Offer中的音频流
```
m=audio 9 UDP/TLS/RTP/SAVPF 111 63 9 0 8 13 110 126
c=IN IP4 0.0.0.0
a=rtcp:9 IN IP4 0.0.0.0
a=ice-ufrag:cT0d
a=ice-pwd:/y3DCq5BQRaTblvUe0J6LIAE
a=ice-options:trickle
a=fingerprint:sha-256 77:B0:05:EC:26:1B:9F:85:B7:83:69:0A:57:2F:55:81:9C:60:1A:F7:A6:54:CC:A7:DF:16:61:E1:F8:72:39:F0
a=setup:actpass
a=mid:0
a=extmap:1 urn:ietf:params:rtp-hdrext:ssrc-audio-level
a=extmap:2 http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time
a=extmap:3 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01
a=extmap:4 urn:ietf:params:rtp-hdrext:sdes:mid
a=sendrecv
a=msid:fa22dd6c-0593-4715-b600-a888210d390d 740d5089-2267-4b3e-b6e6-65bd3808ea6f
a=rtcp-mux
a=rtcp-rsize
a=rtpmap:111 opus/48000/2
a=rtcp-fb:111 transport-cc
a=fmtp:111 minptime=10;useinbandfec=1
a=rtpmap:63 red/48000/2
a=fmtp:63 111/111
a=rtpmap:9 G722/8000
a=rtpmap:0 PCMU/8000
a=rtpmap:8 PCMA/8000
a=rtpmap:13 CN/8000
a=rtpmap:110 telephone-event/48000
a=rtpmap:126 telephone-event/8000
a=ssrc:3816472125 cname:6YGQFLAdDyF8WBK8
a=ssrc:3816472125 msid:fa22dd6c-0593-4715-b600-a888210d390d 740d5089-2267-4b3e-b6e6-65bd3808ea6f
```

### 6.2 Answer中的视频流
```
m=video 9 UDP/TLS/RTP/SAVPF 109 114
c=IN IP4 0.0.0.0
a=rtpmap:109 H264/90000
a=rtpmap:114 rtx/90000
a=fmtp:109 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f
a=fmtp:114 apt=109
a=rtcp-fb:109 goog-remb
a=rtcp-fb:109 transport-cc
a=rtcp-fb:109 ccm fir
a=rtcp-fb:109 nack
a=rtcp-fb:109 nack pli
a=rtcp:9 IN IP4 0.0.0.0
a=extmap:2 http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time
a=extmap:4 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01
a=extmap:9 urn:ietf:params:rtp-hdrext:sdes:mid
a=setup:passive
a=mid:0
a=sendonly
a=ice-lite
a=ice-ufrag:0976943898583609
a=ice-pwd:57833878120097553383154149854999
a=fingerprint:sha-256 E8:7A:3E:9F:DF:B2:FD:09:3E:B2:28:7A:36:4E:C8:82:91:11:7C:16:8C:2C:1B:FA:6B:18:8B:F5:72:67:45:5C
a=candidate:34580690 1 UDP 10001 192.168.1.86 8090 typ host
a=ssrc-group:FID 2934887430 384920610
a=ssrc:2934887430 cname:cname_384920610
a=ssrc:2934887430 msid:89426f95-33be-6632-cd81-2e4480e12614 b346455a-e3dd-e68c-17b3-c19ca6ef7508
a=ssrc:384920610 cname:cname_384920610
a=ssrc:384920610 msid:89426f95-33be-6632-cd81-2e4480e12614 b346455a-e3dd-e68c-17b3-c19ca6ef7508
a=rtcp-mux
a=rtcp-rsize
```

## 8. P2P与SFU模式的SDP配置差异

WebRTC支持多种部署模式，其中最常见的是P2P（点对点）模式和SFU（Selective Forwarding Unit）模式。SDP Answer中的几个关键配置决定了客户端PeerConnection将使用哪种模式。

### 8.1 SFU模式的典型SDP特征

在RTCPilot项目中，主要使用SFU模式，SDP Answer具有以下特征：

#### 8.1.1 ICE Lite模式
```
a=ice-lite
```
- **用途**：表示使用ICE Lite模式，这是SFU服务器的典型配置
- **说明**：ICE Lite服务器只响应ICE请求，不主动发起连接尝试，减少了服务器的处理复杂度

#### 8.1.2 媒体方向限制
```
// 发送者的Answer
a=sendonly

// 接收者的Answer  
a=recvonly
```
- **用途**：限制媒体流的传输方向
- **说明**：在SFU模式下，客户端通常要么是发送者（sendonly），要么是接收者（recvonly），而不是同时双向传输（sendrecv）

#### 8.1.3 预配置的ICE候选者
```
a=candidate:34580690 1 UDP 10001 192.168.1.86 8090 typ host
```
- **用途**：提供服务器的网络连接信息
- **说明**：在SFU模式下，SDP Answer中包含的是服务器的预配置IP地址和端口，而不是客户端的P2P候选者
- **配置来源**：这些候选者来自项目配置文件中的`rtc_candidates_`配置项

#### 8.1.4 媒体格式筛选
```
// 在Answer中只选择部分Offer中支持的媒体格式
a=rtpmap:109 H264/90000
a=rtpmap:114 rtx/90000
```
- **用途**：优化媒体格式选择
- **说明**：SFU通常会根据服务器能力和网络条件筛选媒体格式，而不是使用Offer中的所有格式

### 8.2 P2P模式的典型SDP特征

如果要使用P2P模式，SDP Answer会有以下不同特征：

1. **媒体方向为sendrecv**：
   ```
a=sendrecv
```
   - 表示客户端同时发送和接收媒体流

2. **完整的ICE候选者集合**：
   ```
// 本地候选者
a=candidate:1 1 UDP 2130706431 192.168.1.100 50000 typ host

// 服务器反射候选者  
a=candidate:2 1 UDP 1694498815 203.0.113.1 60000 typ srflx raddr 192.168.1.100 rport 50000

// 对等反射候选者

a=candidate:3 1 UDP 1021703167 10.0.0.1 70000 typ prflx raddr 192.168.1.100 rport 50000

// 中继候选者

a=candidate:4 1 UDP 250472959 198.51.100.1 80000 typ relay raddr 203.0.113.1 rport 60000
   ```
   - 包含多种类型的ICE候选者，用于在不同网络条件下建立P2P连接

3. **DTLS角色为actpass**：
   ```
a=setup:actpass
```
   - 表示支持主动/被动模式的DTLS连接

### 8.3 RTCPilot项目中的模式选择

在RTCPilot项目中，连接模式主要通过以下配置决定：

1. **配置文件中的RTC候选者**：
   ```cpp
   // 在room.cpp中
   for (auto & candidate : Config::Instance().rtc_candidates_) {
       IceCandidate ice_candidate;
       ice_candidate.ip_ = candidate.candidate_ip_;
       ice_candidate.port_ = candidate.port_;
       // ... 其他配置
       answer_sdp->ice_candidates_.push_back(ice_candidate);
   }
   ```

2. **SDP Answer生成时的参数**：
   ```cpp
   // 在room.cpp中
   auto answer_sdp = sdp_ptr->GenAnswerSdp(g_sdp_answer_filter, 
       RTC_SETUP_PASSIVE,  // DTLS角色为被动模式
       DIRECTION_RECVONLY, // 媒体方向为仅接收
       local_ufrag,
       local_pwd,
       local_fp
       );
   ```

3. **媒体方向的动态设置**：
   - 根据客户端是推流者（sendonly）还是拉流者（recvonly）动态设置媒体方向

## 9. 总结

SDP是WebRTC中媒体协商的核心协议，通过详细的字段定义了媒体能力、传输参数和网络配置。理解SDP的各个字段对于调试WebRTC连接问题和优化媒体性能非常重要。

在RTCPilot项目中，SDP Answer通过以下方式决定连接模式：

1. **`a=ice-lite`字段**：表示使用SFU模式
2. **媒体方向设置**：`sendonly`/`recvonly`表示SFU模式，`sendrecv`表示P2P模式
3. **ICE候选者配置**：预配置的服务器候选者表示SFU模式，完整的候选者集合表示P2P模式

开发者可以通过修改配置文件中的`rtc_candidates_`和SDP生成参数来灵活切换P2P和SFU模式。
