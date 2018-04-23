/*
 * Copyright (C) 2006, 2008, 2013-2015 Apple Inc.  All rights reserved.
 * Copyright (C) 2009, 2011 Brent Fulgham.  All rights reserved.
 * Copyright (C) 2009, 2010, 2011 Appcelerator, Inc. All rights reserved.
 * Copyright (C) 2013 Alex Christensen. All rights reserved.
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
#include "stdafx.h"

#include "BrowserWindow.h"
#include "MiniBrowser.h"
#include "MiniBrowserLibResource.h"
#include "MiniBrowserReplace.h"

#if ENABLE(WEBKIT)
#include "WK2ContentWindow.h"
#endif

#define URLBAR_HEIGHT  24
#define CONTROLBUTTON_WIDTH 24
#define MAX_LOADSTRING 100
TCHAR szTitle[MAX_LOADSTRING]; // The title bar text
TCHAR szWindowClass[MAX_LOADSTRING]; // the main window class name

extern HINSTANCE hInst;
extern WNDPROC DefEditProc;
extern WNDPROC DefButtonProc;
extern BrowserWindow* gBrowserWindow;
extern ContentWindow* gMiniBrowser;

void loadURL(BSTR passedURL);
LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK BackButtonProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ForwardButtonProc(HWND, UINT, WPARAM, LPARAM);

namespace WebCore {
float deviceScaleFactorForWindow(HWND);
}

static ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEX wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = BrowserWindow::WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MINIBROWSER));
    wcex.hCursor        = LoadCursor(0, IDC_ARROW);
    wcex.hbrBackground  = 0;
    wcex.lpszMenuName   = MAKEINTRESOURCE(IDC_MINIBROWSER);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassEx(&wcex);
}

BrowserWindow::BrowserWindow(int nCmdShow, bool usesLayeredWebView, bool pageLoadTesting, _bstr_t requestedURL, bool useWK2)
    : m_usesLayeredWebView(usesLayeredWebView)
{
    // Initialize global strings
    LoadString(hInst, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadString(hInst, IDC_MINIBROWSER, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInst);

    float scaleFactor = WebCore::deviceScaleFactorForWindow(nullptr);

    m_hMainWnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, 0, 0, hInst, 0);

    m_hBackButtonWnd = CreateWindow(L"BUTTON", L"<", WS_CHILD | WS_VISIBLE  | BS_TEXT, 0, 0, 0, 0, m_hMainWnd, 0, hInst, 0);
    m_hForwardButtonWnd = CreateWindow(L"BUTTON", L">", WS_CHILD | WS_VISIBLE | BS_TEXT, scaleFactor * CONTROLBUTTON_WIDTH, 0, 0, 0, m_hMainWnd, 0, hInst, 0);
    m_hURLBarWnd = CreateWindow(L"EDIT", 0, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOVSCROLL, scaleFactor * CONTROLBUTTON_WIDTH * 2, 0, 0, 0, m_hMainWnd, 0, hInst, 0);

    updateDeviceScaleFactor();

    DefEditProc = reinterpret_cast<WNDPROC>(GetWindowLongPtr(m_hURLBarWnd, GWLP_WNDPROC));
    DefButtonProc = reinterpret_cast<WNDPROC>(GetWindowLongPtr(m_hBackButtonWnd, GWLP_WNDPROC));
    SetWindowLongPtr(m_hURLBarWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditProc));
    SetWindowLongPtr(m_hBackButtonWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(BackButtonProc));
    SetWindowLongPtr(m_hForwardButtonWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ForwardButtonProc));

    SetFocus(m_hURLBarWnd);

    std::unique_ptr<ContentWindow> (*factory)(HWND, HWND, bool, bool);
    factory = [](HWND mainWnd, HWND urlBarWnd, bool useLayeredWebView, bool pageLoadTesting) -> std::unique_ptr<ContentWindow> {
        return std::make_unique<MiniBrowser>(mainWnd, urlBarWnd, useLayeredWebView, pageLoadTesting);
    };
#if ENABLE(WEBKIT)
    if (useWK2) {
        factory = [](HWND mainWnd, HWND urlBarWnd, bool, bool) -> std::unique_ptr<ContentWindow> {
            return std::make_unique<WK2ContentWindow>(mainWnd, urlBarWnd);
        };
    }
#endif
    m_contentWindow = factory(usesLayeredWebView ? nullptr : m_hMainWnd, m_hURLBarWnd, usesLayeredWebView, pageLoadTesting);
    gMiniBrowser = m_contentWindow.get();

    resizeSubViews();
    ShowWindow(m_hMainWnd, nCmdShow);

    if (requestedURL.length())
        loadURL(requestedURL.GetBSTR());
    else
        m_contentWindow->loadHTMLString(defaultHTML);

    return;
}

void BrowserWindow::updateDeviceScaleFactor()
{
    auto scaleFactor = WebCore::deviceScaleFactorForWindow(m_hMainWnd);

    if (m_hURLBarFont)
        ::DeleteObject(m_hURLBarFont);

    m_hURLBarFont = ::CreateFont(scaleFactor * 18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FF_DONTCARE, L"Times New Roman");
}

void BrowserWindow::resizeSubViews()
{
    float scaleFactor = WebCore::deviceScaleFactorForWindow(m_hMainWnd);

    RECT rcClient;
    GetClientRect(m_hMainWnd, &rcClient);

    int height = scaleFactor * URLBAR_HEIGHT;
    int width = scaleFactor * CONTROLBUTTON_WIDTH;

    MoveWindow(m_hBackButtonWnd, 0, 0, width, height, TRUE);
    MoveWindow(m_hForwardButtonWnd, width, 0, width, height, TRUE);
    MoveWindow(m_hURLBarWnd, width * 2, 0, rcClient.right, height, TRUE);
    if (!m_usesLayeredWebView)
        MoveWindow(contentWindow()->hwnd(), 0, height, rcClient.right, rcClient.bottom - height, TRUE);

    ::SendMessage(m_hURLBarWnd, static_cast<UINT>(WM_SETFONT), reinterpret_cast<WPARAM>(urlBarFont()), TRUE);
}

static INT_PTR CALLBACK authDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_INITDIALOG: {
        HWND edit = ::GetDlgItem(hDlg, IDC_AUTH_USER);
        ::SetWindowText(edit, L"");

        edit = ::GetDlgItem(hDlg, IDC_AUTH_PASSWORD);
        ::SetWindowText(edit, L"");
        return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            INT_PTR result { };

            if (LOWORD(wParam) == IDOK) {
                TCHAR user[256];
                int strLen = ::GetWindowText(::GetDlgItem(hDlg, IDC_AUTH_USER), user, 256);
                user[strLen] = 0;

                TCHAR pass[256];
                strLen = ::GetWindowText(::GetDlgItem(hDlg, IDC_AUTH_PASSWORD), pass, 256);
                pass[strLen] = 0;

                result = reinterpret_cast<INT_PTR>(new std::pair<std::wstring, std::wstring>(user, pass));
            }

            ::EndDialog(hDlg, result);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

bool BrowserWindow::displayAuthDialog(std::wstring& username, std::wstring& password)
{
    auto result = DialogBox(hInst, MAKEINTRESOURCE(IDD_AUTH), m_hMainWnd, authDialogProc);
    if (!result)
        return E_FAIL;

    auto pair = reinterpret_cast<std::pair<std::wstring, std::wstring>*>(result);
    username = pair->first;
    password = pair->second;
    delete pair;

    return S_OK;
}
