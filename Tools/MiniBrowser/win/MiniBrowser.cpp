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
#include "MiniBrowser.h"

#include "DOMDefaultImpl.h"
#include "MiniBrowserLibResource.h"
#include "MiniBrowserWebHost.h"
#include "PrintWebUIDelegate.h"
#include "AccessibilityDelegate.h"
#include "ResourceLoadDelegate.h"
#include "WebDownloadDelegate.h"
#include <WebCore/COMPtr.h>
#include <WebKitLegacy/WebKitCOMAPI.h>
#include <wtf/ExportMacros.h>
#include <wtf/Platform.h>
#include <wtf/text/WTFString.h>

#if USE(CF)
#include <CoreFoundation/CFRunLoop.h>
#include <WebKitLegacy/CFDictionaryPropertyBag.h>
#endif

#include <algorithm>
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <vector>

extern POINT s_windowPosition;
extern SIZE s_windowSize;

bool getAppDataFolder(_bstr_t& directory);

namespace WebCore {
float deviceScaleFactorForWindow(HWND);
}

bool MiniBrowser::setCacheFolder()
{
    IWebCachePtr webCache = this->webCache();
    if (!webCache)
        return false;

    _bstr_t appDataFolder;
    if (!getAppDataFolder(appDataFolder))
        return false;

    appDataFolder += L"\\cache";
    webCache->setCacheFolder(appDataFolder);

    return true;
}

static const int maxHistorySize = 10;

typedef _com_ptr_t<_com_IIID<IWebMutableURLRequest, &__uuidof(IWebMutableURLRequest)>> IWebMutableURLRequestPtr;

MiniBrowser::MiniBrowser(HWND mainWnd, HWND urlBarWnd, bool useLayeredWebView, bool pageLoadTesting)
    : m_hMainWnd(mainWnd)
    , m_hURLBarWnd(urlBarWnd)
    , m_useLayeredWebView(useLayeredWebView)
    , m_pageLoadTestClient(std::make_unique<PageLoadTestClient>(this, pageLoadTesting))
{
    IWebDownloadDelegatePtr downloadDelegate;

    if (!seedInitialDefaultPreferences())
        goto exit;

    if (!setToDefaultPreferences())
        goto exit;

    HRESULT hr = init();
    if (FAILED(hr))
        goto exit;

    if (!setCacheFolder())
        goto exit;

    auto webHost = new MiniBrowserWebHost(this, m_hURLBarWnd);

    hr = setFrameLoadDelegate(webHost);
    if (FAILED(hr))
        goto exit;

    hr = setFrameLoadDelegatePrivate(webHost);
    if (FAILED(hr))
        goto exit;

    hr = setUIDelegate(new PrintWebUIDelegate());
    if (FAILED (hr))
        goto exit;

    hr = setAccessibilityDelegate(new AccessibilityDelegate());
    if (FAILED (hr))
        goto exit;

    hr = setResourceLoadDelegate(new ResourceLoadDelegate(this));
    if (FAILED(hr))
        goto exit;

    downloadDelegate.Attach(new WebDownloadDelegate());
    hr = setDownloadDelegate(downloadDelegate);
    if (FAILED(hr))
        goto exit;

    RECT clientRect;
    ::GetClientRect(m_hMainWnd, &clientRect);

    if (usesLayeredWebView())
        clientRect = { s_windowPosition.x, s_windowPosition.y, s_windowPosition.x + s_windowSize.cx, s_windowPosition.y + s_windowSize.cy };

    hr = prepareViews(m_hMainWnd, clientRect, m_viewWnd);
    if (FAILED(hr) || !m_viewWnd)
        goto exit;

    if (usesLayeredWebView())
        subclassForLayeredWindow();

exit:
    return;
}

