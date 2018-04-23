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
#pragma once

#include "ContentWindow.h"
#include <WebKit/WKRetainPtr.h>
#include <WebKit/WebKit2_C.h>

class WK2ContentWindow : public ContentWindow {
public:
    WK2ContentWindow(HWND mainWnd, HWND urlBarWnd);

private:
    bool loadURL(const std::wstring& url) override;
    bool loadHTMLString(const std::wstring&) override;

    void print() override;

    void launchInspector() override;
    void navigateForwardOrBackward(bool isBackward) override;
    void navigateToHistory(unsigned historyEntry) override;

    void setAVFoundationEnabled(bool) override;
    void setAcceleratedCompositingEnabled(bool) override;
    void setAuthorAndUserStylesEnabled(bool) override;
    void setFullScreenEnabled(bool) override;
    void setJavaScriptEnabled(bool) override;
    void setLoadsImagesAutomatically(bool) override;
    void setLocalFileRestrictionsEnabled(bool) override;
    void setShouldInvertColors(bool) override;
    void setShowCompositingBorders(bool) override;
    void setShowTiledScrollingIndicator(bool) override;

    bool goBack() override;
    bool goForward() override;

    void setUserAgent(const std::wstring& customUAString) override;
    std::wstring userAgent() override;

    void resetZoom() override;
    void zoomIn() override;
    void zoomOut() override;

    void showLayerTree() override;
    void updateStatistics(HWND hDlg) override;

    HWND hwnd() override;

    static void didReceiveTitleForFrame(WKPageRef, WKStringRef title, WKFrameRef, WKTypeRef userData, const void *clientInfo);
    static void didFailProvisionalLoadWithErrorForFrame(WKPageRef, WKFrameRef, WKErrorRef, WKTypeRef userData, const void *clientInfo);
    static void didCommitLoadForFrame(WKPageRef, WKFrameRef, WKTypeRef userData, const void *clientInfo);
    static void didChangeProgress(WKPageRef, const void* clientInfo);
    static void didSameDocumentNavigationForFrame(WKPageRef, WKFrameRef, WKSameDocumentNavigationType, WKTypeRef userData, const void *clientInfo);

    WKRetainPtr<WKViewRef> m_view;
    HWND m_urlBarWnd;
};
