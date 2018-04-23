/*
 * Copyright (C) 2018 Sony Interactive Entertainment Inc.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */
#include "WK2ContentWindow.h"

#include <WebKit/WKInspector.h>

std::wstring
createString(WKStringRef wkString)
{
    size_t maxSize = WKStringGetMaximumUTF8CStringSize(wkString);
    char* buffer = new char[maxSize];
    size_t actualSize = WKStringGetUTF8CString(wkString, buffer, maxSize);

    size_t wlen;
    wchar_t* wcs = new wchar_t[actualSize + 1];
    mbstowcs_s(&wlen, wcs, actualSize+1, buffer, actualSize);
    delete [] buffer;
    std::wstring ret(wcs);
    delete [] wcs;

    return ret;
}

std::wstring createString(WKURLRef wkURL)
{
    WKRetainPtr<WKStringRef> url = adoptWK(WKURLCopyString(wkURL));
    return createString(url.get());
}

std::string toUtf8(const std::wstring& src)
{
    size_t len = src.length();
    char* mbs = new char[len * MB_CUR_MAX + 1];
    wcstombs(mbs, src.c_str(), len * MB_CUR_MAX + 1);
    std::string dest(mbs);
    delete[] mbs;
    return dest;
}

std::wstring fromUtf8(const std::string& src)
{
    size_t len = src.length();
    wchar_t* wcs = new wchar_t[len + 1];
    size_t wlen;
    mbstowcs_s(&wlen, wcs, len+1, src.c_str(), len);
    std::wstring dest(wcs);
    delete[] wcs;
    return dest;
}

WKRetainPtr<WKStringRef>
createWKString(std::wstring s)
{
    auto utf8 = toUtf8(s);
    return adoptWK(WKStringCreateWithUTF8CString(utf8.c_str()));
}

WKRetainPtr<WKURLRef>
createWKURL(std::wstring s)
{
    auto utf8 = toUtf8(s);
    return adoptWK(WKURLCreateWithUTF8CString(utf8.c_str()));
}


WK2ContentWindow::WK2ContentWindow(HWND mainWnd, HWND urlBarWnd)
    : m_urlBarWnd(urlBarWnd)
{
    RECT rect = { };
    auto conf = adoptWK(WKPageConfigurationCreate());
    auto context = adoptWK(WKContextCreate());
    WKPageConfigurationSetContext(conf.get(), context.get());

    m_view = adoptWK(WKViewCreate(rect, conf.get(), mainWnd));
    auto page = WKViewGetPage(m_view.get());

    WKPageLoaderClientV0 loadClient = {{ 0, this }};
    loadClient.didReceiveTitleForFrame = didReceiveTitleForFrame;
    loadClient.didFailProvisionalLoadWithErrorForFrame = didFailProvisionalLoadWithErrorForFrame;
    loadClient.didCommitLoadForFrame = didCommitLoadForFrame;
    loadClient.didChangeProgress = didChangeProgress;
    loadClient.didSameDocumentNavigationForFrame = didSameDocumentNavigationForFrame;
    WKPageSetPageLoaderClient(page, &loadClient.base);
}

bool WK2ContentWindow::loadURL(const std::wstring& url)
{
    auto page = WKViewGetPage(m_view.get());
    WKPageLoadURL(page, createWKURL(url).get());
    return true;
}

bool WK2ContentWindow::loadHTMLString(const std::wstring& str)
{
    auto page = WKViewGetPage(m_view.get());
    auto url = createWKURL(L"about:");
    WKPageLoadHTMLString(page, createWKString(str).get(), url.get());
    return true;
}

void WK2ContentWindow::print()
{
}


void WK2ContentWindow::launchInspector()
{
    auto page = WKViewGetPage(m_view.get());
    auto inspector = WKPageGetInspector(page);
    WKInspectorShow(inspector);
}

void WK2ContentWindow::navigateForwardOrBackward(bool isBackward)
{
}

void WK2ContentWindow::navigateToHistory(unsigned historyEntry)
{
}


void WK2ContentWindow::setAVFoundationEnabled(bool)
{
}

void WK2ContentWindow::setAcceleratedCompositingEnabled(bool)
{
}

void WK2ContentWindow::setAuthorAndUserStylesEnabled(bool)
{
}

void WK2ContentWindow::setFullScreenEnabled(bool)
{
}