HRESULT MiniBrowser::init()
{
    HRESULT hr = WebKitCreateInstance(CLSID_WebView, 0, IID_IWebView, reinterpret_cast<void**>(&m_webView.GetInterfacePtr()));
    if (FAILED(hr))
        return hr;

    hr = m_webView->QueryInterface(IID_IWebViewPrivate2, reinterpret_cast<void**>(&m_webViewPrivate.GetInterfacePtr()));
    if (FAILED(hr))
        return hr;

    hr = WebKitCreateInstance(CLSID_WebHistory, 0, __uuidof(m_webHistory), reinterpret_cast<void**>(&m_webHistory.GetInterfacePtr()));
    if (FAILED(hr))
        return hr;

    hr = WebKitCreateInstance(CLSID_WebCoreStatistics, 0, __uuidof(m_statistics), reinterpret_cast<void**>(&m_statistics.GetInterfacePtr()));
    if (FAILED(hr))
        return hr;

    hr = WebKitCreateInstance(CLSID_WebCache, 0, __uuidof(m_webCache), reinterpret_cast<void**>(&m_webCache.GetInterfacePtr()));

    return hr;
}

HRESULT MiniBrowser::prepareViews(HWND mainWnd, const RECT& clientRect, HWND& viewHwnd)
{
    if (!m_webView)
        return E_FAIL;

    HRESULT hr = m_webView->setHostWindow(mainWnd);
    if (FAILED(hr))
        return hr;

    hr = m_webView->initWithFrame(clientRect, 0, 0);
    if (FAILED(hr))
        return hr;

    hr = m_webViewPrivate->setTransparent(m_useLayeredWebView);
    if (FAILED(hr))
        return hr;

    hr = m_webViewPrivate->setUsesLayeredWindow(m_useLayeredWebView);
    if (FAILED(hr))
        return hr;

    hr = m_webViewPrivate->viewWindow(&viewHwnd);

    return hr;
}

bool MiniBrowser::loadHTMLString(const std::wstring& str)
{
    IWebFramePtr frame;
    HRESULT hr = m_webView->mainFrame(&frame.GetInterfacePtr());
    if (FAILED(hr))
        return false;

    frame->loadHTMLString(_bstr_t(str.c_str()).GetBSTR(), 0);
    return true;
}

HRESULT MiniBrowser::setFrameLoadDelegate(IWebFrameLoadDelegate* frameLoadDelegate)
{
    m_frameLoadDelegate = frameLoadDelegate;
    return m_webView->setFrameLoadDelegate(frameLoadDelegate);
}

HRESULT MiniBrowser::setFrameLoadDelegatePrivate(IWebFrameLoadDelegatePrivate* frameLoadDelegatePrivate)
{
    return m_webViewPrivate->setFrameLoadDelegatePrivate(frameLoadDelegatePrivate);
}

HRESULT MiniBrowser::setUIDelegate(IWebUIDelegate* uiDelegate)
{
    m_uiDelegate = uiDelegate;
    return m_webView->setUIDelegate(uiDelegate);
}

HRESULT MiniBrowser::setAccessibilityDelegate(IAccessibilityDelegate* accessibilityDelegate)
{
    m_accessibilityDelegate = accessibilityDelegate;
    return m_webView->setAccessibilityDelegate(accessibilityDelegate);
}

HRESULT MiniBrowser::setResourceLoadDelegate(IWebResourceLoadDelegate* resourceLoadDelegate)
{
    m_resourceLoadDelegate = resourceLoadDelegate;
    return m_webView->setResourceLoadDelegate(resourceLoadDelegate);
}

HRESULT MiniBrowser::setDownloadDelegate(IWebDownloadDelegatePtr downloadDelegate)
{
    m_downloadDelegate = downloadDelegate;
    return m_webView->setDownloadDelegate(downloadDelegate);
}

IWebFramePtr MiniBrowser::mainFrame()
{
    IWebFramePtr framePtr;
    m_webView->mainFrame(&framePtr.GetInterfacePtr());
    return framePtr;
}

bool MiniBrowser::seedInitialDefaultPreferences()
{
    IWebPreferencesPtr tmpPreferences;
    if (FAILED(WebKitCreateInstance(CLSID_WebPreferences, 0, IID_IWebPreferences, reinterpret_cast<void**>(&tmpPreferences.GetInterfacePtr()))))
        return false;

    if (FAILED(tmpPreferences->standardPreferences(&m_standardPreferences.GetInterfacePtr())))
        return false;

    return true;
}

