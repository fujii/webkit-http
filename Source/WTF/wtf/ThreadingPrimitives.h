/*
 * Copyright (C) 2007, 2008, 2010 Apple Inc. All rights reserved.
 * Copyright (C) 2007 Justin Haygood (jhaygood@reaktix.com)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer. 
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution. 
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef ThreadingPrimitives_h
#define ThreadingPrimitives_h

#include <wtf/FastMalloc.h>
#include <wtf/Locker.h>
#include <wtf/Noncopyable.h>
#include <wtf/WallTime.h>

#if OS(WINDOWS)
#include <windows.h>
#endif

#if USE(PTHREADS)
#include <pthread.h>
#endif

namespace WTF {

using ThreadFunction = void (*)(void* argument);

#if USE(PTHREADS)
using PlatformThreadHandle = pthread_t;
using PlatformMutex = pthread_mutex_t;
using PlatformCondition = pthread_cond_t;
#elif OS(WINDOWS)
using ThreadIdentifier = uint32_t;
using PlatformThreadHandle = HANDLE;
using PlatformMutex = SRWLOCK;
using PlatformCondition = CONDITION_VARIABLE;
#else
#error "Not supported platform"
#endif

class Mutex {
    WTF_MAKE_NONCOPYABLE(Mutex);
    WTF_MAKE_FAST_ALLOCATED;
public:
    WTF_EXPORT_PRIVATE Mutex();
    WTF_EXPORT_PRIVATE ~Mutex();

    WTF_EXPORT_PRIVATE void lock();
    WTF_EXPORT_PRIVATE bool tryLock();
    WTF_EXPORT_PRIVATE void unlock();

    PlatformMutex& impl() { return m_mutex; }

private:
    PlatformMutex m_mutex;
};

typedef Locker<Mutex> MutexLocker;

class ThreadCondition {
    WTF_MAKE_NONCOPYABLE(ThreadCondition);
    WTF_MAKE_FAST_ALLOCATED;
public:
    WTF_EXPORT_PRIVATE ThreadCondition();
    WTF_EXPORT_PRIVATE ~ThreadCondition();
    
    WTF_EXPORT_PRIVATE void wait(Mutex& mutex);
    // Returns true if the condition was signaled before absoluteTime, false if the absoluteTime was reached or is in the past.
    WTF_EXPORT_PRIVATE bool timedWait(Mutex&, WallTime absoluteTime);
    WTF_EXPORT_PRIVATE void signal();
    WTF_EXPORT_PRIVATE void broadcast();
    
private:
    PlatformCondition m_condition;
};

} // namespace WTF

using WTF::Mutex;
using WTF::MutexLocker;
using WTF::ThreadCondition;

#endif // ThreadingPrimitives_h
