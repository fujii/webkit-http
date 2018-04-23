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
#include "DOMDefaultImpl.h"
#include "MiniBrowser.h"
#include "MiniBrowserLibResource.h"
#include <WebKitLegacy/WebKitCOMAPI.h>
#include <cassert>
#include <comip.h>
#include <commctrl.h>
#include <commdlg.h>
#include <comutil.h>
#include <dbghelp.h>
#include <memory>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <wininet.h>
#include <wtf/ExportMacros.h>
#include <wtf/Platform.h>
#include <wtf/text/CString.h>
#include <wtf/text/WTFString.h>

#if USE(CF)
#include <CoreFoundation/CFRunLoop.h>
#endif


static const int maxHistorySize = 10;

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

typedef _com_ptr_t<_com_IIID<IWebFrame, &__uuidof(IWebFrame)>> IWebFramePtr;
typedef _com_ptr_t<_com_IIID<IWebMutableURLRequest, &__uuidof(IWebMutableURLRequest)>> IWebMutableURLRequestPtr;

// Global Variables:
HINSTANCE hInst;
HWND hCacheWnd;
WNDPROC DefEditProc = nullptr;
WNDPROC DefButtonProc = nullptr;
BrowserWindow* gBrowserWindow = nullptr;
ContentWindow* gMiniBrowser = nullptr;

// Support moving the transparent window
POINT s_windowPosition = { 100, 100 };
SIZE s_windowSize = { 500, 200 };

// Forward declarations of functions included in this code module:
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK CustomUserAgent(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK BackButtonProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ForwardButtonProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ReloadButtonProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK Caches(HWND, UINT, WPARAM, LPARAM);

void loadURL(BSTR urlBStr);

namespace WebCore {
float deviceScaleFactorForWindow(HWND);
}

void computeFullDesktopFrame()
{
    RECT desktop;
    if (!::SystemParametersInfo(SPI_GETWORKAREA, 0, static_cast<void*>(&desktop), 0))
        return;

    float scaleFactor = WebCore::deviceScaleFactorForWindow(nullptr);

    s_windowPosition.x = 0;
    s_windowPosition.y = 0;
    s_windowSize.cx = scaleFactor * (desktop.right - desktop.left);
    s_windowSize.cy = scaleFactor * (desktop.bottom - desktop.top);
}

BOOL WINAPI DllMain(HINSTANCE dllInstance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        hInst = dllInstance;

    return TRUE;
}

bool getAppDataFolder(_bstr_t& directory)
{
    wchar_t appDataDirectory[MAX_PATH];
    if (FAILED(SHGetFolderPathW(0, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, 0, 0, appDataDirectory)))
        return false;

    wchar_t executablePath[MAX_PATH];
    if (!::GetModuleFileNameW(0, executablePath, MAX_PATH))
        return false;

    ::PathRemoveExtensionW(executablePath);

    directory = _bstr_t(appDataDirectory) + L"\\" + ::PathFindFileNameW(executablePath);

    return true;
}

static void processCrashReport(const wchar_t* fileName) { ::MessageBox(0, fileName, L"Crash Report", MB_OK); }

void createCrashReport(EXCEPTION_POINTERS* exceptionPointers)
{
    _bstr_t directory;

    if (!getAppDataFolder(directory))
        return;

    if (::SHCreateDirectoryEx(0, directory, 0) != ERROR_SUCCESS
        && ::GetLastError() != ERROR_FILE_EXISTS
        && ::GetLastError() != ERROR_ALREADY_EXISTS)
        return;

    std::wstring fileName = std::wstring(static_cast<const wchar_t*>(directory)) + L"\\CrashReport.dmp";
    HANDLE miniDumpFile = ::CreateFile(fileName.c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);

    if (miniDumpFile && miniDumpFile != INVALID_HANDLE_VALUE) {

        MINIDUMP_EXCEPTION_INFORMATION mdei;
        mdei.ThreadId = ::GetCurrentThreadId();
        mdei.ExceptionPointers  = exceptionPointers;
        mdei.ClientPointers = 0;

#ifdef _DEBUG
        MINIDUMP_TYPE dumpType = MiniDumpWithFullMemory;
#else
        MINIDUMP_TYPE dumpType = MiniDumpNormal;
#endif

        ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), miniDumpFile, dumpType, &mdei, 0, 0);
        ::CloseHandle(miniDumpFile);
        processCrashReport(fileName.c_str());
    }
}