bool MiniBrowser::setToDefaultPreferences()
{
    HRESULT hr = m_standardPreferences->QueryInterface(IID_IWebPreferencesPrivate, reinterpret_cast<void**>(&m_prefsPrivate.GetInterfacePtr()));
    if (!SUCCEEDED(hr))
        return false;

#if USE(CG)
    m_standardPreferences->setAVFoundationEnabled(TRUE);
    m_prefsPrivate->setAcceleratedCompositingEnabled(TRUE);
#endif

    m_prefsPrivate->setFullScreenEnabled(TRUE);
    m_prefsPrivate->setShowDebugBorders(FALSE);
    m_prefsPrivate->setShowRepaintCounter(FALSE);
    m_prefsPrivate->setShouldInvertColors(FALSE);

    m_standardPreferences->setLoadsImagesAutomatically(TRUE);
    m_prefsPrivate->setAuthorAndUserStylesEnabled(TRUE);
    m_standardPreferences->setJavaScriptEnabled(TRUE);
    m_prefsPrivate->setAllowUniversalAccessFromFileURLs(FALSE);
    m_prefsPrivate->setAllowFileAccessFromFileURLs(TRUE);

    m_prefsPrivate->setDeveloperExtrasEnabled(TRUE);

    return true;
}

static void updateMenuItemForHistoryItem(HMENU menu, IWebHistoryItem& historyItem, int currentHistoryItem)
{
    UINT menuID = IDM_HISTORY_LINK0 + currentHistoryItem;

    MENUITEMINFO menuItemInfo = { 0 };
    menuItemInfo.cbSize = sizeof(MENUITEMINFO);
    menuItemInfo.fMask = MIIM_TYPE;
    menuItemInfo.fType = MFT_STRING;

    _bstr_t title;
    historyItem.title(title.GetAddress());
    menuItemInfo.dwTypeData = static_cast<LPWSTR>(title);

    ::SetMenuItemInfo(menu, menuID, FALSE, &menuItemInfo);
    ::EnableMenuItem(menu, menuID, MF_BYCOMMAND | MF_ENABLED);
}

void MiniBrowser::showLastVisitedSites(IWebView& webView)
{
    HMENU menu = ::GetMenu(m_hMainWnd);

    _com_ptr_t<_com_IIID<IWebBackForwardList, &__uuidof(IWebBackForwardList)>> backForwardList;
    HRESULT hr = webView.backForwardList(&backForwardList.GetInterfacePtr());
    if (FAILED(hr))
        return;

    int capacity = 0;
    hr = backForwardList->capacity(&capacity);
    if (FAILED(hr))
        return;

    int backCount = 0;
    hr = backForwardList->backListCount(&backCount);
    if (FAILED(hr))
        return;

    UINT backSetting = MF_BYCOMMAND | ((backCount) ? MF_ENABLED : MF_DISABLED);
    ::EnableMenuItem(menu, IDM_HISTORY_BACKWARD, backSetting);

    int forwardCount = 0;
    hr = backForwardList->forwardListCount(&forwardCount);
    if (FAILED(hr))
        return;

    UINT forwardSetting = MF_BYCOMMAND | ((forwardCount) ? MF_ENABLED : MF_DISABLED);
    ::EnableMenuItem(menu, IDM_HISTORY_FORWARD, forwardSetting);

    IWebHistoryItemPtr currentItem;
    hr = backForwardList->currentItem(&currentItem.GetInterfacePtr());
    if (FAILED(hr))
        return;

    hr = m_webHistory->addItems(1, &currentItem.GetInterfacePtr());
    if (FAILED(hr))
        return;

    _com_ptr_t<_com_IIID<IWebHistoryPrivate, &__uuidof(IWebHistoryPrivate)>> webHistory;
    hr = m_webHistory->QueryInterface(IID_IWebHistoryPrivate, reinterpret_cast<void**>(&webHistory.GetInterfacePtr()));
    if (FAILED(hr))
        return;

    int totalListCount = 0;
    hr = webHistory->allItems(&totalListCount, 0);
    if (FAILED(hr))
        return;

    m_historyItems.resize(totalListCount);

    std::vector<IWebHistoryItem*> historyToLoad(totalListCount);
    hr = webHistory->allItems(&totalListCount, historyToLoad.data());
    if (FAILED(hr))
        return;

    size_t i = 0;
    for (auto& cur : historyToLoad) {
        m_historyItems[i].Attach(cur);
        ++i;
    }

    int allItemsOffset = 0;
    if (totalListCount > maxHistorySize)
        allItemsOffset = totalListCount - maxHistorySize;

    int currentHistoryItem = 0;
    for (int i = 0; i < m_historyItems.size() && (allItemsOffset + currentHistoryItem) < m_historyItems.size(); ++i) {
        updateMenuItemForHistoryItem(menu, *(m_historyItems[allItemsOffset + currentHistoryItem]), currentHistoryItem);
        ++currentHistoryItem;
    }

    // Hide any history we aren't using yet.
    for (int i = currentHistoryItem; i < maxHistorySize; ++i)
        ::EnableMenuItem(menu, IDM_HISTORY_LINK0 + i, MF_BYCOMMAND | MF_DISABLED);
}

