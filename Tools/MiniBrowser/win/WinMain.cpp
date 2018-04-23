/*
 * Copyright (C) 2006, 2008, 2013-2015 Apple Inc.  All rights reserved.
 * Copyright (C) 2009, 2011 Brent Fulgham.  All rights reserved.
 * Copyright (C) 2009, 2010, 2011 Appcelerator, Inc. All rights reserved.
 * Copyright (C) 2013 Alex Christensen. All rights reserved.
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
#include "stdafx.h"

#include "BrowserWindow.h"
#include "MiniBrowserLibResource.h"
#include <WebKitLegacy/WebKit.h>

#if USE(CF)
#include <CoreFoundation/CFRunLoop.h>
#endif

extern BrowserWindow* gBrowserWindow;

void computeFullDesktopFrame();
void createCrashReport(EXCEPTION_POINTERS*);
void parseCommandLine(bool& usesLayeredWebView, bool& useFullDesktop, bool& pageLoadTesting, _bstr_t& requestedURL, bool& useWK2);
    
int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpstrCmdLine, _In_ int nCmdShow)
{
#ifdef _CRTDBG_MAP_ALLOC
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
#endif

    INITCOMMONCONTROLSEX InitCtrlEx;

    InitCtrlEx.dwSize = sizeof(INITCOMMONCONTROLSEX);
    InitCtrlEx.dwICC  = 0x00004000; // ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&InitCtrlEx);

    bool usesLayeredWebView = false;
    bool useFullDesktop = false;
    bool pageLoadTesting = false;
    bool useWK2 = false;
    _bstr_t requestedURL;

    parseCommandLine(usesLayeredWebView, useFullDesktop, pageLoadTesting, requestedURL, useWK2);

    if (useFullDesktop)
        computeFullDesktopFrame();

    // Init COM
    OleInitialize(nullptr);

    ::SetProcessDPIAware();

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_MINIBROWSER));

    gBrowserWindow = new BrowserWindow(nCmdShow, usesLayeredWebView, pageLoadTesting, requestedURL, useWK2);

#pragma warning(disable:4509)

    // Main message loop:
    MSG msg = { };
    __try {
        while (GetMessage(&msg, 0, 0, 0)) {
#if USE(CF)
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, true);
#endif
            if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    } __except(createCrashReport(GetExceptionInformation()), EXCEPTION_EXECUTE_HANDLER) { }

    shutDownWebKit();
#ifdef _CRTDBG_MAP_ALLOC
    _CrtDumpMemoryLeaks();
#endif

    delete gBrowserWindow;

    // Shut down COM.
    OleUninitialize();

    return static_cast<int>(msg.wParam);
}

extern "C" __declspec(dllexport) int WINAPI dllLauncherEntryPoint(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpstrCmdLine, int nCmdShow)
{
    return wWinMain(hInstance, hPrevInstance, lpstrCmdLine, nCmdShow);
}
