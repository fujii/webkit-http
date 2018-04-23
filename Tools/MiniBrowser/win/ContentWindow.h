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

#include <string>
#include <windows.h>

class ContentWindow {
public:
    virtual ~ContentWindow() { };

    virtual bool loadURL(const std::wstring& url) = 0;
    virtual bool loadHTMLString(const std::wstring&) = 0;

    virtual void print() = 0;

    virtual void launchInspector() = 0;
    virtual void navigateForwardOrBackward(bool isBackward) = 0;
    virtual void navigateToHistory(unsigned historyEntry) = 0;

    virtual void setAVFoundationEnabled(bool) = 0;
    virtual void setAcceleratedCompositingEnabled(bool) = 0;
    virtual void setAuthorAndUserStylesEnabled(bool) = 0;
    virtual void setFullScreenEnabled(bool) = 0;
    virtual void setJavaScriptEnabled(bool) = 0;
    virtual void setLoadsImagesAutomatically(bool) = 0;
    virtual void setLocalFileRestrictionsEnabled(bool) = 0;
    virtual void setShouldInvertColors(bool) = 0;
    virtual void setShowCompositingBorders(bool) = 0;
    virtual void setShowTiledScrollingIndicator(bool) = 0;

    virtual bool goBack() = 0;
    virtual bool goForward() = 0;

    virtual void setUserAgent(const std::wstring& customUAString) = 0;
    virtual std::wstring userAgent() = 0;

    virtual void resetZoom() = 0;
    virtual void zoomIn() = 0;
    virtual void zoomOut() = 0;

    virtual void showLayerTree() = 0;
    virtual void updateStatistics(HWND hDlg) = 0;

    virtual HWND hwnd() = 0;
};