void MiniBrowser::launchInspector()
{
    if (!m_webViewPrivate)
        return;

    if (!SUCCEEDED(m_webViewPrivate->inspector(&m_inspector.GetInterfacePtr())))
        return;

    m_inspector->show();
}

void MiniBrowser::navigateForwardOrBackward(bool isBackward)
{
    if (!m_webView)
        return;

    BOOL wentBackOrForward = FALSE;
    if (!isBackward)
        m_webView->goForward(&wentBackOrForward);
    else
        m_webView->goBack(&wentBackOrForward);
}

void MiniBrowser::navigateToHistory(unsigned historyEntry)
{
    if (!m_webView)
        return;

    if (historyEntry > m_historyItems.size())
        return;

    IWebHistoryItemPtr desiredHistoryItem = m_historyItems[historyEntry];
    if (!desiredHistoryItem)
        return;

    BOOL succeeded = FALSE;
    m_webView->goToBackForwardItem(desiredHistoryItem, &succeeded);

    _bstr_t frameURL;
    desiredHistoryItem->URLString(frameURL.GetAddress());

    ::SendMessage(m_hURLBarWnd, (UINT)WM_SETTEXT, 0, (LPARAM)frameURL.GetBSTR());
}

bool MiniBrowser::goBack()
{
    BOOL wentBack = FALSE;
    m_webView->goBack(&wentBack);
    return wentBack;
}

bool MiniBrowser::goForward()
{
    BOOL wentForward = FALSE;
    m_webView->goForward(&wentForward);
    return wentForward;
}

bool MiniBrowser::loadURL(const std::wstring& url)
{
    _bstr_t urlBStr(url.c_str());
    if (!!urlBStr && (::PathFileExists(urlBStr) || ::PathIsUNC(urlBStr))) {
        TCHAR fileURL[INTERNET_MAX_URL_LENGTH];
        DWORD fileURLLength = sizeof(fileURL) / sizeof(fileURL[0]);

        if (SUCCEEDED(::UrlCreateFromPath(urlBStr, fileURL, &fileURLLength, 0)))
            urlBStr = fileURL;
    }

    IWebFramePtr frame;
    HRESULT hr = m_webView->mainFrame(&frame.GetInterfacePtr());
    if (FAILED(hr))
        return false;

    IWebMutableURLRequestPtr request;
    hr = WebKitCreateInstance(CLSID_WebMutableURLRequest, 0, IID_IWebMutableURLRequest, (void**)&request);
    if (FAILED(hr))
        return false;

    hr = request->initWithURL(wcsstr(static_cast<wchar_t*>(urlBStr), L"://") ? urlBStr : _bstr_t(L"http://") + urlBStr, WebURLRequestUseProtocolCachePolicy, 60);
    if (FAILED(hr))
        return false;

    _bstr_t methodBStr(L"GET");
    hr = request->setHTTPMethod(methodBStr);
    if (FAILED(hr))
        return false;

    hr = frame->loadRequest(request);

    return true;
}

void MiniBrowser::exitProgram()
{
    ::PostMessage(m_hMainWnd, static_cast<UINT>(WM_COMMAND), MAKELPARAM(IDM_EXIT, 0), 0);
}

void MiniBrowser::setUserAgent(const std::wstring& customUserAgent)
{
    _bstr_t s;
    if (!customUserAgent.empty())
        s = customUserAgent.c_str();
    webView()->setCustomUserAgent(s.GetBSTR());
}