void WK2ContentWindow::setJavaScriptEnabled(bool)
{
}

void WK2ContentWindow::setLoadsImagesAutomatically(bool enabled)
{
    auto page = WKViewGetPage(m_view.get());
    auto pgroup = WKPageGetPageGroup(page);
    auto pref = WKPageGroupGetPreferences(pgroup);
    WKPreferencesSetLoadsImagesAutomatically(pref, enabled);
}

void WK2ContentWindow::setLocalFileRestrictionsEnabled(bool)
{
}

void WK2ContentWindow::setShouldInvertColors(bool)
{
}

void WK2ContentWindow::setShowCompositingBorders(bool)
{
}

void WK2ContentWindow::setShowTiledScrollingIndicator(bool)
{
}


bool WK2ContentWindow::goBack()
{
    auto page = WKViewGetPage(m_view.get());
    WKPageGoBack(page);
    return true;
}

bool WK2ContentWindow::goForward()
{
    auto page = WKViewGetPage(m_view.get());
    WKPageGoForward(page);
    return true;
}


void WK2ContentWindow::setUserAgent(const std::wstring& customUAString)
{
    auto page = WKViewGetPage(m_view.get());
    auto ua = createWKString(customUAString);
    WKPageSetCustomUserAgent(page, ua.get());
}

std::wstring WK2ContentWindow::userAgent()
{
    auto page = WKViewGetPage(m_view.get());
    auto ua = adoptWK(WKPageCopyUserAgent(page));
    return createString(ua.get());
}


void WK2ContentWindow::resetZoom()
{
    auto page = WKViewGetPage(m_view.get());
    WKPageSetPageZoomFactor(page, 1);
}

void WK2ContentWindow::zoomIn()
{
    auto page = WKViewGetPage(m_view.get());
    double s = WKPageGetPageZoomFactor(page);
    WKPageSetPageZoomFactor(page, s * 1.25);
}

void WK2ContentWindow::zoomOut()
{
    auto page = WKViewGetPage(m_view.get());
    double s = WKPageGetPageZoomFactor(page);
    WKPageSetPageZoomFactor(page, s * 0.8);
}


void WK2ContentWindow::showLayerTree()
{
}

void WK2ContentWindow::updateStatistics(HWND hDlg)
{
}


HWND WK2ContentWindow::hwnd()
{
    return WKViewGetWindow(m_view.get());
}

static WK2ContentWindow* toContentWindow(const void *clientInfo)
{
    return const_cast<WK2ContentWindow*>(static_cast<const WK2ContentWindow*>(clientInfo));
}

void WK2ContentWindow::didReceiveTitleForFrame(WKPageRef page, WKStringRef title, WKFrameRef frame, WKTypeRef userData, const void *clientInfo)
{
    if (!WKFrameIsMainFrame(frame))
        return;
    std::wstring titleString = createString(title);
    auto thiz = toContentWindow(clientInfo);
    printf(__FUNCTION__ ": %S", titleString.c_str());
}

void WK2ContentWindow::didFailProvisionalLoadWithErrorForFrame(WKPageRef page, WKFrameRef frame, WKErrorRef error, WKTypeRef userData, const void *clientInfo)
{
    int errorCode = WKErrorGetErrorCode(error);

    printf(__FUNCTION__ ": %d\n", errorCode);
}

void WK2ContentWindow::didCommitLoadForFrame(WKPageRef page, WKFrameRef frame, WKTypeRef userData, const void *clientInfo)
{
    if (!WKFrameIsMainFrame(frame))
        return;
    auto thiz = toContentWindow(clientInfo);

    WKRetainPtr<WKURLRef> wkurl = adoptWK(WKFrameCopyURL(frame));
    std::wstring urlString = createString(wkurl.get());
    SetWindowText(thiz->m_urlBarWnd, urlString.c_str());
}

void WK2ContentWindow::didChangeProgress(WKPageRef page, const void* clientInfo)
{
    auto thiz = toContentWindow(clientInfo);
    double progress = WKPageGetEstimatedProgress(page);
    printf(__FUNCTION__ ": %f\n", progress);
}

void WK2ContentWindow::didSameDocumentNavigationForFrame(WKPageRef page, WKFrameRef frame, WKSameDocumentNavigationType type, WKTypeRef userData, const void *clientInfo)
{
    if (!WKFrameIsMainFrame(frame))
        return;
}
