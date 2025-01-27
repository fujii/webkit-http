/*
 *  Copyright (C) 1999-2001 Harri Porten (porten@kde.org)
 *  Copyright (C) 2001 Peter Kelly (pmk@post.com)
 *  Copyright (C) 2006-2018 Apple Inc. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#pragma once

#include "JSWindowProxy.h"
#include <JavaScriptCore/Strong.h>
#include <wtf/HashMap.h>

namespace JSC {
class Debugger;
}

namespace WebCore {

class AbstractFrame;

class WindowProxy {
    WTF_MAKE_FAST_ALLOCATED;
public:
    using ProxyMap = HashMap<RefPtr<DOMWrapperWorld>, JSC::Strong<JSWindowProxy>>;

    explicit WindowProxy(AbstractFrame&);
    ~WindowProxy();

    void destroyJSWindowProxy(DOMWrapperWorld&);

    ProxyMap::ValuesConstIteratorRange jsWindowProxies() const { return m_jsWindowProxies.values(); }
    Vector<JSC::Strong<JSWindowProxy>> jsWindowProxiesAsVector() const;

    ProxyMap releaseJSWindowProxies() { return std::exchange(m_jsWindowProxies, ProxyMap()); }
    void setJSWindowProxies(ProxyMap&& windowProxies) { m_jsWindowProxies = WTFMove(windowProxies); }

    JSWindowProxy& jsWindowProxy(DOMWrapperWorld& world)
    {
        auto it = m_jsWindowProxies.find(&world);
        if (it != m_jsWindowProxies.end())
            return *it->value.get();

        return createJSWindowProxyWithInitializedScript(world);
    }

    JSWindowProxy* existingJSWindowProxy(DOMWrapperWorld& world) const
    {
        auto it = m_jsWindowProxies.find(&world);
        return (it != m_jsWindowProxies.end()) ? it->value.get() : nullptr;
    }

    JSDOMGlobalObject* globalObject(DOMWrapperWorld& world)
    {
        return jsWindowProxy(world).window();
    }

    void clearJSWindowProxiesNotMatchingDOMWindow(AbstractDOMWindow*, bool goingIntoPageCache);

    WEBCORE_EXPORT void setDOMWindow(AbstractDOMWindow*);

    // Debugger can be nullptr to detach any existing Debugger.
    void attachDebugger(JSC::Debugger*); // Attaches/detaches in all worlds/window proxies.

    WEBCORE_EXPORT AbstractDOMWindow* window() const;

    WEBCORE_EXPORT void ref();
    WEBCORE_EXPORT void deref();

private:
    JSWindowProxy& createJSWindowProxy(DOMWrapperWorld&);
    WEBCORE_EXPORT JSWindowProxy& createJSWindowProxyWithInitializedScript(DOMWrapperWorld&);

    AbstractFrame& m_frame;
    ProxyMap m_jsWindowProxies;
};

} // namespace WebCore