std::wstring MiniBrowser::userAgent()
{
    _bstr_t userAgent;
    if (FAILED(webView()->customUserAgent(&userAgent.GetBSTR())))
        return L"- Unknown -: Call failed.";

    return std::wstring(userAgent);
}

typedef _com_ptr_t<_com_IIID<IWebIBActions, &__uuidof(IWebIBActions)>> IWebIBActionsPtr;

void MiniBrowser::resetZoom()
{
    IWebIBActionsPtr webActions;
    if (FAILED(m_webView->QueryInterface(IID_IWebIBActions, reinterpret_cast<void**>(&webActions.GetInterfacePtr()))))
        return;

    webActions->resetPageZoom(nullptr);
}

void MiniBrowser::zoomIn()
{
    IWebIBActionsPtr webActions;
    if (FAILED(m_webView->QueryInterface(IID_IWebIBActions, reinterpret_cast<void**>(&webActions.GetInterfacePtr()))))
        return;

    webActions->zoomPageIn(nullptr);
}

void MiniBrowser::zoomOut()
{
    IWebIBActionsPtr webActions;
    if (FAILED(m_webView->QueryInterface(IID_IWebIBActions, reinterpret_cast<void**>(&webActions.GetInterfacePtr()))))
        return;

    webActions->zoomPageOut(nullptr);
}

typedef _com_ptr_t<_com_IIID<IWebViewPrivate3, &__uuidof(IWebViewPrivate3)>> IWebViewPrivate3Ptr;

void MiniBrowser::showLayerTree()
{
    IWebViewPrivate3Ptr webViewPrivate;
    if (FAILED(m_webView->QueryInterface(IID_IWebViewPrivate3, reinterpret_cast<void**>(&webViewPrivate.GetInterfacePtr()))))
        return;

    OutputDebugString(L"CURRENT TREE:\n");

    _bstr_t layerTreeBstr;
    if (FAILED(webViewPrivate->layerTreeAsString(layerTreeBstr.GetAddress())))
        OutputDebugString(L"    Failed to retrieve the layer tree.\n");
    else
        OutputDebugString(layerTreeBstr);
    OutputDebugString(L"\n\n");
}

static void setWindowText(HWND dialog, UINT field, _bstr_t value)
{
    ::SetDlgItemText(dialog, field, value);
}

static void setWindowText(HWND dialog, UINT field, UINT value)
{
    String valueStr = WTF::String::number(value);

    setWindowText(dialog, field, _bstr_t(valueStr.utf8().data()));
}

typedef _com_ptr_t<_com_IIID<IPropertyBag, &__uuidof(IPropertyBag)>> IPropertyBagPtr;

static void setWindowText(HWND dialog, UINT field, IPropertyBagPtr statistics, const _bstr_t& key)
{
    _variant_t var;
    V_VT(&var) = VT_UI8;
    if (FAILED(statistics->Read(key, &var.GetVARIANT(), nullptr)))
        return;

    unsigned long long value = V_UI8(&var);
    String valueStr = WTF::String::number(value);

    setWindowText(dialog, field, _bstr_t(valueStr.utf8().data()));
}

static void setWindowText(HWND dialog, UINT field, CFDictionaryRef dictionary, CFStringRef key, UINT& total)
{
    CFNumberRef countNum = static_cast<CFNumberRef>(CFDictionaryGetValue(dictionary, key));
    if (!countNum)
        return;

    int count = 0;
    CFNumberGetValue(countNum, kCFNumberIntType, &count);

    setWindowText(dialog, field, static_cast<UINT>(count));
    total += count;
}

