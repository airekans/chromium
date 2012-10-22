// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CCSchedulerTestCommon_h
#define CCSchedulerTestCommon_h

#include "CCDelayBasedTimeSource.h"
#include "CCFrameRateController.h"
#include "base/threading/platform_thread.h"
#include "cc/thread.h"
#include "testing/gtest/include/gtest/gtest.h"
#include <wtf/OwnPtr.h>

namespace WebKitTests {

class FakeTimeSourceClient : public cc::TimeSourceClient {
public:
    FakeTimeSourceClient() { reset(); }
    void reset() { m_tickCalled = false; }
    bool tickCalled() const { return m_tickCalled; }

    virtual void onTimerTick() OVERRIDE;

protected:
    bool m_tickCalled;
};

class FakeThread : public cc::Thread {
public:
    FakeThread();
    virtual ~FakeThread();

    void reset()
    {
        m_pendingTaskDelay = 0;
        m_pendingTask.clear();
        m_runPendingTaskOnOverwrite = false;
    }

    void runPendingTaskOnOverwrite(bool enable)
    {
        m_runPendingTaskOnOverwrite = enable;
    }

    bool hasPendingTask() const { return m_pendingTask; }
    void runPendingTask()
    {
        ASSERT_TRUE(m_pendingTask);
        OwnPtr<Task> task = m_pendingTask.release();
        task->performTask();
    }

    long long pendingDelayMs() const
    {
        EXPECT_TRUE(hasPendingTask());
        return m_pendingTaskDelay;
    }

    virtual void postTask(PassOwnPtr<Task>) OVERRIDE;
    virtual void postDelayedTask(PassOwnPtr<Task> task, long long delay) OVERRIDE;
    virtual base::PlatformThreadId threadID() const OVERRIDE;

protected:
    OwnPtr<Task> m_pendingTask;
    long long m_pendingTaskDelay;
    bool m_runPendingTaskOnOverwrite;
};

class FakeTimeSource : public cc::TimeSource {
public:
    FakeTimeSource()
        : m_active(false)
        , m_client(0)
    {
    }

    virtual void setClient(cc::TimeSourceClient* client) OVERRIDE;
    virtual void setActive(bool b) OVERRIDE;
    virtual bool active() const OVERRIDE;
    virtual void setTimebaseAndInterval(base::TimeTicks timebase, base::TimeDelta interval) OVERRIDE { }
    virtual base::TimeTicks lastTickTime() OVERRIDE;
    virtual base::TimeTicks nextTickTime() OVERRIDE;

    void tick()
    {
        ASSERT_TRUE(m_active);
        if (m_client)
            m_client->onTimerTick();
    }

    void setNextTickTime(base::TimeTicks nextTickTime) { m_nextTickTime = nextTickTime; }

protected:
    virtual ~FakeTimeSource() { }

    bool m_active;
    base::TimeTicks m_nextTickTime;
    cc::TimeSourceClient* m_client;
};

class FakeDelayBasedTimeSource : public cc::DelayBasedTimeSource {
public:
    static scoped_refptr<FakeDelayBasedTimeSource> create(base::TimeDelta interval, cc::Thread* thread)
    {
        return make_scoped_refptr(new FakeDelayBasedTimeSource(interval, thread));
    }

    void setNow(base::TimeTicks time) { m_now = time; }
    virtual base::TimeTicks now() const OVERRIDE;

protected:
    FakeDelayBasedTimeSource(base::TimeDelta interval, cc::Thread* thread)
        : DelayBasedTimeSource(interval, thread)
    {
    }
    virtual ~FakeDelayBasedTimeSource() { }

    base::TimeTicks m_now;
};

class FakeFrameRateController : public cc::FrameRateController {
public:
    FakeFrameRateController(scoped_refptr<cc::TimeSource> timer) : cc::FrameRateController(timer) { }

    int numFramesPending() const { return m_numFramesPending; }
};

}

#endif // CCSchedulerTestCommon_h
