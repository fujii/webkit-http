/*
 * Copyright (C) 2012-2018 Apple Inc. All rights reserved.
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

#include "config.h"
#include "NetworkResourceLoader.h"

#include "DataReference.h"
#include "Logging.h"
#include "NetworkBlobRegistry.h"
#include "NetworkCache.h"
#include "NetworkConnectionToWebProcess.h"
#include "NetworkLoad.h"
#include "NetworkLoadChecker.h"
#include "NetworkProcess.h"
#include "NetworkProcessConnectionMessages.h"
#include "SessionTracker.h"
#include "WebCoreArgumentCoders.h"
#include "WebErrors.h"
#include "WebResourceLoaderMessages.h"
#include "WebsiteDataStoreParameters.h"
#include <WebCore/BlobDataFileReference.h>
#include <WebCore/CertificateInfo.h>
#include <WebCore/DiagnosticLoggingKeys.h>
#include <WebCore/HTTPHeaderNames.h>
#include <WebCore/HTTPParsers.h>
#include <WebCore/NetworkLoadMetrics.h>
#include <WebCore/ProtectionSpace.h>
#include <WebCore/SameSiteInfo.h>
#include <WebCore/SecurityOrigin.h>
#include <WebCore/SharedBuffer.h>
#include <WebCore/SynchronousLoaderClient.h>
#include <wtf/RunLoop.h>

#if HAVE(CFNETWORK_STORAGE_PARTITIONING) && !RELEASE_LOG_DISABLED
#include <WebCore/NetworkStorageSession.h>
#include <WebCore/PlatformCookieJar.h>
#endif

using namespace WebCore;

#define RELEASE_LOG_IF_ALLOWED(fmt, ...) RELEASE_LOG_IF(isAlwaysOnLoggingAllowed(), Network, "%p - NetworkResourceLoader::" fmt, this, ##__VA_ARGS__)
#define RELEASE_LOG_ERROR_IF_ALLOWED(fmt, ...) RELEASE_LOG_ERROR_IF(isAlwaysOnLoggingAllowed(), Network, "%p - NetworkResourceLoader::" fmt, this, ##__VA_ARGS__)

namespace WebKit {

struct NetworkResourceLoader::SynchronousLoadData {
    SynchronousLoadData(RefPtr<Messages::NetworkConnectionToWebProcess::PerformSynchronousLoad::DelayedReply>&& reply)
        : delayedReply(WTFMove(reply))
    {
        ASSERT(delayedReply);
    }
    ResourceRequest currentRequest;
    RefPtr<Messages::NetworkConnectionToWebProcess::PerformSynchronousLoad::DelayedReply> delayedReply;
    ResourceResponse response;
    ResourceError error;
};

static void sendReplyToSynchronousRequest(NetworkResourceLoader::SynchronousLoadData& data, const SharedBuffer* buffer)
{
    ASSERT(data.delayedReply);
    ASSERT(!data.response.isNull() || !data.error.isNull());

    Vector<char> responseBuffer;
    if (buffer && buffer->size())
        responseBuffer.append(buffer->data(), buffer->size());

    data.delayedReply->send(data.error, data.response, responseBuffer);
    data.delayedReply = nullptr;
}

static inline bool shouldUseNetworkLoadChecker(bool isSynchronous, const NetworkResourceLoadParameters& parameters)
{
    if (isSynchronous)
        return true;

    if (!parameters.shouldRestrictHTTPResponseAccess)
        return false;

    // FIXME: Add support for other destinations.
    switch (parameters.options.destination) {
    case FetchOptions::Destination::Audio:
    case FetchOptions::Destination::Video:
        return true;
    default:
        break;
    }
    return false;
}

NetworkResourceLoader::NetworkResourceLoader(NetworkResourceLoadParameters&& parameters, NetworkConnectionToWebProcess& connection, RefPtr<Messages::NetworkConnectionToWebProcess::PerformSynchronousLoad::DelayedReply>&& synchronousReply)
    : m_parameters { WTFMove(parameters) }
    , m_connection { connection }
    , m_defersLoading { parameters.defersLoading }
    , m_isAllowedToAskUserForCredentials { m_parameters.clientCredentialPolicy == ClientCredentialPolicy::MayAskClientForCredentials }
    , m_bufferingTimer { *this, &NetworkResourceLoader::bufferingTimerFired }
    , m_cache { sessionID().isEphemeral() ? nullptr : NetworkProcess::singleton().cache() }
{
    ASSERT(RunLoop::isMain());
    // FIXME: This is necessary because of the existence of EmptyFrameLoaderClient in WebCore.
    //        Once bug 116233 is resolved, this ASSERT can just be "m_webPageID && m_webFrameID"
    ASSERT((m_parameters.webPageID && m_parameters.webFrameID) || m_parameters.clientCredentialPolicy == ClientCredentialPolicy::CannotAskClientForCredentials);

    if (originalRequest().httpBody()) {
        for (const auto& element : originalRequest().httpBody()->elements()) {
            if (element.m_type == FormDataElement::Type::EncodedBlob)
                m_fileReferences.appendVector(NetworkBlobRegistry::singleton().filesInBlob(connection, element.m_url));
        }
    }

    if (shouldUseNetworkLoadChecker(!!synchronousReply, m_parameters)) {
        m_networkLoadChecker = NetworkLoadChecker::create(FetchOptions { m_parameters.options }, m_parameters.sessionID, HTTPHeaderMap { m_parameters.originalRequestHeaders }, URL { m_parameters.request.url() }, m_parameters.sourceOrigin.copyRef(), m_parameters.preflightPolicy);
        if (m_parameters.cspResponseHeaders)
            m_networkLoadChecker->setCSPResponseHeaders(ContentSecurityPolicyResponseHeaders { m_parameters.cspResponseHeaders.value() });
#if ENABLE(CONTENT_EXTENSIONS)
        m_networkLoadChecker->setContentExtensionController(URL { m_parameters.mainDocumentURL }, m_parameters.userContentControllerIdentifier);
#endif
    }
    if (synchronousReply)
        m_synchronousLoadData = std::make_unique<SynchronousLoadData>(WTFMove(synchronousReply));
}

NetworkResourceLoader::~NetworkResourceLoader()
{
    ASSERT(RunLoop::isMain());
    ASSERT(!m_networkLoad);
    ASSERT(!isSynchronous() || !m_synchronousLoadData->delayedReply);
}

bool NetworkResourceLoader::canUseCache(const ResourceRequest& request) const
{
    if (!m_cache)
        return false;
    ASSERT(!sessionID().isEphemeral());

    if (!request.url().protocolIsInHTTPFamily())
        return false;
    if (originalRequest().cachePolicy() == WebCore::DoNotUseAnyCache)
        return false;

    return true;
}

bool NetworkResourceLoader::canUseCachedRedirect(const ResourceRequest& request) const
{
    if (!canUseCache(request))
        return false;
    // Limit cached redirects to avoid cycles and other trouble.
    // Networking layer follows over 30 redirects but caching that many seems unnecessary.
    static const unsigned maximumCachedRedirectCount { 5 };
    if (m_redirectCount > maximumCachedRedirectCount)
        return false;

    return true;
}

bool NetworkResourceLoader::isSynchronous() const
{
    return !!m_synchronousLoadData;
}

void NetworkResourceLoader::start()
{
    ASSERT(RunLoop::isMain());

    if (m_defersLoading) {
        RELEASE_LOG_IF_ALLOWED("start: Loading is deferred (pageID = %" PRIu64 ", frameID = %" PRIu64 ", resourceID = %" PRIu64 ", isMainResource = %d, isSynchronous = %d)", m_parameters.webPageID, m_parameters.webFrameID, m_parameters.identifier, isMainResource(), isSynchronous());
        return;
    }

    ASSERT(!m_wasStarted);
    m_wasStarted = true;

    if (m_networkLoadChecker) {
        m_networkLoadChecker->check(ResourceRequest { originalRequest() }, [this] (auto&& result) {
            if (!result.has_value()) {
                if (!result.error().isCancellation())
                    this->didFailLoading(result.error());
                return;
            }
            if (this->canUseCache(this->originalRequest())) {
                RELEASE_LOG_IF_ALLOWED("start: Checking cache for resource (pageID = %" PRIu64 ", frameID = %" PRIu64 ", resourceID = %" PRIu64 ", isMainResource = %d, isSynchronous = %d)", m_parameters.webPageID, m_parameters.webFrameID, m_parameters.identifier, this->isMainResource(), this->isSynchronous());
                this->retrieveCacheEntry(this->originalRequest());
                return;
            }

            this->startNetworkLoad(WTFMove(result.value()), FirstLoad::Yes);
        });
        return;
    }
    // FIXME: Remove that code path once m_networkLoadChecker is used for all network loads.
    if (canUseCache(originalRequest())) {
        RELEASE_LOG_IF_ALLOWED("start: Checking cache for resource (pageID = %" PRIu64 ", frameID = %" PRIu64 ", resourceID = %" PRIu64 ", isMainResource = %d, isSynchronous = %d)", m_parameters.webPageID, m_parameters.webFrameID, m_parameters.identifier, isMainResource(), isSynchronous());
        retrieveCacheEntry(originalRequest());
        return;
    }

    startNetworkLoad(ResourceRequest { originalRequest() }, FirstLoad::Yes);
}

void NetworkResourceLoader::retrieveCacheEntry(const ResourceRequest& request)
{
    ASSERT(canUseCache(request));

    RefPtr<NetworkResourceLoader> loader(this);
    m_cache->retrieve(request, { m_parameters.webPageID, m_parameters.webFrameID }, [this, loader = WTFMove(loader), request = ResourceRequest { request }](auto entry) mutable {
#if RELEASE_LOG_DISABLED
        UNUSED_PARAM(this);
#endif
        if (loader->hasOneRef()) {
            // The loader has been aborted and is only held alive by this lambda.
            return;
        }
        if (!entry) {
            RELEASE_LOG_IF_ALLOWED("retrieveCacheEntry: Resource not in cache (pageID = %" PRIu64 ", frameID = %" PRIu64 ", resourceID = %" PRIu64 ", isMainResource = %d, isSynchronous = %d)", m_parameters.webPageID, m_parameters.webFrameID, m_parameters.identifier, isMainResource(), isSynchronous());
            loader->startNetworkLoad(WTFMove(request), FirstLoad::Yes);
            return;
        }
        if (entry->redirectRequest()) {
            RELEASE_LOG_IF_ALLOWED("retrieveCacheEntry: Handling redirect (pageID = %" PRIu64 ", frameID = %" PRIu64 ", resourceID = %" PRIu64 ", isMainResource = %d, isSynchronous = %d)", m_parameters.webPageID, m_parameters.webFrameID, m_parameters.identifier, isMainResource(), isSynchronous());
            loader->dispatchWillSendRequestForCacheEntry(WTFMove(entry));
            return;
        }
        if (loader->m_parameters.needsCertificateInfo && !entry->response().certificateInfo()) {
            RELEASE_LOG_IF_ALLOWED("retrieveCacheEntry: Resource does not have required certificate (pageID = %" PRIu64 ", frameID = %" PRIu64 ", resourceID = %" PRIu64 ", isMainResource = %d, isSynchronous = %d)", m_parameters.webPageID, m_parameters.webFrameID, m_parameters.identifier, isMainResource(), isSynchronous());
            loader->startNetworkLoad(WTFMove(request), FirstLoad::Yes);
            return;
        }
        if (entry->needsValidation() || request.cachePolicy() == WebCore::RefreshAnyCacheData) {
            RELEASE_LOG_IF_ALLOWED("retrieveCacheEntry: Validating cache entry (pageID = %" PRIu64 ", frameID = %" PRIu64 ", resourceID = %" PRIu64 ", isMainResource = %d, isSynchronous = %d)", m_parameters.webPageID, m_parameters.webFrameID, m_parameters.identifier, isMainResource(), isSynchronous());
            loader->validateCacheEntry(WTFMove(entry));
            return;
        }
        RELEASE_LOG_IF_ALLOWED("retrieveCacheEntry: Retrieved resource from cache (pageID = %" PRIu64 ", frameID = %" PRIu64 ", resourceID = %" PRIu64 ", isMainResource = %d, isSynchronous = %d)", m_parameters.webPageID, m_parameters.webFrameID, m_parameters.identifier, isMainResource(), isSynchronous());
        loader->didRetrieveCacheEntry(WTFMove(entry));
    });
}

void NetworkResourceLoader::startNetworkLoad(ResourceRequest&& request, FirstLoad load)
{
    if (load == FirstLoad::Yes) {
        RELEASE_LOG_IF_ALLOWED("startNetworkLoad: (pageID = %" PRIu64 ", frameID = %" PRIu64 ", resourceID = %" PRIu64 ", isMainResource = %d, isSynchronous = %d)", m_parameters.webPageID, m_parameters.webFrameID, m_parameters.identifier, isMainResource(), isSynchronous());

        consumeSandboxExtensions();

        if (isSynchronous() || m_parameters.maximumBufferingTime > 0_s)
            m_bufferedData = SharedBuffer::create();

        if (canUseCache(request))
            m_bufferedDataForCache = SharedBuffer::create();
    }

    NetworkLoadParameters parameters = m_parameters;
    parameters.defersLoading = m_defersLoading;
    if (m_networkLoadChecker)
        parameters.storedCredentialsPolicy = m_networkLoadChecker->storedCredentialsPolicy();

    if (request.url().protocolIsBlob())
        parameters.blobFileReferences = NetworkBlobRegistry::singleton().filesInBlob(m_connection, originalRequest().url());

    auto* networkSession = SessionTracker::networkSession(parameters.sessionID);
    if (!networkSession && parameters.sessionID.isEphemeral()) {
        NetworkProcess::singleton().addWebsiteDataStore(WebsiteDataStoreParameters::privateSessionParameters(parameters.sessionID));
        networkSession = SessionTracker::networkSession(parameters.sessionID);
    }
    if (!networkSession) {
        WTFLogAlways("Attempted to create a NetworkLoad with a session (id=%" PRIu64 ") that does not exist.", parameters.sessionID.sessionID());
        RELEASE_LOG_ERROR_IF_ALLOWED("startNetworkLoad: Attempted to create a NetworkLoad with a session that does not exist (pageID = %" PRIu64 ", frameID = %" PRIu64 ", resourceID = %" PRIu64 ", sessionID=%" PRIu64 ")", m_parameters.webPageID, m_parameters.webFrameID, m_parameters.identifier, parameters.sessionID.sessionID());
        NetworkProcess::singleton().logDiagnosticMessage(m_parameters.webPageID, WebCore::DiagnosticLoggingKeys::internalErrorKey(), WebCore::DiagnosticLoggingKeys::invalidSessionIDKey(), WebCore::ShouldSample::No);
        didFailLoading(internalError(request.url()));
        return;
    }

    parameters.request = WTFMove(request);
    m_networkLoad = std::make_unique<NetworkLoad>(*this, WTFMove(parameters), *networkSession);

    if (m_defersLoading) {
        RELEASE_LOG_IF_ALLOWED("startNetworkLoad: Created, but deferred (pageID = %" PRIu64 ", frameID = %" PRIu64 ", resourceID = %" PRIu64 ")",
            m_parameters.webPageID, m_parameters.webFrameID, m_parameters.identifier);
    }
}

void NetworkResourceLoader::setDefersLoading(bool defers)
{
    if (m_defersLoading == defers)
        return;
    m_defersLoading = defers;

    if (defers)
        RELEASE_LOG_IF_ALLOWED("setDefersLoading: Deferring resource load (pageID = %" PRIu64 ", frameID = %" PRIu64 ", resourceID = %" PRIu64 ")", m_parameters.webPageID, m_parameters.webFrameID, m_parameters.identifier);
    else
        RELEASE_LOG_IF_ALLOWED("setDefersLoading: Resuming deferred resource load (pageID = %" PRIu64 ", frameID = %" PRIu64 ", resourceID = %" PRIu64 ")", m_parameters.webPageID, m_parameters.webFrameID, m_parameters.identifier);

    if (m_networkLoad) {
        m_networkLoad->setDefersLoading(defers);
        return;
    }

    if (!m_defersLoading && !m_wasStarted)
        start();
    else
        RELEASE_LOG_IF_ALLOWED("setDefersLoading: defers = %d, but nothing to do (pageID = %" PRIu64 ", frameID = %" PRIu64 ", resourceID = %" PRIu64 ")", m_defersLoading, m_parameters.webPageID, m_parameters.webFrameID, m_parameters.identifier);
}

void NetworkResourceLoader::cleanup()
{
    ASSERT(RunLoop::isMain());

    m_bufferingTimer.stop();

    invalidateSandboxExtensions();

    m_networkLoad = nullptr;

    // This will cause NetworkResourceLoader to be destroyed and therefore we do it last.
    m_connection->didCleanupResourceLoader(*this);
}

void NetworkResourceLoader::convertToDownload(DownloadID downloadID, const ResourceRequest& request, const ResourceResponse& response)
{
    ASSERT(m_networkLoad);
    NetworkProcess::singleton().downloadManager().convertNetworkLoadToDownload(downloadID, std::exchange(m_networkLoad, nullptr), WTFMove(m_fileReferences), request, response);
}

void NetworkResourceLoader::abort()
{
    ASSERT(RunLoop::isMain());

    RELEASE_LOG_IF_ALLOWED("abort: Canceling resource load (pageID = %" PRIu64 ", frameID = %" PRIu64 ", resourceID = %" PRIu64 ")",
        m_parameters.webPageID, m_parameters.webFrameID, m_parameters.identifier);

    if (m_networkLoad) {
        if (canUseCache(m_networkLoad->currentRequest())) {
            // We might already have used data from this incomplete load. Ensure older versions don't remain in the cache after cancel.
            if (!m_response.isNull())
                m_cache->remove(m_networkLoad->currentRequest());
        }
        m_networkLoad->cancel();
    }

    cleanup();
}

static bool areFrameAncestorsSameSite(const ResourceResponse& response, const Vector<RefPtr<SecurityOrigin>>& frameAncestorOrigins)
{
#if ENABLE(PUBLIC_SUFFIX_LIST)
    auto responsePartition = ResourceRequest::partitionName(response.url().host());
    return frameAncestorOrigins.findMatching([&](const auto& item) {
        return item->isUnique() || ResourceRequest::partitionName(item->host()) != responsePartition;
    }) == notFound;
#else
    UNUSED_PARAM(response);
    UNUSED_PARAM(frameAncestorOrigins);
    return false;
#endif
}

static bool areFrameAncestorsSameOrigin(const ResourceResponse& response, const Vector<RefPtr<SecurityOrigin>>& frameAncestorOrigins)
{
    return frameAncestorOrigins.findMatching([responseOrigin = SecurityOrigin::create(response.url())](const auto& item) {
        return !item->isSameOriginAs(responseOrigin);
    }) == notFound;
}

static bool shouldCancelCrossOriginLoad(const ResourceResponse& response, const Vector<RefPtr<SecurityOrigin>>& frameAncestorOrigins)
{
    auto fromOriginDirective = WebCore::parseFromOriginHeader(response.httpHeaderField(WebCore::HTTPHeaderName::FromOrigin));
    switch (fromOriginDirective) {
    case WebCore::FromOriginDisposition::None:
    case WebCore::FromOriginDisposition::Invalid:
        return false;
    case WebCore::FromOriginDisposition::Same:
        return !areFrameAncestorsSameOrigin(response, frameAncestorOrigins);
    case WebCore::FromOriginDisposition::SameSite:
        return !areFrameAncestorsSameSite(response, frameAncestorOrigins);
    }
}

static ResourceError fromOriginResourceError(const URL& url)
{
    return { errorDomainWebKitInternal, 0, url, ASCIILiteral { "Cancelled load because it violates the resource's From-Origin response header." }, ResourceError::Type::AccessControl };
}

auto NetworkResourceLoader::didReceiveResponse(ResourceResponse&& receivedResponse) -> ShouldContinueDidReceiveResponse
{
    RELEASE_LOG_IF_ALLOWED("didReceiveResponse: (pageID = %" PRIu64 ", frameID = %" PRIu64 ", resourceID = %" PRIu64 ", httpStatusCode = %d, length = %" PRId64 ")", m_parameters.webPageID, m_parameters.webFrameID, m_parameters.identifier, receivedResponse.httpStatusCode(), receivedResponse.expectedContentLength());

    m_response = WTFMove(receivedResponse);

    if (shouldCaptureExtraNetworkLoadMetrics())
        m_connection->addNetworkLoadInformationResponse(identifier(), m_response);

    // For multipart/x-mixed-replace didReceiveResponseAsync gets called multiple times and buffering would require special handling.
    if (!isSynchronous() && m_response.isMultipart())
        m_bufferedData = nullptr;

    if (m_response.isMultipart())
        m_bufferedDataForCache = nullptr;

    if (m_cacheEntryForValidation) {
        bool validationSucceeded = m_response.httpStatusCode() == 304; // 304 Not Modified
        if (validationSucceeded) {
            m_cacheEntryForValidation = m_cache->update(originalRequest(), { m_parameters.webPageID, m_parameters.webFrameID }, *m_cacheEntryForValidation, m_response);
            // If the request was conditional then this revalidation was not triggered by the network cache and we pass the 304 response to WebCore.
            if (originalRequest().isConditional())
                m_cacheEntryForValidation = nullptr;
        } else
            m_cacheEntryForValidation = nullptr;
    }
    bool shouldSendDidReceiveResponse = !m_cacheEntryForValidation;

    bool shouldWaitContinueDidReceiveResponse = isMainResource();
    if (shouldSendDidReceiveResponse) {

        ResourceError error;
        if (m_parameters.shouldEnableFromOriginResponseHeader && shouldCancelCrossOriginLoad(m_response, m_parameters.frameAncestorOrigins))
            error = fromOriginResourceError(m_response.url());

        if (error.isNull() && m_networkLoadChecker)
            error = m_networkLoadChecker->validateResponse(m_response);

        if (!error.isNull()) {
            RunLoop::main().dispatch([protectedThis = makeRef(*this), error = WTFMove(error)] {
                if (protectedThis->m_networkLoad)
                    protectedThis->didFailLoading(error);
            });
            return ShouldContinueDidReceiveResponse::No;
        }

        auto response = sanitizeResponseIfPossible(ResourceResponse { m_response }, ResourceResponse::SanitizationType::CrossOriginSafe);
        if (isSynchronous())
            m_synchronousLoadData->response = WTFMove(response);
        else
            send(Messages::WebResourceLoader::DidReceiveResponse { response, shouldWaitContinueDidReceiveResponse });
    }

    // For main resources, the web process is responsible for sending back a NetworkResourceLoader::ContinueDidReceiveResponse message.
    bool shouldContinueDidReceiveResponse = !shouldWaitContinueDidReceiveResponse || m_cacheEntryForValidation;

    if (shouldContinueDidReceiveResponse) {
        RELEASE_LOG_IF_ALLOWED("didReceiveResponse: Should not wait for message from WebContent process before continuing resource load (pageID = %" PRIu64 ", frameID = %" PRIu64 ", resourceID = %" PRIu64 ")", m_parameters.webPageID, m_parameters.webFrameID, m_parameters.identifier);
        return ShouldContinueDidReceiveResponse::Yes;
    }

    RELEASE_LOG_IF_ALLOWED("didReceiveResponse: Should wait for message from WebContent process before continuing resource load (pageID = %" PRIu64 ", frameID = %" PRIu64 ", resourceID = %" PRIu64 ")", m_parameters.webPageID, m_parameters.webFrameID, m_parameters.identifier);
    return ShouldContinueDidReceiveResponse::No;
}

void NetworkResourceLoader::didReceiveBuffer(Ref<SharedBuffer>&& buffer, int reportedEncodedDataLength)
{
    if (!m_numBytesReceived) {
        RELEASE_LOG_IF_ALLOWED("didReceiveBuffer: Started receiving data (pageID = %" PRIu64 ", frameID = %" PRIu64 ", resourceID = %" PRIu64 ")", m_parameters.webPageID, m_parameters.webFrameID, m_parameters.identifier);
    }
    m_numBytesReceived += buffer->size();

    ASSERT(!m_cacheEntryForValidation);

    if (m_bufferedDataForCache) {
        // Prevent memory growth in case of streaming data.
        const size_t maximumCacheBufferSize = 10 * 1024 * 1024;
        if (m_bufferedDataForCache->size() + buffer->size() <= maximumCacheBufferSize)
            m_bufferedDataForCache->append(buffer.get());
        else
            m_bufferedDataForCache = nullptr;
    }
    // FIXME: At least on OS X Yosemite we always get -1 from the resource handle.
    unsigned encodedDataLength = reportedEncodedDataLength >= 0 ? reportedEncodedDataLength : buffer->size();

    m_bytesReceived += buffer->size();
    if (m_bufferedData) {
        m_bufferedData->append(buffer.get());
        m_bufferedDataEncodedDataLength += encodedDataLength;
        startBufferingTimerIfNeeded();
        return;
    }
    sendBuffer(buffer, encodedDataLength);
}

void NetworkResourceLoader::didFinishLoading(const NetworkLoadMetrics& networkLoadMetrics)
{
    RELEASE_LOG_IF_ALLOWED("didFinishLoading: (pageID = %" PRIu64 ", frameID = %" PRIu64 ", resourceID = %" PRIu64 ", length = %zd)", m_parameters.webPageID, m_parameters.webFrameID, m_parameters.identifier, m_numBytesReceived);

    if (shouldCaptureExtraNetworkLoadMetrics())
        m_connection->addNetworkLoadInformationMetrics(identifier(), networkLoadMetrics);

    if (m_cacheEntryForValidation) {
        // 304 Not Modified
        ASSERT(m_response.httpStatusCode() == 304);
        LOG(NetworkCache, "(NetworkProcess) revalidated");
        didRetrieveCacheEntry(WTFMove(m_cacheEntryForValidation));
        return;
    }

#if HAVE(CFNETWORK_STORAGE_PARTITIONING) && !RELEASE_LOG_DISABLED
    if (shouldLogCookieInformation())
        logCookieInformation();
#endif

    if (isSynchronous())
        sendReplyToSynchronousRequest(*m_synchronousLoadData, m_bufferedData.get());
    else {
        if (m_bufferedData && !m_bufferedData->isEmpty()) {
            // FIXME: Pass a real value or remove the encoded data size feature.
            sendBuffer(*m_bufferedData, -1);
        }
        send(Messages::WebResourceLoader::DidFinishResourceLoad(networkLoadMetrics));
    }

    tryStoreAsCacheEntry();

    cleanup();
}

void NetworkResourceLoader::didFailLoading(const ResourceError& error)
{
    RELEASE_LOG_IF_ALLOWED("didFailLoading: (pageID = %" PRIu64 ", frameID = %" PRIu64 ", resourceID = %" PRIu64 ", isTimeout = %d, isCancellation = %d, isAccessControl = %d, errCode = %d)", m_parameters.webPageID, m_parameters.webFrameID, m_parameters.identifier, error.isTimeout(), error.isCancellation(), error.isAccessControl(), error.errorCode());

    if (shouldCaptureExtraNetworkLoadMetrics())
        m_connection->removeNetworkLoadInformation(identifier());

    ASSERT(!error.isNull());

    m_cacheEntryForValidation = nullptr;

    if (isSynchronous()) {
        m_synchronousLoadData->error = error;
        sendReplyToSynchronousRequest(*m_synchronousLoadData, nullptr);
    } else if (auto* connection = messageSenderConnection())
        connection->send(Messages::WebResourceLoader::DidFailResourceLoad(error), messageSenderDestinationID());

    cleanup();
}

void NetworkResourceLoader::didBlockAuthenticationChallenge()
{
    send(Messages::WebResourceLoader::DidBlockAuthenticationChallenge());
}

void NetworkResourceLoader::willSendRedirectedRequest(ResourceRequest&& request, WebCore::ResourceRequest&& redirectRequest, ResourceResponse&& redirectResponse)
{
    ++m_redirectCount;

    if (m_networkLoadChecker) {
        m_networkLoadChecker->checkRedirection(redirectResponse, WTFMove(redirectRequest), [protectedThis = makeRef(*this), this, storedCredentialsPolicy = m_networkLoadChecker->storedCredentialsPolicy(), request = WTFMove(request), redirectResponse](auto&& result) mutable {
            if (!result.has_value()) {
                if (result.error().isCancellation())
                    return;
                this->didFailLoading(result.error());
                return;
            }

            if (storedCredentialsPolicy != m_networkLoadChecker->storedCredentialsPolicy()) {
                // We need to restart the load to update the session according the new credential policy.
                m_networkLoad->cancel();
                this->startNetworkLoad(WTFMove(result.value()), FirstLoad::No);
                return;
            }

            if (this->isSynchronous()) {
                // We do not support prompting for credentials for synchronous loads. If we ever change this policy then
                // we need to take care to prompt if and only if request and redirectRequest are not mixed content.
                this->continueWillSendRequest(WTFMove(result.value()), false);
                return;
            }

            this->continueWillSendRedirectedRequest(WTFMove(request), WTFMove(result.value()), WTFMove(redirectResponse));
        });
        return;
    }
    continueWillSendRedirectedRequest(WTFMove(request), WTFMove(redirectRequest), WTFMove(redirectResponse));
}

void NetworkResourceLoader::continueWillSendRedirectedRequest(WebCore::ResourceRequest&& request, WebCore::ResourceRequest&& redirectRequest, ResourceResponse&& redirectResponse)
{
    ASSERT(!isSynchronous());

    if (canUseCachedRedirect(request))
        m_cache->storeRedirect(request, redirectResponse, redirectRequest);

    if (m_parameters.shouldEnableFromOriginResponseHeader && shouldCancelCrossOriginLoad(redirectResponse, m_parameters.frameAncestorOrigins) && m_networkLoad) {
        didFailLoading(fromOriginResourceError(redirectResponse.url()));
        return;
    }

    send(Messages::WebResourceLoader::WillSendRequest(redirectRequest, sanitizeResponseIfPossible(WTFMove(redirectResponse), ResourceResponse::SanitizationType::Redirection)));
}

ResourceResponse NetworkResourceLoader::sanitizeResponseIfPossible(ResourceResponse&& response, ResourceResponse::SanitizationType type)
{
    if (m_parameters.shouldRestrictHTTPResponseAccess) {
        if (type == ResourceResponse::SanitizationType::CrossOriginSafe) {
            // We reduce filtering when it would otherwise be visible to scripts.
            // FIXME: We should use response tainting once computed in Network Process.
            bool isSameOrigin = m_parameters.sourceOrigin ? m_parameters.sourceOrigin->canRequest(response.url()) : protocolHostAndPortAreEqual(response.url(), m_parameters.request.url());
            if (isSameOrigin && m_parameters.options.destination == FetchOptions::Destination::EmptyString)
                type = ResourceResponse::SanitizationType::RemoveCookies;
        }
        response.sanitizeHTTPHeaderFields(type);
    }
    return WTFMove(response);
}

void NetworkResourceLoader::continueWillSendRequest(ResourceRequest&& newRequest, bool isAllowedToAskUserForCredentials)
{
    RELEASE_LOG_IF_ALLOWED("continueWillSendRequest: (pageID = %" PRIu64 ", frameID = %" PRIu64 ", resourceID = %" PRIu64 ")", m_parameters.webPageID, m_parameters.webFrameID, m_parameters.identifier);

    m_isAllowedToAskUserForCredentials = isAllowedToAskUserForCredentials;

    // If there is a match in the network cache, we need to reuse the original cache policy and partition.
    newRequest.setCachePolicy(originalRequest().cachePolicy());
    newRequest.setCachePartition(originalRequest().cachePartition());

    if (m_isWaitingContinueWillSendRequestForCachedRedirect) {
        m_isWaitingContinueWillSendRequestForCachedRedirect = false;

        LOG(NetworkCache, "(NetworkProcess) Retrieving cached redirect");

        if (canUseCachedRedirect(newRequest))
            retrieveCacheEntry(newRequest);
        else
            startNetworkLoad(WTFMove(newRequest), FirstLoad::Yes);

        return;
    }

    if (m_networkLoad)
        m_networkLoad->continueWillSendRequest(WTFMove(newRequest));
}

void NetworkResourceLoader::continueDidReceiveResponse()
{
    if (m_cacheEntryWaitingForContinueDidReceiveResponse) {
        continueProcessingCachedEntryAfterDidReceiveResponse(WTFMove(m_cacheEntryWaitingForContinueDidReceiveResponse));
        return;
    }

    // FIXME: Remove this check once BlobResourceHandle implements didReceiveResponseAsync correctly.
    // Currently, it does not wait for response, so the load is likely to finish before continueDidReceiveResponse.
    if (m_networkLoad)
        m_networkLoad->continueDidReceiveResponse();
}

void NetworkResourceLoader::didSendData(unsigned long long bytesSent, unsigned long long totalBytesToBeSent)
{
    if (!isSynchronous())
        send(Messages::WebResourceLoader::DidSendData(bytesSent, totalBytesToBeSent));
}

void NetworkResourceLoader::startBufferingTimerIfNeeded()
{
    if (isSynchronous())
        return;
    if (m_bufferingTimer.isActive())
        return;
    m_bufferingTimer.startOneShot(m_parameters.maximumBufferingTime);
}

void NetworkResourceLoader::bufferingTimerFired()
{
    ASSERT(m_bufferedData);
    ASSERT(m_networkLoad);

    if (m_bufferedData->isEmpty())
        return;

    IPC::SharedBufferDataReference dataReference(m_bufferedData.get());
    size_t encodedLength = m_bufferedDataEncodedDataLength;

    m_bufferedData = SharedBuffer::create();
    m_bufferedDataEncodedDataLength = 0;

    send(Messages::WebResourceLoader::DidReceiveData(dataReference, encodedLength));
}

void NetworkResourceLoader::sendBuffer(SharedBuffer& buffer, size_t encodedDataLength)
{
    ASSERT(!isSynchronous());

    IPC::SharedBufferDataReference dataReference(&buffer);
    send(Messages::WebResourceLoader::DidReceiveData(dataReference, encodedDataLength));
}

void NetworkResourceLoader::tryStoreAsCacheEntry()
{
    if (!canUseCache(m_networkLoad->currentRequest()))
        return;
    if (!m_bufferedDataForCache)
        return;

    m_cache->store(m_networkLoad->currentRequest(), m_response, WTFMove(m_bufferedDataForCache), [loader = makeRef(*this)](auto& mappedBody) mutable {
#if ENABLE(SHAREABLE_RESOURCE)
        if (mappedBody.shareableResourceHandle.isNull())
            return;
        LOG(NetworkCache, "(NetworkProcess) sending DidCacheResource");
        loader->send(Messages::NetworkProcessConnection::DidCacheResource(loader->originalRequest(), mappedBody.shareableResourceHandle, loader->sessionID()));
#endif
    });
}

void NetworkResourceLoader::didRetrieveCacheEntry(std::unique_ptr<NetworkCache::Entry> entry)
{
    auto response = entry->response();

    ResourceError error;
    if (m_parameters.shouldEnableFromOriginResponseHeader && shouldCancelCrossOriginLoad(response, m_parameters.frameAncestorOrigins))
        error = fromOriginResourceError(response.url());

    if (error.isNull() && m_networkLoadChecker)
        error = m_networkLoadChecker->validateResponse(response);

    if (!error.isNull()) {
        didFailLoading(error);
        return;
    }

    response = sanitizeResponseIfPossible(WTFMove(response), ResourceResponse::SanitizationType::CrossOriginSafe);
    if (isSynchronous()) {
        m_synchronousLoadData->response = WTFMove(response);
        sendReplyToSynchronousRequest(*m_synchronousLoadData, entry->buffer());
        cleanup();
        return;
    }

    bool needsContinueDidReceiveResponseMessage = isMainResource();
    send(Messages::WebResourceLoader::DidReceiveResponse { response, needsContinueDidReceiveResponseMessage });

    if (needsContinueDidReceiveResponseMessage)
        m_cacheEntryWaitingForContinueDidReceiveResponse = WTFMove(entry);
    else
        continueProcessingCachedEntryAfterDidReceiveResponse(WTFMove(entry));
}

void NetworkResourceLoader::continueProcessingCachedEntryAfterDidReceiveResponse(std::unique_ptr<NetworkCache::Entry> entry)
{
    if (entry->sourceStorageRecord().bodyHash && !m_parameters.derivedCachedDataTypesToRetrieve.isEmpty()) {
        auto bodyHash = *entry->sourceStorageRecord().bodyHash;
        auto* entryPtr = entry.release();
        auto retrieveCount = m_parameters.derivedCachedDataTypesToRetrieve.size();

        for (auto& type : m_parameters.derivedCachedDataTypesToRetrieve) {
            NetworkCache::DataKey key { originalRequest().cachePartition(), type, bodyHash };
            m_cache->retrieveData(key, [loader = makeRef(*this), entryPtr, type, retrieveCount] (const uint8_t* data, size_t size) mutable {
                loader->m_retrievedDerivedDataCount++;
                bool retrievedAll = loader->m_retrievedDerivedDataCount == retrieveCount;
                std::unique_ptr<NetworkCache::Entry> entry(retrievedAll ? entryPtr : nullptr);
                if (loader->hasOneRef())
                    return;
                if (data) {
                    IPC::DataReference dataReference(data, size);
                    loader->send(Messages::WebResourceLoader::DidRetrieveDerivedData(type, dataReference));
                }
                if (retrievedAll) {
                    loader->sendResultForCacheEntry(WTFMove(entry));
                    loader->cleanup();
                }
            });
        }
        return;
    }

    sendResultForCacheEntry(WTFMove(entry));

    cleanup();
}

void NetworkResourceLoader::sendResultForCacheEntry(std::unique_ptr<NetworkCache::Entry> entry)
{
#if ENABLE(SHAREABLE_RESOURCE)
    if (!entry->shareableResourceHandle().isNull()) {
        send(Messages::WebResourceLoader::DidReceiveResource(entry->shareableResourceHandle()));
        return;
    }
#endif

#if HAVE(CFNETWORK_STORAGE_PARTITIONING) && !RELEASE_LOG_DISABLED
    if (shouldLogCookieInformation())
        logCookieInformation();
#endif

    WebCore::NetworkLoadMetrics networkLoadMetrics;
    networkLoadMetrics.markComplete();
    networkLoadMetrics.requestHeaderBytesSent = 0;
    networkLoadMetrics.requestBodyBytesSent = 0;
    networkLoadMetrics.responseHeaderBytesReceived = 0;
    networkLoadMetrics.responseBodyBytesReceived = 0;
    networkLoadMetrics.responseBodyDecodedSize = 0;

    sendBuffer(*entry->buffer(), entry->buffer()->size());
    send(Messages::WebResourceLoader::DidFinishResourceLoad(networkLoadMetrics));
}

void NetworkResourceLoader::validateCacheEntry(std::unique_ptr<NetworkCache::Entry> entry)
{
    ASSERT(!m_networkLoad);

    // If the request is already conditional then the revalidation was not triggered by the disk cache
    // and we should not overwrite the existing conditional headers.
    ResourceRequest revalidationRequest = originalRequest();
    if (!revalidationRequest.isConditional()) {
        String eTag = entry->response().httpHeaderField(HTTPHeaderName::ETag);
        String lastModified = entry->response().httpHeaderField(HTTPHeaderName::LastModified);
        if (!eTag.isEmpty())
            revalidationRequest.setHTTPHeaderField(HTTPHeaderName::IfNoneMatch, eTag);
        if (!lastModified.isEmpty())
            revalidationRequest.setHTTPHeaderField(HTTPHeaderName::IfModifiedSince, lastModified);
    }

    m_cacheEntryForValidation = WTFMove(entry);

    startNetworkLoad(WTFMove(revalidationRequest), FirstLoad::Yes);
}

void NetworkResourceLoader::dispatchWillSendRequestForCacheEntry(std::unique_ptr<NetworkCache::Entry> entry)
{
    ASSERT(entry->redirectRequest());
    ASSERT(!m_isWaitingContinueWillSendRequestForCachedRedirect);

    LOG(NetworkCache, "(NetworkProcess) Executing cached redirect");

    auto& response = entry->response();
    if (m_parameters.shouldEnableFromOriginResponseHeader && shouldCancelCrossOriginLoad(response, m_parameters.frameAncestorOrigins) && m_networkLoad) {
        didFailLoading(fromOriginResourceError(response.url()));
        return;
    }

    ++m_redirectCount;
    send(Messages::WebResourceLoader::WillSendRequest { *entry->redirectRequest(), sanitizeResponseIfPossible(ResourceResponse { response }, ResourceResponse::SanitizationType::Redirection) });
    m_isWaitingContinueWillSendRequestForCachedRedirect = true;
}

IPC::Connection* NetworkResourceLoader::messageSenderConnection()
{
    return &connectionToWebProcess().connection();
}

void NetworkResourceLoader::consumeSandboxExtensions()
{
    ASSERT(!m_didConsumeSandboxExtensions);

    for (auto& extension : m_parameters.requestBodySandboxExtensions)
        extension->consume();

    if (auto& extension = m_parameters.resourceSandboxExtension)
        extension->consume();

    for (auto& fileReference : m_fileReferences)
        fileReference->prepareForFileAccess();

    m_didConsumeSandboxExtensions = true;
}

void NetworkResourceLoader::invalidateSandboxExtensions()
{
    if (m_didConsumeSandboxExtensions) {
        for (auto& extension : m_parameters.requestBodySandboxExtensions)
            extension->revoke();
        if (auto& extension = m_parameters.resourceSandboxExtension)
            extension->revoke();
        for (auto& fileReference : m_fileReferences)
            fileReference->revokeFileAccess();

        m_didConsumeSandboxExtensions = false;
    }

    m_fileReferences.clear();
}

#if USE(PROTECTION_SPACE_AUTH_CALLBACK)
void NetworkResourceLoader::canAuthenticateAgainstProtectionSpaceAsync(const ProtectionSpace& protectionSpace)
{
    NetworkProcess::singleton().canAuthenticateAgainstProtectionSpace(*this, protectionSpace);
}

void NetworkResourceLoader::continueCanAuthenticateAgainstProtectionSpace(bool result)
{
    if (m_networkLoad)
        m_networkLoad->continueCanAuthenticateAgainstProtectionSpace(result);
}
#endif

bool NetworkResourceLoader::isAlwaysOnLoggingAllowed() const
{
    if (NetworkProcess::singleton().sessionIsControlledByAutomation(sessionID()))
        return true;

    return sessionID().isAlwaysOnLoggingAllowed();
}

bool NetworkResourceLoader::shouldCaptureExtraNetworkLoadMetrics() const
{
    return m_connection->captureExtraNetworkLoadMetricsEnabled();
}

#if HAVE(CFNETWORK_STORAGE_PARTITIONING) && !RELEASE_LOG_DISABLED
bool NetworkResourceLoader::shouldLogCookieInformation()
{
    return NetworkProcess::singleton().shouldLogCookieInformation();
}

static String escapeForJSON(String s)
{
    return s.replace('\\', "\\\\").replace('"', "\\\"");
}

static String escapeIDForJSON(const std::optional<uint64_t>& value)
{
    return value ? String::number(value.value()) : String("None");
};

void NetworkResourceLoader::logCookieInformation() const
{
    ASSERT(shouldLogCookieInformation());

    auto networkStorageSession = WebCore::NetworkStorageSession::storageSession(sessionID());
    ASSERT(networkStorageSession);

    logCookieInformation("NetworkResourceLoader", reinterpret_cast<const void*>(this), *networkStorageSession, originalRequest().firstPartyForCookies(), SameSiteInfo::create(originalRequest()), originalRequest().url(), originalRequest().httpReferrer(), frameID(), pageID(), identifier());
}

static void logBlockedCookieInformation(const String& label, const void* loggedObject, const WebCore::NetworkStorageSession& networkStorageSession, const WebCore::URL& firstParty, const SameSiteInfo& sameSiteInfo, const WebCore::URL& url, const String& referrer, std::optional<uint64_t> frameID, std::optional<uint64_t> pageID, std::optional<uint64_t> identifier)
{
    ASSERT(NetworkResourceLoader::shouldLogCookieInformation());

    auto escapedURL = escapeForJSON(url.string());
    auto escapedFirstParty = escapeForJSON(firstParty.string());
    auto escapedFrameID = escapeIDForJSON(frameID);
    auto escapedPageID = escapeIDForJSON(pageID);
    auto escapedIdentifier = escapeIDForJSON(identifier);
    auto escapedReferrer = escapeForJSON(referrer);

#define LOCAL_LOG_IF_ALLOWED(fmt, ...) RELEASE_LOG_IF(networkStorageSession.sessionID().isAlwaysOnLoggingAllowed(), Network, "%p - %s::" fmt, loggedObject, label.utf8().data(), ##__VA_ARGS__)
#define LOCAL_LOG(str, ...) \
    LOCAL_LOG_IF_ALLOWED("logCookieInformation: BLOCKED cookie access for pageID = %s, frameID = %s, resourceID = %s, firstParty = %s: " str, escapedPageID.utf8().data(), escapedFrameID.utf8().data(), escapedIdentifier.utf8().data(), escapedFirstParty.utf8().data(), ##__VA_ARGS__)

    LOCAL_LOG(R"({ "url": "%{public}s",)", escapedURL.utf8().data());
    LOCAL_LOG(R"(  "partition": "%{public}s",)", "BLOCKED");
    LOCAL_LOG(R"(  "hasStorageAccess": %{public}s,)", "false");
    LOCAL_LOG(R"(  "referer": "%{public}s",)", escapedReferrer.utf8().data());
    LOCAL_LOG(R"(  "isSameSite": "%{public}s",)", sameSiteInfo.isSameSite ? "true" : "false");
    LOCAL_LOG(R"(  "isTopSite": "%{public}s",)", sameSiteInfo.isTopSite ? "true" : "false");
    LOCAL_LOG(R"(  "cookies": [])");
    LOCAL_LOG(R"(  "})");
#undef LOCAL_LOG
#undef LOCAL_LOG_IF_ALLOWED
}

static void logCookieInformationInternal(const String& label, const void* loggedObject, const WebCore::NetworkStorageSession& networkStorageSession, const WebCore::URL& partition, const WebCore::SameSiteInfo& sameSiteInfo, const WebCore::URL& url, const String& referrer, std::optional<uint64_t> frameID, std::optional<uint64_t> pageID, std::optional<uint64_t> identifier)
{
    ASSERT(NetworkResourceLoader::shouldLogCookieInformation());

    Vector<WebCore::Cookie> cookies;
    if (!WebCore::getRawCookies(networkStorageSession, partition, sameSiteInfo, url, frameID, pageID, cookies))
        return;

    auto escapedURL = escapeForJSON(url.string());
    auto escapedPartition = escapeForJSON(partition.string());
    auto escapedReferrer = escapeForJSON(referrer);
    auto escapedFrameID = escapeIDForJSON(frameID);
    auto escapedPageID = escapeIDForJSON(pageID);
    auto escapedIdentifier = escapeIDForJSON(identifier);
    bool hasStorageAccess = (frameID && pageID) ? networkStorageSession.hasStorageAccess(url.string(), partition.string(), frameID.value(), pageID.value()) : false;

#define LOCAL_LOG_IF_ALLOWED(fmt, ...) RELEASE_LOG_IF(networkStorageSession.sessionID().isAlwaysOnLoggingAllowed(), Network, "%p - %s::" fmt, loggedObject, label.utf8().data(), ##__VA_ARGS__)
#define LOCAL_LOG(str, ...) \
    LOCAL_LOG_IF_ALLOWED("logCookieInformation: pageID = %s, frameID = %s, resourceID = %s: " str, escapedPageID.utf8().data(), escapedFrameID.utf8().data(), escapedIdentifier.utf8().data(), ##__VA_ARGS__)

    LOCAL_LOG(R"({ "url": "%{public}s",)", escapedURL.utf8().data());
    LOCAL_LOG(R"(  "partition": "%{public}s",)", escapedPartition.utf8().data());
    LOCAL_LOG(R"(  "hasStorageAccess": %{public}s,)", hasStorageAccess ? "true" : "false");
    LOCAL_LOG(R"(  "referer": "%{public}s",)", escapedReferrer.utf8().data());
    LOCAL_LOG(R"(  "isSameSite": "%{public}s",)", sameSiteInfo.isSameSite ? "true" : "false");
    LOCAL_LOG(R"(  "isTopSite": "%{public}s",)", sameSiteInfo.isTopSite ? "true" : "false");
    LOCAL_LOG(R"(  "cookies": [)");

    auto size = cookies.size();
    decltype(size) count = 0;
    for (const auto& cookie : cookies) {
        const char* trailingComma = ",";
        if (++count == size)
            trailingComma = "";

        auto escapedName = escapeForJSON(cookie.name);
        auto escapedValue = escapeForJSON(cookie.value);
        auto escapedDomain = escapeForJSON(cookie.domain);
        auto escapedPath = escapeForJSON(cookie.path);
        auto escapedComment = escapeForJSON(cookie.comment);
        auto escapedCommentURL = escapeForJSON(cookie.commentURL.string());
        // FIXME: Log Same-Site policy for each cookie. See <https://bugs.webkit.org/show_bug.cgi?id=184894>.

        LOCAL_LOG(R"(  { "name": "%{public}s",)", escapedName.utf8().data());
        LOCAL_LOG(R"(    "value": "%{public}s",)", escapedValue.utf8().data());
        LOCAL_LOG(R"(    "domain": "%{public}s",)", escapedDomain.utf8().data());
        LOCAL_LOG(R"(    "path": "%{public}s",)", escapedPath.utf8().data());
        LOCAL_LOG(R"(    "created": %f,)", cookie.created);
        LOCAL_LOG(R"(    "expires": %f,)", cookie.expires);
        LOCAL_LOG(R"(    "httpOnly": %{public}s,)", cookie.httpOnly ? "true" : "false");
        LOCAL_LOG(R"(    "secure": %{public}s,)", cookie.secure ? "true" : "false");
        LOCAL_LOG(R"(    "session": %{public}s,)", cookie.session ? "true" : "false");
        LOCAL_LOG(R"(    "comment": "%{public}s",)", escapedComment.utf8().data());
        LOCAL_LOG(R"(    "commentURL": "%{public}s")", escapedCommentURL.utf8().data());
        LOCAL_LOG(R"(  }%{public}s)", trailingComma);
    }
    LOCAL_LOG(R"(]})");
#undef LOCAL_LOG
#undef LOCAL_LOG_IF_ALLOWED
}

void NetworkResourceLoader::logCookieInformation(const String& label, const void* loggedObject, const NetworkStorageSession& networkStorageSession, const URL& firstParty, const SameSiteInfo& sameSiteInfo, const URL& url, const String& referrer, std::optional<uint64_t> frameID, std::optional<uint64_t> pageID, std::optional<uint64_t> identifier)
{
    ASSERT(shouldLogCookieInformation());

    if (networkStorageSession.shouldBlockCookies(firstParty, url))
        logBlockedCookieInformation(label, loggedObject, networkStorageSession, firstParty, sameSiteInfo, url, referrer, frameID, pageID, identifier);
    else {
        auto partition = URL(ParsedURLString, networkStorageSession.cookieStoragePartition(firstParty, url, frameID, pageID));
        logCookieInformationInternal(label, loggedObject, networkStorageSession, partition, sameSiteInfo, url, referrer, frameID, pageID, identifier);
    }
}
#endif

} // namespace WebKit