void MiniBrowser::updateStatistics(HWND dialog)
{
    IWebCoreStatisticsPtr webCoreStatistics = this->statistics();
    if (!webCoreStatistics)
        return;

    IPropertyBagPtr statistics;
    HRESULT hr = webCoreStatistics->memoryStatistics(&statistics.GetInterfacePtr());
    if (FAILED(hr))
        return;

    // FastMalloc.
    setWindowText(dialog, IDC_RESERVED_VM, statistics, "FastMallocReservedVMBytes");
    setWindowText(dialog, IDC_COMMITTED_VM, statistics, "FastMallocCommittedVMBytes");
    setWindowText(dialog, IDC_FREE_LIST_BYTES, statistics, "FastMallocFreeListBytes");

    // WebCore Cache.
#if USE(CF)
    IWebCachePtr webCache = this->webCache();

    int dictCount = 6;
    IPropertyBag* cacheDict[6] = { 0 };
    if (FAILED(webCache->statistics(&dictCount, cacheDict)))
        return;

    COMPtr<CFDictionaryPropertyBag> counts, sizes, liveSizes, decodedSizes, purgableSizes;
    counts.adoptRef(reinterpret_cast<CFDictionaryPropertyBag*>(cacheDict[0]));
    sizes.adoptRef(reinterpret_cast<CFDictionaryPropertyBag*>(cacheDict[1]));
    liveSizes.adoptRef(reinterpret_cast<CFDictionaryPropertyBag*>(cacheDict[2]));
    decodedSizes.adoptRef(reinterpret_cast<CFDictionaryPropertyBag*>(cacheDict[3]));
    purgableSizes.adoptRef(reinterpret_cast<CFDictionaryPropertyBag*>(cacheDict[4]));

    static CFStringRef imagesKey = CFSTR("images");
    static CFStringRef stylesheetsKey = CFSTR("style sheets");
    static CFStringRef xslKey = CFSTR("xsl");
    static CFStringRef scriptsKey = CFSTR("scripts");

    if (counts) {
        UINT totalObjects = 0;
        setWindowText(dialog, IDC_IMAGES_OBJECT_COUNT, counts->dictionary(), imagesKey, totalObjects);
        setWindowText(dialog, IDC_CSS_OBJECT_COUNT, counts->dictionary(), stylesheetsKey, totalObjects);
        setWindowText(dialog, IDC_XSL_OBJECT_COUNT, counts->dictionary(), xslKey, totalObjects);
        setWindowText(dialog, IDC_JSC_OBJECT_COUNT, counts->dictionary(), scriptsKey, totalObjects);
        setWindowText(dialog, IDC_TOTAL_OBJECT_COUNT, totalObjects);
    }

    if (sizes) {
        UINT totalBytes = 0;
        setWindowText(dialog, IDC_IMAGES_BYTES, sizes->dictionary(), imagesKey, totalBytes);
        setWindowText(dialog, IDC_CSS_BYTES, sizes->dictionary(), stylesheetsKey, totalBytes);
        setWindowText(dialog, IDC_XSL_BYTES, sizes->dictionary(), xslKey, totalBytes);
        setWindowText(dialog, IDC_JSC_BYTES, sizes->dictionary(), scriptsKey, totalBytes);
        setWindowText(dialog, IDC_TOTAL_BYTES, totalBytes);
    }

    if (liveSizes) {
        UINT totalLiveBytes = 0;
        setWindowText(dialog, IDC_IMAGES_LIVE_COUNT, liveSizes->dictionary(), imagesKey, totalLiveBytes);
        setWindowText(dialog, IDC_CSS_LIVE_COUNT, liveSizes->dictionary(), stylesheetsKey, totalLiveBytes);
        setWindowText(dialog, IDC_XSL_LIVE_COUNT, liveSizes->dictionary(), xslKey, totalLiveBytes);
        setWindowText(dialog, IDC_JSC_LIVE_COUNT, liveSizes->dictionary(), scriptsKey, totalLiveBytes);
        setWindowText(dialog, IDC_TOTAL_LIVE_COUNT, totalLiveBytes);
    }

    if (decodedSizes) {
        UINT totalDecoded = 0;
        setWindowText(dialog, IDC_IMAGES_DECODED_COUNT, decodedSizes->dictionary(), imagesKey, totalDecoded);
        setWindowText(dialog, IDC_CSS_DECODED_COUNT, decodedSizes->dictionary(), stylesheetsKey, totalDecoded);
        setWindowText(dialog, IDC_XSL_DECODED_COUNT, decodedSizes->dictionary(), xslKey, totalDecoded);
        setWindowText(dialog, IDC_JSC_DECODED_COUNT, decodedSizes->dictionary(), scriptsKey, totalDecoded);
        setWindowText(dialog, IDC_TOTAL_DECODED, totalDecoded);
    }

    if (purgableSizes) {
        UINT totalPurgable = 0;
        setWindowText(dialog, IDC_IMAGES_PURGEABLE_COUNT, purgableSizes->dictionary(), imagesKey, totalPurgable);
        setWindowText(dialog, IDC_CSS_PURGEABLE_COUNT, purgableSizes->dictionary(), stylesheetsKey, totalPurgable);
        setWindowText(dialog, IDC_XSL_PURGEABLE_COUNT, purgableSizes->dictionary(), xslKey, totalPurgable);
        setWindowText(dialog, IDC_JSC_PURGEABLE_COUNT, purgableSizes->dictionary(), scriptsKey, totalPurgable);
        setWindowText(dialog, IDC_TOTAL_PURGEABLE, totalPurgable);
    }
#endif

    // JavaScript Heap.
    setWindowText(dialog, IDC_JSC_HEAP_SIZE, statistics, "JavaScriptHeapSize");
    setWindowText(dialog, IDC_JSC_HEAP_FREE, statistics, "JavaScriptFreeSize");

    UINT count;
    if (SUCCEEDED(webCoreStatistics->javaScriptObjectsCount(&count)))
        setWindowText(dialog, IDC_TOTAL_JSC_HEAP_OBJECTS, count);
    if (SUCCEEDED(webCoreStatistics->javaScriptGlobalObjectsCount(&count)))
        setWindowText(dialog, IDC_GLOBAL_JSC_HEAP_OBJECTS, count);
    if (SUCCEEDED(webCoreStatistics->javaScriptProtectedObjectsCount(&count)))
        setWindowText(dialog, IDC_PROTECTED_JSC_HEAP_OBJECTS, count);

    // Font and Glyph Caches.
    if (SUCCEEDED(webCoreStatistics->cachedFontDataCount(&count)))
        setWindowText(dialog, IDC_TOTAL_FONT_OBJECTS, count);
    if (SUCCEEDED(webCoreStatistics->cachedFontDataInactiveCount(&count)))
        setWindowText(dialog, IDC_INACTIVE_FONT_OBJECTS, count);
    if (SUCCEEDED(webCoreStatistics->glyphPageCount(&count)))
        setWindowText(dialog, IDC_GLYPH_PAGES, count);

    // Site Icon Database.
    if (SUCCEEDED(webCoreStatistics->iconPageURLMappingCount(&count)))
        setWindowText(dialog, IDC_PAGE_URL_MAPPINGS, count);
    if (SUCCEEDED(webCoreStatistics->iconRetainedPageURLCount(&count)))
        setWindowText(dialog, IDC_RETAINED_PAGE_URLS, count);
    if (SUCCEEDED(webCoreStatistics->iconRecordCount(&count)))
        setWindowText(dialog, IDC_SITE_ICON_RECORDS, count);
    if (SUCCEEDED(webCoreStatistics->iconsWithDataCount(&count)))
        setWindowText(dialog, IDC_SITE_ICONS_WITH_DATA, count);
}