static void ToggleMenuFlag(HWND hWnd, UINT menuID)
{
    HMENU menu = ::GetMenu(hWnd);

    MENUITEMINFO info;
    ::memset(&info, 0x00, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = MIIM_STATE;

    if (!::GetMenuItemInfo(menu, menuID, FALSE, &info))
        return;

    BOOL newState = !(info.fState & MFS_CHECKED);
    info.fState = (newState) ? MFS_CHECKED : MFS_UNCHECKED;

    ::SetMenuItemInfo(menu, menuID, FALSE, &info);
}

static bool menuItemIsChecked(const MENUITEMINFO& info)
{
    return info.fState & MFS_CHECKED;
}

static void turnOffOtherUserAgents(HMENU menu)
{
    MENUITEMINFO info;
    ::memset(&info, 0x00, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = MIIM_STATE;

    // Must unset the other menu items:
    for (UINT menuToClear = IDM_UA_DEFAULT; menuToClear <= IDM_UA_OTHER; ++menuToClear) {
        if (!::GetMenuItemInfo(menu, menuToClear, FALSE, &info))
            continue;
        if (!menuItemIsChecked(info))
            continue;

        info.fState = MFS_UNCHECKED;
        ::SetMenuItemInfo(menu, menuToClear, FALSE, &info);
    }
}

static void setUserAgent(UINT menuID)
{
    std::wstring customUserAgent;
    switch (menuID) {
    case IDM_UA_DEFAULT:
        // Set to null user agent
        break;
    case IDM_UA_SAFARI_8_0:
        customUserAgent = L"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_10) AppleWebKit/600.1.25 (KHTML, like Gecko) Version/8.0 Safari/600.1.25";
        break;
    case IDM_UA_SAFARI_IOS_8_IPHONE:
        customUserAgent = L"Mozilla/5.0 (iPhone; CPU OS 8_1 like Mac OS X) AppleWebKit/601.1.4 (KHTML, like Gecko) Version/8.0 Mobile/12B403 Safari/600.1.4";
        break;
    case IDM_UA_SAFARI_IOS_8_IPAD:
        customUserAgent = L"Mozilla/5.0 (iPad; CPU OS 8_1 like Mac OS X) AppleWebKit/600.1.4 (KHTML, like Gecko) Version/8.0 Mobile/12B403 Safari/600.1.4";
        break;
    case IDM_UA_IE_11:
        customUserAgent = L"Mozilla/5.0 (Windows NT 6.3; WOW64; Trident/7.0; rv:11.0) like Gecko";
        break;
    case IDM_UA_CHROME_MAC:
        customUserAgent = L"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_8_3) AppleWebKit/537.31 (KHTML, like Gecko) Chrome/26.0.1410.65 Safari/537.31";
        break;
    case IDM_UA_CHROME_WIN:
        customUserAgent = L"Mozilla/5.0 (Windows NT 6.2; WOW64) AppleWebKit/537.31 (KHTML, like Gecko) Chrome/26.0.1410.64 Safari/537.31";
        break;
    case IDM_UA_FIREFOX_MAC:
        customUserAgent = L"Mozilla/5.0 (Macintosh; Intel Mac OS X 10.8; rv:20.0) Gecko/20100101 Firefox/20.0";
        break;
    case IDM_UA_FIREFOX_WIN:
        customUserAgent = L"Mozilla/5.0 (Windows NT 6.2; WOW64; rv:20.0) Gecko/20100101 Firefox/20.0";
        break;
    case IDM_UA_OTHER:
    default:
        ASSERT(0); // We should never hit this case
        return;
    }
    gMiniBrowser->setUserAgent(customUserAgent);
}
static bool ToggleMenuItem(HWND hWnd, UINT menuID)
{
    if (!gMiniBrowser)
        return false;

    HMENU menu = ::GetMenu(hWnd);

    MENUITEMINFO info;
    ::memset(&info, 0x00, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = MIIM_STATE;

    if (!::GetMenuItemInfo(menu, menuID, FALSE, &info))
        return false;

    BOOL newState = !menuItemIsChecked(info);

    switch (menuID) {
    case IDM_AVFOUNDATION:
        gMiniBrowser->setAVFoundationEnabled(newState);
        break;
    case IDM_ACC_COMPOSITING:
        gMiniBrowser->setAcceleratedCompositingEnabled(newState);
        break;
    case IDM_WK_FULLSCREEN:
        gMiniBrowser->setFullScreenEnabled(newState);
        break;
    case IDM_COMPOSITING_BORDERS:
        gMiniBrowser->setShowCompositingBorders(newState);
        break;
    case IDM_DEBUG_INFO_LAYER:
        gMiniBrowser->setShowTiledScrollingIndicator(newState);
        break;
    case IDM_INVERT_COLORS:
        gMiniBrowser->setShouldInvertColors(newState);
        break;
    case IDM_DISABLE_IMAGES:
        gMiniBrowser->setLoadsImagesAutomatically(!newState);
        break;
    case IDM_DISABLE_STYLES:
        gMiniBrowser->setAuthorAndUserStylesEnabled(!newState);
        break;
    case IDM_DISABLE_JAVASCRIPT:
        gMiniBrowser->setJavaScriptEnabled(!newState);
        break;
    case IDM_DISABLE_LOCAL_FILE_RESTRICTIONS:
        gMiniBrowser->setLocalFileRestrictionsEnabled(!newState);
        break;
    case IDM_UA_DEFAULT:
    case IDM_UA_SAFARI_8_0:
    case IDM_UA_SAFARI_IOS_8_IPHONE:
    case IDM_UA_SAFARI_IOS_8_IPAD:
    case IDM_UA_IE_11:
    case IDM_UA_CHROME_MAC:
    case IDM_UA_CHROME_WIN:
    case IDM_UA_FIREFOX_MAC:
    case IDM_UA_FIREFOX_WIN:
        setUserAgent(menuID);
        turnOffOtherUserAgents(menu);
        break;
    case IDM_UA_OTHER:
        // The actual user agent string will be set by the custom user agent dialog
        turnOffOtherUserAgents(menu);
        break;
    default:
        return false;
    }

    info.fState = (newState) ? MFS_CHECKED : MFS_UNCHECKED;

    ::SetMenuItemInfo(menu, menuID, FALSE, &info);

    return true;
}

LRESULT CALLBACK BrowserWindow::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    WNDPROC parentProc = DefWindowProc;

    switch (message) {
    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        int wmEvent = HIWORD(wParam);
        if (wmId >= IDM_HISTORY_LINK0 && wmId <= IDM_HISTORY_LINK9) {
            if (gMiniBrowser)
                gMiniBrowser->navigateToHistory(wmId - IDM_HISTORY_LINK0);
            break;
        }
        // Parse the menu selections:
        switch (wmId) {
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        case IDM_PRINT:
            if (gMiniBrowser)
                gMiniBrowser->print();
            break;
        case IDM_WEB_INSPECTOR:
            if (gMiniBrowser)
                gMiniBrowser->launchInspector();
            break;
        case IDM_CACHES:
            if (!::IsWindow(hCacheWnd)) {
                hCacheWnd = CreateDialog(hInst, MAKEINTRESOURCE(IDD_CACHES), hWnd, Caches);
                ::ShowWindow(hCacheWnd, SW_SHOW);
            }
            break;
        case IDM_HISTORY_BACKWARD:
        case IDM_HISTORY_FORWARD:
            if (gMiniBrowser)
                gMiniBrowser->navigateForwardOrBackward(wmId == IDM_HISTORY_BACKWARD);
            break;
        case IDM_UA_OTHER:
            if (wmEvent)
                ToggleMenuItem(hWnd, wmId);
            else
                DialogBox(hInst, MAKEINTRESOURCE(IDD_USER_AGENT), hWnd, CustomUserAgent);
            break;
        case IDM_ACTUAL_SIZE:
            if (gMiniBrowser)
                gMiniBrowser->resetZoom();
            break;
        case IDM_ZOOM_IN:
            if (gMiniBrowser)
                gMiniBrowser->zoomIn();
            break;
        case IDM_ZOOM_OUT:
            if (gMiniBrowser)
                gMiniBrowser->zoomOut();
            break;
        case IDM_SHOW_LAYER_TREE:
            if (gMiniBrowser)
                gMiniBrowser->showLayerTree();
            break;
        default:
            if (!ToggleMenuItem(hWnd, wmId))
                return CallWindowProc(parentProc, hWnd, message, wParam, lParam);
        }
        }
        break;
    case WM_DESTROY:
#if USE(CF)
        CFRunLoopStop(CFRunLoopGetMain());
#endif
        PostQuitMessage(0);
        break;
    case WM_SIZE:
        if (!gBrowserWindow)
            return CallWindowProc(parentProc, hWnd, message, wParam, lParam);

        gBrowserWindow->resizeSubViews();
        break;
    case WM_DPICHANGED:
        if (gBrowserWindow)
            gBrowserWindow->updateDeviceScaleFactor();
        return CallWindowProc(parentProc, hWnd, message, wParam, lParam);
    default:
        return CallWindowProc(parentProc, hWnd, message, wParam, lParam);
    }

    return 0;
}

LRESULT CALLBACK EditProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CHAR:
        if (wParam == 13) { // Enter Key
            wchar_t strPtr[INTERNET_MAX_URL_LENGTH];
            *((LPWORD)strPtr) = INTERNET_MAX_URL_LENGTH; 
            int strLen = SendMessage(hDlg, EM_GETLINE, 0, (LPARAM)strPtr);

            strPtr[strLen] = 0;
            _bstr_t bstr(strPtr);
            loadURL(bstr.GetBSTR());

            return 0;
        } 
    default:
        return CallWindowProc(DefEditProc, hDlg, message, wParam, lParam);
    }
}

