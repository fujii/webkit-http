/*
 * Copyright (C) 2018 Apple Inc. All rights reserved.
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

#import "config.h"
#import "WKPDFView.h"

#if ENABLE(WKPDFVIEW)

#import "FindClient.h"
#import "WKWebViewInternal.h"
#import "WeakObjCPtr.h"
#import "WebPageProxy.h"
#import "_WKWebViewPrintFormatterInternal.h"
#import <PDFKit/PDFHostViewController.h>
#import <wtf/BlockPtr.h>
#import <wtf/MainThread.h>
#import <wtf/RetainPtr.h>

@interface WKPDFView () <PDFHostViewControllerDelegate>
@end

@implementation WKPDFView {
    RetainPtr<NSData> _data;
    BlockPtr<void()> _findCompletion;
    RetainPtr<NSString> _findString;
    NSUInteger _findStringCount;
    NSUInteger _findStringMaxCount;
    RetainPtr<UIView> _fixedOverlayView;
    std::optional<NSUInteger> _focusedSearchResultIndex;
    NSInteger _focusedSearchResultPendingOffset;
    RetainPtr<PDFHostViewController> _hostViewController;
    CGSize _overlaidAccessoryViewsInset;
    RetainPtr<UIView> _pageNumberIndicator;
    RetainPtr<NSString> _suggestedFilename;
    WebKit::WeakObjCPtr<WKWebView> _webView;
}

- (void)dealloc
{
    [[_hostViewController view] removeFromSuperview];
    [_pageNumberIndicator removeFromSuperview];
    [super dealloc];
}

- (BOOL)gestureRecognizerShouldBegin:(UIGestureRecognizer *)gestureRecognizer
{
    return [_hostViewController gestureRecognizerShouldBegin:gestureRecognizer];
}

#pragma mark WKWebViewContentProvider

- (instancetype)web_initWithFrame:(CGRect)frame webView:(WKWebView *)webView
{
    if (!(self = [super initWithFrame:frame webView:webView]))
        return nil;

    self.backgroundColor = UIColor.grayColor;
    webView.scrollView.backgroundColor = UIColor.grayColor;

    _webView = webView;
    return self;
}

- (void)web_setContentProviderData:(NSData *)data suggestedFilename:(NSString *)filename
{
    _data = adoptNS([data copy]);
    _suggestedFilename = adoptNS([filename copy]);

    [PDFHostViewController createHostView:^(PDFHostViewController * _Nullable hostViewController) {
        ASSERT(isMainThread());

        WKWebView *webView = _webView.getAutoreleased();
        if (!webView)
            return;

        if (!hostViewController)
            return;
        _hostViewController = hostViewController;

        UIView *hostView = hostViewController.view;
        hostView.frame = webView.bounds;
        hostView.backgroundColor = UIColor.grayColor;

        UIScrollView *scrollView = webView.scrollView;
        [self removeFromSuperview];
        [scrollView addSubview:hostView];

        _pageNumberIndicator = hostViewController.pageNumberIndicator;
        [_fixedOverlayView addSubview:_pageNumberIndicator.get()];

        hostViewController.delegate = self;
        [hostViewController setDocumentData:_data.get() withScrollView:scrollView];
    } forExtensionIdentifier:nil];
}

- (CGPoint)_offsetForPageNumberIndicator
{
    WKWebView *webView = _webView.getAutoreleased();
    if (!webView)
        return CGPointZero;

    UIEdgeInsets insets = UIEdgeInsetsAdd(webView._computedUnobscuredSafeAreaInset, webView._computedObscuredInset, UIRectEdgeAll);
    return CGPointMake(insets.left, insets.top + _overlaidAccessoryViewsInset.height);
}

- (void)_movePageNumberIndicatorToPoint:(CGPoint)point animated:(BOOL)animated
{
    void (^setFrame)() = ^{
        static const CGFloat margin = 20;
        const CGRect frame = { CGPointMake(point.x + margin, point.y + margin), [_pageNumberIndicator frame].size };
        [_pageNumberIndicator setFrame:frame];
    };

    if (animated) {
        static const NSTimeInterval duration = 0.3;
        [UIView animateWithDuration:duration animations:setFrame];
        return;
    }

    setFrame();
}

- (void)_updateLayoutAnimated:(BOOL)animated
{
    [_hostViewController updatePDFViewLayout];
    [self _movePageNumberIndicatorToPoint:self._offsetForPageNumberIndicator animated:animated];
}

- (void)web_setMinimumSize:(CGSize)size
{
    self.frame = { self.frame.origin, size };
    [self _updateLayoutAnimated:NO];
}

- (void)web_setOverlaidAccessoryViewsInset:(CGSize)inset
{
    _overlaidAccessoryViewsInset = inset;
    [self _updateLayoutAnimated:YES];
}

- (void)web_computedContentInsetDidChange
{
    [self _updateLayoutAnimated:NO];
}

- (void)web_setFixedOverlayView:(UIView *)fixedOverlayView
{
    _fixedOverlayView = fixedOverlayView;
}

- (void)_scrollToURLFragment:(NSString *)fragment
{
    NSInteger pageIndex = 0;
    if ([fragment hasPrefix:@"page"])
        pageIndex = [[fragment substringFromIndex:4] integerValue] - 1;

    if (pageIndex >= 0 && pageIndex < [_hostViewController pageCount] && pageIndex != [_hostViewController currentPageIndex])
        [_hostViewController goToPageIndex:pageIndex];
}

- (void)web_didSameDocumentNavigation:(WKSameDocumentNavigationType)navigationType
{
    if (navigationType == kWKSameDocumentNavigationSessionStatePop)
        [self _scrollToURLFragment:[_webView URL].fragment];
}

static NSStringCompareOptions stringCompareOptions(_WKFindOptions findOptions)
{
    NSStringCompareOptions compareOptions = 0;
    if (findOptions & _WKFindOptionsBackwards)
        compareOptions |= NSBackwardsSearch;
    if (findOptions & _WKFindOptionsCaseInsensitive)
        compareOptions |= NSCaseInsensitiveSearch;
    return compareOptions;
}

- (void)_resetFind
{
    if (_findCompletion)
        [_hostViewController cancelFindString];

    _findCompletion = nil;
    _findString = nil;
    _findStringCount = 0;
    _findStringMaxCount = 0;
    _focusedSearchResultIndex = std::nullopt;
    _focusedSearchResultPendingOffset = 0;
}

- (void)_findString:(NSString *)string withOptions:(_WKFindOptions)options maxCount:(NSUInteger)maxCount completion:(void(^)())completion
{
    [self _resetFind];

    _findCompletion = completion;
    _findString = adoptNS([string copy]);
    _findStringMaxCount = maxCount;
    [_hostViewController findString:_findString.get() withOptions:stringCompareOptions(options)];
}

- (void)web_countStringMatches:(NSString *)string options:(_WKFindOptions)options maxCount:(NSUInteger)maxCount
{
    [self _findString:string withOptions:options maxCount:maxCount completion:^{
        ASSERT([_findString isEqualToString:string]);
        if (auto page = [_webView _page])
            page->findClient().didCountStringMatches(page, _findString.get(), _findStringCount);
    }];
}

- (BOOL)_computeFocusedSearchResultIndexWithOptions:(_WKFindOptions)options didWrapAround:(BOOL *)didWrapAround
{
    BOOL isBackwards = options & _WKFindOptionsBackwards;
    NSInteger singleOffset = isBackwards ? -1 : 1;

    if (_findCompletion) {
        ASSERT(!_focusedSearchResultIndex);
        _focusedSearchResultPendingOffset += singleOffset;
        return NO;
    }

    if (!_findStringCount)
        return NO;

    NSInteger newIndex;
    if (_focusedSearchResultIndex) {
        ASSERT(!_focusedSearchResultPendingOffset);
        newIndex = *_focusedSearchResultIndex + singleOffset;
    } else {
        newIndex = isBackwards ? _findStringCount - 1 : 0;
        newIndex += std::exchange(_focusedSearchResultPendingOffset, 0);
    }

    if (newIndex < 0 || static_cast<NSUInteger>(newIndex) >= _findStringCount) {
        if (!(options & _WKFindOptionsWrapAround))
            return NO;

        NSUInteger wrappedIndex = std::abs(newIndex) % _findStringCount;
        if (newIndex < 0)
            wrappedIndex = _findStringCount - wrappedIndex;
        newIndex = wrappedIndex;
        *didWrapAround = YES;
    }

    _focusedSearchResultIndex = newIndex;
    ASSERT(*_focusedSearchResultIndex < _findStringCount);
    return YES;
}

- (void)_focusOnSearchResultWithOptions:(_WKFindOptions)options
{
    auto page = [_webView _page];
    if (!page)
        return;

    BOOL didWrapAround = NO;
    if (![self _computeFocusedSearchResultIndexWithOptions:options didWrapAround:&didWrapAround]) {
        if (!_findCompletion)
            page->findClient().didFailToFindString(page, _findString.get());
        return;
    }

    auto focusedIndex = *_focusedSearchResultIndex;
    [_hostViewController focusOnSearchResultAtIndex:focusedIndex];
    page->findClient().didFindString(page, _findString.get(), { }, _findStringCount, focusedIndex, didWrapAround);
}

- (void)web_findString:(NSString *)string options:(_WKFindOptions)options maxCount:(NSUInteger)maxCount
{
    if ([_findString isEqualToString:string]) {
        [self _focusOnSearchResultWithOptions:options];
        return;
    }

    [self _findString:string withOptions:options maxCount:maxCount completion:^{
        ASSERT([_findString isEqualToString:string]);
        [self _focusOnSearchResultWithOptions:options];
    }];
}

- (void)web_hideFindUI
{
    [self _resetFind];
}

- (UIView *)web_contentView
{
    return _hostViewController ? [_hostViewController view] : self;
}

- (void)web_scrollViewDidScroll:(UIScrollView *)scrollView
{
    [_hostViewController updatePDFViewLayout];
}

- (void)web_scrollViewWillBeginZooming:(UIScrollView *)scrollView withView:(UIView *)view
{
    [_hostViewController updatePDFViewLayout];
}

- (void)web_scrollViewDidEndZooming:(UIScrollView *)scrollView withView:(UIView *)view atScale:(CGFloat)scale
{
    [_hostViewController updatePDFViewLayout];
}

- (void)web_scrollViewDidZoom:(UIScrollView *)scrollView
{
    [_hostViewController updatePDFViewLayout];
}

- (NSData *)web_dataRepresentation
{
    return _data.get();
}

- (NSString *)web_suggestedFilename
{
    return _suggestedFilename.get();
}

- (BOOL)web_isBackground
{
    return self.isBackground;
}

#pragma mark PDFHostViewControllerDelegate

- (void)pdfHostViewController:(PDFHostViewController *)controller updatePageCount:(NSInteger)pageCount
{
    [self _scrollToURLFragment:[_webView URL].fragment];
}

- (void)pdfHostViewController:(PDFHostViewController *)controller findStringUpdate:(NSUInteger)numFound done:(BOOL)done
{
    // FIXME: We should stop searching once numFound exceeds _findStringMaxCount, but PDFKit doesn't
    // allow us to stop the search without also clearing the search highlights. See <rdar://problem/39546973>.
    if (!done)
        return;

    _findStringCount = numFound;
    if (auto findCompletion = std::exchange(_findCompletion, nil))
        findCompletion();
}

- (void)pdfHostViewController:(PDFHostViewController *)controller goToURL:(NSURL *)url
{
    WKWebView *webView = _webView.getAutoreleased();
    if (!webView)
        return;

    // FIXME: We'd use the real tap location if we knew it.
    WebCore::IntPoint point;
    webView->_page->navigateToPDFLinkWithSimulatedClick(url.absoluteString, point, point);
}

- (void)pdfHostViewController:(PDFHostViewController *)controller goToPageIndex:(NSInteger)pageIndex withViewFrustum:(CGRect)documentViewRect
{
    WKWebView *webView = _webView.getAutoreleased();
    if (!webView)
        return;

    NSURL *url = [NSURL URLWithString:[NSString stringWithFormat:@"#page%ld", (long)pageIndex + 1] relativeToURL:webView.URL];
    CGPoint documentPoint = documentViewRect.origin;
    CGPoint screenPoint = [self.window convertPoint:[self convertPoint:documentPoint toView:nil] toWindow:nil];
    webView->_page->navigateToPDFLinkWithSimulatedClick(url.absoluteString, WebCore::roundedIntPoint(documentPoint), WebCore::roundedIntPoint(screenPoint));
}

@end

#pragma mark _WKWebViewPrintProvider

#if !ENABLE(MINIMAL_SIMULATOR)

@interface WKPDFView (_WKWebViewPrintFormatter) <_WKWebViewPrintProvider>
@end

@implementation WKPDFView (_WKWebViewPrintFormatter)

- (NSUInteger)_wk_pageCountForPrintFormatter:(_WKWebViewPrintFormatter *)printFormatter
{
    NSUInteger pageCount = std::max<NSInteger>([_hostViewController pageCount], 0);
    if (printFormatter.snapshotFirstPage)
        return std::min<NSUInteger>(pageCount, 1);
    return pageCount;
}

- (CGPDFDocumentRef)_wk_printedDocument
{
    auto dataProvider = adoptCF(CGDataProviderCreateWithCFData((CFDataRef)_data.get()));
    return adoptCF(CGPDFDocumentCreateWithProvider(dataProvider.get())).autorelease();
}

@end

#endif // !ENABLE(MINIMAL_SIMULATOR)

#endif // ENABLE(WKPDFVIEW)