static BOOL CALLBACK AbortProc(HDC hDC, int Error)
{
    MSG msg;
    while (::PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
    }

    return TRUE;
}

static HDC getPrinterDC()
{
    PRINTDLG pdlg;
    memset(&pdlg, 0, sizeof(PRINTDLG));
    pdlg.lStructSize = sizeof(PRINTDLG);
    pdlg.Flags = PD_PRINTSETUP | PD_RETURNDC;

    ::PrintDlg(&pdlg);

    return pdlg.hDC;
}

static void initDocStruct(DOCINFO* di, TCHAR* docname)
{
    memset(di, 0, sizeof(DOCINFO));
    di->cbSize = sizeof(DOCINFO);
    di->lpszDocName = docname;
}

typedef _com_ptr_t<_com_IIID<IWebFramePrivate, &__uuidof(IWebFramePrivate)>> IWebFramePrivatePtr;

void MiniBrowser::print()
{
    HDC printDC = getPrinterDC();
    if (!printDC) {
        ::MessageBoxW(0, L"Error creating printing DC", L"Error", MB_APPLMODAL | MB_OK);
        return;
    }

    if (::SetAbortProc(printDC, AbortProc) == SP_ERROR) {
        ::MessageBoxW(0, L"Error setting up AbortProc", L"Error", MB_APPLMODAL | MB_OK);
        return;
    }

    IWebFramePtr frame = mainFrame();
    if (!frame)
        return;

    IWebFramePrivatePtr framePrivate;
    if (FAILED(frame->QueryInterface(&framePrivate.GetInterfacePtr())))
        return;

    framePrivate->setInPrintingMode(TRUE, printDC);

    UINT pageCount = 0;
    framePrivate->getPrintedPageCount(printDC, &pageCount);

    DOCINFO di;
    initDocStruct(&di, L"WebKit Doc");
    ::StartDoc(printDC, &di);

    // FIXME: Need CoreGraphics implementation
    void* graphicsContext = 0;
    for (size_t page = 1; page <= pageCount; ++page) {
        ::StartPage(printDC);
        framePrivate->spoolPages(printDC, page, page, graphicsContext);
        ::EndPage(printDC);
    }

    framePrivate->setInPrintingMode(FALSE, printDC);

    ::EndDoc(printDC);
    ::DeleteDC(printDC);
}