LRESULT CALLBACK BackButtonProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_LBUTTONUP:
        gMiniBrowser->goBack();
    default:
        return CallWindowProc(DefButtonProc, hDlg, message, wParam, lParam);
    }
}

LRESULT CALLBACK ForwardButtonProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_LBUTTONUP:
        gMiniBrowser->goForward();
    default:
        return CallWindowProc(DefButtonProc, hDlg, message, wParam, lParam);
    }
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message) {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK Caches(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message) {
    case WM_INITDIALOG:
        ::SetTimer(hDlg, IDT_UPDATE_STATS, 1000, nullptr);
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            ::KillTimer(hDlg, IDT_UPDATE_STATS);
            ::DestroyWindow(hDlg);
            hCacheWnd = 0;
            return (INT_PTR)TRUE;
        }
        break;

    case IDT_UPDATE_STATS:
        ::InvalidateRect(hDlg, nullptr, FALSE);
        return (INT_PTR)TRUE;

    case WM_PAINT:
        if (gMiniBrowser)
            gMiniBrowser->updateStatistics(hDlg);
        break;
    }

    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK CustomUserAgent(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message) {
    case WM_INITDIALOG: {
        HWND edit = ::GetDlgItem(hDlg, IDC_USER_AGENT_INPUT);
        std::wstring userAgent;
        if (gMiniBrowser)
            userAgent = gMiniBrowser->userAgent();

        ::SetWindowText(edit, userAgent.c_str());
        return (INT_PTR)TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            HWND edit = ::GetDlgItem(hDlg, IDC_USER_AGENT_INPUT);

            TCHAR buffer[1024];
            int strLen = ::GetWindowText(edit, buffer, 1024);
            buffer[strLen] = 0;

            std::wstring bstr(buffer);
            if (bstr.length())
                gMiniBrowser->setUserAgent(bstr);
        }

        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            ::EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

void loadURL(BSTR passedURL)
{
    if (FAILED(gMiniBrowser->loadURL(passedURL)))
        return;

    SetFocus(gMiniBrowser->hwnd());
}

void parseCommandLine(bool& usesLayeredWebView, bool& useFullDesktop, bool& pageLoadTesting, _bstr_t& requestedURL, bool& useWK2)
{
    usesLayeredWebView = false;
    useFullDesktop = false;
    pageLoadTesting = false;
    useWK2 = false;

    int argc = 0;
    WCHAR** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        if (!wcsicmp(argv[i], L"--transparent"))
            usesLayeredWebView = true;
        else if (!wcsicmp(argv[i], L"--desktop"))
            useFullDesktop = true;
        else if (!wcsicmp(argv[i], L"--performance"))
            pageLoadTesting = true;
        else if (!wcsicmp(argv[i], L"--highDPI"))
            continue; // ignore
        else if (!wcsicmp(argv[i], L"--wk2"))
            useWK2 = true;
        else if (!requestedURL)
            requestedURL = argv[i];
    }
}
