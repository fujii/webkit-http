/*
 * Copyright (C) 2017 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "LibWebRTCMacros.h"
#include <pal/SessionID.h>
#include <wtf/CompletionHandler.h>
#include <wtf/EnumTraits.h>
#include <wtf/Expected.h>
#include <wtf/UniqueRef.h>
#include <wtf/text/WTFString.h>

#if USE(LIBWEBRTC)

#include <webrtc/api/peerconnectioninterface.h>
#include <webrtc/api/video_codecs/video_decoder_factory.h>
#include <webrtc/api/video_codecs/video_encoder_factory.h>
#include <webrtc/rtc_base/scoped_ref_ptr.h>
#include <webrtc/media/engine/webrtcvideodecoderfactory.h>
#include <webrtc/media/engine/webrtcvideoencoderfactory.h>

namespace rtc {
class NetworkManager;
class PacketSocketFactory;
class Thread;
}

namespace webrtc {
class PeerConnectionFactoryInterface;
}
#endif

namespace WebCore {

class LibWebRTCAudioModule;

enum class MDNSRegisterError { NotImplemented, BadParameter, DNSSD, Internal, Timeout };

class WEBCORE_EXPORT LibWebRTCProvider {
public:
    static UniqueRef<LibWebRTCProvider> create();

    virtual ~LibWebRTCProvider() = default;

    static bool webRTCAvailable();

    virtual void setActive(bool) { };

    virtual void setH264HardwareEncoderAllowed(bool) { }

    using IPAddressOrError = Expected<String, MDNSRegisterError>;
    using MDNSNameOrError = Expected<String, MDNSRegisterError>;

    virtual void unregisterMDNSNames(uint64_t documentIdentifier)
    {
        UNUSED_PARAM(documentIdentifier);
    }

    virtual void registerMDNSName(PAL::SessionID, uint64_t documentIdentifier, const String& ipAddress, CompletionHandler<void(MDNSNameOrError&&)>&& callback)
    {
        UNUSED_PARAM(documentIdentifier);
        UNUSED_PARAM(ipAddress);
        callback(makeUnexpected(MDNSRegisterError::NotImplemented));
    }

    virtual void resolveMDNSName(PAL::SessionID, const String& name, CompletionHandler<void(IPAddressOrError&&)>&& callback)
    {
        UNUSED_PARAM(name);
        callback(makeUnexpected(MDNSRegisterError::NotImplemented));
    }

#if USE(LIBWEBRTC)
    virtual rtc::scoped_refptr<webrtc::PeerConnectionInterface> createPeerConnection(webrtc::PeerConnectionObserver&, webrtc::PeerConnectionInterface::RTCConfiguration&&);

    webrtc::PeerConnectionFactoryInterface* factory();

    // FIXME: Make these methods not static.
    static void callOnWebRTCNetworkThread(Function<void()>&&);
    static void callOnWebRTCSignalingThread(Function<void()>&&);

    // Used for mock testing
    void setPeerConnectionFactory(rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>&&);

    void disableEnumeratingAllNetworkInterfaces() { m_enableEnumeratingAllNetworkInterfaces = false; }
    void enableEnumeratingAllNetworkInterfaces() { m_enableEnumeratingAllNetworkInterfaces = true; }

protected:
    LibWebRTCProvider() = default;

    rtc::scoped_refptr<webrtc::PeerConnectionInterface> createPeerConnection(webrtc::PeerConnectionObserver&, rtc::NetworkManager&, rtc::PacketSocketFactory&, webrtc::PeerConnectionInterface::RTCConfiguration&&);

    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> createPeerConnectionFactory(rtc::Thread* networkThread, rtc::Thread* signalingThread, LibWebRTCAudioModule*);
    virtual std::unique_ptr<webrtc::VideoDecoderFactory> createDecoderFactory() { return nullptr; }
    virtual std::unique_ptr<webrtc::VideoEncoderFactory> createEncoderFactory() { return nullptr; }

    bool m_enableEnumeratingAllNetworkInterfaces { false };
    // FIXME: Remove m_useNetworkThreadWithSocketServer member variable and make it a global.
    bool m_useNetworkThreadWithSocketServer { true };

    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> m_factory;
#endif
};

} // namespace WebCore

namespace WTF {
template<> struct EnumTraits<WebCore::MDNSRegisterError> {
    using values = EnumValues<
        WebCore::MDNSRegisterError,
        WebCore::MDNSRegisterError::NotImplemented,
        WebCore::MDNSRegisterError::BadParameter,
        WebCore::MDNSRegisterError::DNSSD,
        WebCore::MDNSRegisterError::Internal,
        WebCore::MDNSRegisterError::Timeout
    >;
};
}