void MiniBrowser::setAVFoundationEnabled(bool enabled)
{
    standardPreferences()->setAVFoundationEnabled(enabled);
}

void MiniBrowser::setAcceleratedCompositingEnabled(bool enabled)
{
    privatePreferences()->setAcceleratedCompositingEnabled(enabled);
}

void MiniBrowser::setAuthorAndUserStylesEnabled(bool enabled)
{
    privatePreferences()->setAuthorAndUserStylesEnabled(enabled);
}

void MiniBrowser::setFullScreenEnabled(bool enabled)
{
    privatePreferences()->setFullScreenEnabled(enabled);
}

void MiniBrowser::setJavaScriptEnabled(bool enabled)
{
    standardPreferences()->setJavaScriptEnabled(enabled);
}

void MiniBrowser::setLoadsImagesAutomatically(bool enabled)
{
    standardPreferences()->setLoadsImagesAutomatically(enabled);
}

void MiniBrowser::setLocalFileRestrictionsEnabled(bool enabled)
{
    privatePreferences()->setAllowUniversalAccessFromFileURLs(!enabled);
    privatePreferences()->setAllowFileAccessFromFileURLs(!enabled);
}

void MiniBrowser::setShouldInvertColors(bool enabled)
{
    privatePreferences()->setShouldInvertColors(enabled);
}

void MiniBrowser::setShowCompositingBorders(bool enabled)
{
    privatePreferences()->setShowDebugBorders(enabled);
    privatePreferences()->setShowRepaintCounter(enabled);
}

void MiniBrowser::setShowTiledScrollingIndicator(bool enabled)
{
    privatePreferences()->setShowTiledScrollingIndicator(enabled);
}

static WNDPROC gDefWebKitProc;

static LRESULT CALLBACK viewWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_NCHITTEST:
        constexpr int dragBarHeight = 30;
        RECT window;
        ::GetWindowRect(hWnd, &window);
        // For testing our transparent window, we need a region to use as a handle for
        // dragging. The right way to do this would be to query the web view to see what's
        // under the mouse. However, for testing purposes we just use an arbitrary
        // 30 logical pixel band at the top of the view as an arbitrary gripping location.
        //
        // When we are within this bad, return HT_CAPTION to tell Windows we want to
        // treat this region as if it were the title bar on a normal window.
        int y = HIWORD(lParam);
        float scaledDragBarHeightFactor = dragBarHeight * WebCore::deviceScaleFactorForWindow(hWnd);
        if ((y > window.top) && (y < window.top + scaledDragBarHeightFactor))
            return HTCAPTION;
    }
    return CallWindowProc(gDefWebKitProc, hWnd, message, wParam, lParam);
}

void MiniBrowser::subclassForLayeredWindow()
{
#if defined _M_AMD64 || defined _WIN64
    gDefWebKitProc = reinterpret_cast<WNDPROC>(::GetWindowLongPtr(m_viewWnd, GWLP_WNDPROC));
    ::SetWindowLongPtr(m_viewWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(viewWndProc));
#else
    gDefWebKitProc = reinterpret_cast<WNDPROC>(::GetWindowLong(m_viewWnd, GWL_WNDPROC));
    ::SetWindowLong(m_viewWnd, GWL_WNDPROC, reinterpret_cast<LONG_PTR>(viewWndProc));
#endif
}
