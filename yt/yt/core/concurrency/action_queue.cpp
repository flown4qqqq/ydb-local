#include "action_queue.h"
#include "single_queue_scheduler_thread.h"
#include "fair_share_queue_scheduler_thread.h"
#include "private.h"
#include "profiling_helpers.h"
#include "system_invokers.h"

#include <yt/yt/core/actions/current_invoker.h>
#include <yt/yt/core/actions/invoker_util.h>
#include <yt/yt/core/actions/invoker_detail.h>

#include <yt/yt/core/ypath/token.h>

#include <yt/yt/core/misc/ring_queue.h>
#include <yt/yt/core/misc/shutdown.h>

#include <yt/yt/core/profiling/timing.h>

#include <library/cpp/yt/misc/tls.h>

#include <util/thread/lfqueue.h>

namespace NYT::NConcurrency {

using namespace NProfiling;
using namespace NYPath;
using namespace NYTree;

////////////////////////////////////////////////////////////////////////////////

class TActionQueue::TImpl
    : public TRefCounted
{
public:
    explicit TImpl(const TString& threadName)
        : Queue_(New<TMpscInvokerQueue>(
            CallbackEventCount_,
            GetThreadTags(threadName)))
        , Invoker_(Queue_)
        , Thread_(New<TMpscSingleQueueSchedulerThread>(
            Queue_,
            CallbackEventCount_,
            threadName,
            threadName))
        , ShutdownCookie_(RegisterShutdownCallback(
            Format("ActionQueue(%v)", threadName),
            BIND_NO_PROPAGATE(&TImpl::Shutdown, MakeWeak(this), /*graceful*/ false),
            /*priority*/ 100))
    { }

    ~TImpl()
    {
        Shutdown(/*graceful*/ false);
    }

    void Shutdown(bool graceful)
    {
        // Proper synchronization done via Queue_->Shutdown().
        if (Stopped_.exchange(true, std::memory_order::relaxed)) {
            return;
        }

        Queue_->Shutdown(graceful);
        Thread_->Stop(graceful);
        Queue_->OnConsumerFinished();
    }

    const IInvokerPtr& GetInvoker()
    {
        EnsureStarted();
        return Invoker_;
    }

private:
    const TIntrusivePtr<NThreading::TEventCount> CallbackEventCount_ = New<NThreading::TEventCount>();
    const TMpscInvokerQueuePtr Queue_;
    const IInvokerPtr Invoker_;
    const TMpscSingleQueueSchedulerThreadPtr Thread_;

    const TShutdownCookie ShutdownCookie_;

    std::atomic<bool> Stopped_ = false;


    void EnsureStarted()
    {
        // Thread::Start already has
        // its own short-circ mechanism.
        Thread_->Start();
    }
};

TActionQueue::TActionQueue(TString threadName)
    : Impl_(New<TImpl>(std::move(threadName)))
{ }

TActionQueue::~TActionQueue() = default;

void TActionQueue::Shutdown(bool graceful)
{
    return Impl_->Shutdown(graceful);
}

const IInvokerPtr& TActionQueue::GetInvoker()
{
    return Impl_->GetInvoker();
}

////////////////////////////////////////////////////////////////////////////////

class TSerializedInvoker
    : public TInvokerWrapper<false>
    , public TInvokerProfilingWrapper
{
public:
    explicit TSerializedInvoker(
        IInvokerPtr underlyingInvoker)
        : TInvokerWrapper(std::move(underlyingInvoker))
    { }

    TSerializedInvoker(
        IInvokerPtr underlyingInvoker,
        const NProfiling::TTagSet& tagSet,
        NProfiling::IRegistryPtr registry)
        : TInvokerWrapper(std::move(underlyingInvoker))
        , TInvokerProfilingWrapper(std::move(registry), "/serialized", tagSet)
    { }

    using TInvokerWrapper::Invoke;

    void Invoke(TClosure callback) override
    {
        auto wrappedCallback = WrapCallback(std::move(callback));

        auto guard = Guard(Lock_);
        if (Dead_) {
            return;
        }
        Queue_.push(std::move(wrappedCallback));
        TrySchedule(std::move(guard));
    }

    bool IsSerialized() const override
    {
        return true;
    }

private:
    YT_DECLARE_SPIN_LOCK(NThreading::TSpinLock, Lock_);
    TRingQueue<TClosure> Queue_;
    bool CallbackScheduled_ = false;
    bool Dead_ = false;


    class TInvocationGuard
    {
    public:
        explicit TInvocationGuard(TIntrusivePtr<TSerializedInvoker> owner)
            : Owner_(std::move(owner))
        { }

        TInvocationGuard(TInvocationGuard&& other) = default;
        TInvocationGuard(const TInvocationGuard& other) = delete;

        void Activate()
        {
            YT_ASSERT(!Activated_);
            Activated_ = true;
        }

        void Reset()
        {
            Owner_.Reset();
        }

        ~TInvocationGuard()
        {
            if (Owner_) {
                Owner_->OnFinished(Activated_);
            }
        }

    private:
        TIntrusivePtr<TSerializedInvoker> Owner_;
        bool Activated_ = false;
    };

    void TrySchedule(TGuard<NThreading::TSpinLock>&& guard)
    {
        if (std::exchange(CallbackScheduled_, true)) {
            return;
        }
        guard.Release();
        UnderlyingInvoker_->Invoke(BIND_NO_PROPAGATE(
            &TSerializedInvoker::RunCallback,
            MakeStrong(this),
            Passed(TInvocationGuard(this))));
    }

    void DrainQueue(TGuard<NThreading::TSpinLock>&& guard)
    {
        std::vector<TClosure> callbacks;
        while (!Queue_.empty()) {
            callbacks.push_back(std::move(Queue_.front()));
            Queue_.pop();
        }
        guard.Release();
        callbacks.clear();
    }

    void RunCallback(TInvocationGuard invocationGuard)
    {
        invocationGuard.Activate();

        TCurrentInvokerGuard currentInvokerGuard(this);
        TOneShotContextSwitchGuard contextSwitchGuard([&] {
            invocationGuard.Reset();
            OnFinished(true);
        });

        auto lockGuard = Guard(Lock_);

        if (Queue_.empty()) {
            return;
        }

        auto callback = std::move(Queue_.front());
        Queue_.pop();

        lockGuard.Release();

        callback();
    }

    void OnFinished(bool activated)
    {
        auto guard = Guard(Lock_);

        YT_VERIFY(std::exchange(CallbackScheduled_, false));

        if (activated) {
            if (!Queue_.empty()) {
                TrySchedule(std::move(guard));
            }
        } else {
            Dead_ = true;
            DrainQueue(std::move(guard));
        }
    }
};

IInvokerPtr CreateSerializedInvoker(
    IInvokerPtr underlyingInvoker)
{
    if (underlyingInvoker->IsSerialized()) {
        return underlyingInvoker;
    }

    return New<TSerializedInvoker>(
        std::move(underlyingInvoker));
}

IInvokerPtr CreateSerializedInvoker(
    IInvokerPtr underlyingInvoker,
    const NProfiling::TTagSet& tagSet,
    NProfiling::IRegistryPtr registry)
{
    if (underlyingInvoker->IsSerialized()) {
        return underlyingInvoker;
    }

    return New<TSerializedInvoker>(
        std::move(underlyingInvoker),
        tagSet,
        std::move(registry));
}

IInvokerPtr CreateSerializedInvoker(
    IInvokerPtr underlyingInvoker,
    const std::string& invokerName,
    NProfiling::IRegistryPtr registry)
{
    return CreateSerializedInvoker(
        std::move(underlyingInvoker),
        NProfiling::TTagSet({{"invoker", invokerName}}),
        std::move(registry));
}

////////////////////////////////////////////////////////////////////////////////

class TPrioritizedInvoker
    : public TInvokerWrapper<true>
    , public TInvokerProfilingWrapper
    , public virtual IPrioritizedInvoker
{
public:
    explicit TPrioritizedInvoker(IInvokerPtr underlyingInvoker)
        : TInvokerWrapper(std::move(underlyingInvoker))
    { }

    TPrioritizedInvoker(IInvokerPtr underlyingInvoker, const NProfiling::TTagSet& tagSet, NProfiling::IRegistryPtr registry)
        : TInvokerWrapper(std::move(underlyingInvoker))
        , TInvokerProfilingWrapper(std::move(registry), "/prioritized", tagSet)
    { }

    using TInvokerWrapper::Invoke;

    void Invoke(TClosure callback) override
    {
        auto wrappedCallback = WrapCallback(std::move(callback));
        UnderlyingInvoker_->Invoke(std::move(wrappedCallback));
    }

    void Invoke(TClosure callback, i64 priority) override
    {
        {
            auto guard = Guard(SpinLock_);
            Heap_.push_back({std::move(callback), priority, Counter_--});
            std::push_heap(Heap_.begin(), Heap_.end());
        }
        auto wrappedCallback = WrapCallback(BIND_NO_PROPAGATE(&TPrioritizedInvoker::DoExecute, MakeStrong(this)));
        UnderlyingInvoker_->Invoke(std::move(wrappedCallback));
    }

private:
    struct TEntry
    {
        TClosure Callback;
        i64 Priority;
        i64 Index;

        bool operator < (const TEntry& other) const
        {
            return std::tie(Priority, Index) < std::tie(other.Priority, other.Index);
        }
    };

    YT_DECLARE_SPIN_LOCK(NThreading::TSpinLock, SpinLock_);
    std::vector<TEntry> Heap_;
    i64 Counter_ = 0;

    void DoExecute()
    {
        auto guard = Guard(SpinLock_);
        std::pop_heap(Heap_.begin(), Heap_.end());
        auto callback = std::move(Heap_.back().Callback);
        Heap_.pop_back();
        guard.Release();
        callback();
    }
};

IPrioritizedInvokerPtr CreatePrioritizedInvoker(
    IInvokerPtr underlyingInvoker)
{
    return New<TPrioritizedInvoker>(
        std::move(underlyingInvoker));
}

IPrioritizedInvokerPtr CreatePrioritizedInvoker(
    IInvokerPtr underlyingInvoker,
    const NProfiling::TTagSet& tagSet,
    NProfiling::IRegistryPtr registry)
{
    return New<TPrioritizedInvoker>(
        std::move(underlyingInvoker),
        std::move(tagSet),
        std::move(registry));
}

IPrioritizedInvokerPtr CreatePrioritizedInvoker(
    IInvokerPtr underlyingInvoker,
    const std::string& invokerName,
    NProfiling::IRegistryPtr registry)
{
    return CreatePrioritizedInvoker(
        std::move(underlyingInvoker),
        NProfiling::TTagSet({{"invoker", invokerName}}),
        std::move(registry));
}

////////////////////////////////////////////////////////////////////////////////

class TFakePrioritizedInvoker
    : public TInvokerWrapper<true>
    , public virtual IPrioritizedInvoker
{
public:
    explicit TFakePrioritizedInvoker(IInvokerPtr underlyingInvoker)
        : TInvokerWrapper(std::move(underlyingInvoker))
    { }

    using TInvokerWrapper::Invoke;

    void Invoke(TClosure callback, i64 /*priority*/) override
    {
        Invoke(std::move(callback));
    }

    void Invoke(TClosure callback) override
    {
        UnderlyingInvoker_->Invoke(std::move(callback));
    }
};

IPrioritizedInvokerPtr CreateFakePrioritizedInvoker(IInvokerPtr underlyingInvoker)
{
    return New<TFakePrioritizedInvoker>(std::move(underlyingInvoker));
}

////////////////////////////////////////////////////////////////////////////////

class TFixedPriorityInvoker
    : public TInvokerWrapper<false>
{
public:
    TFixedPriorityInvoker(
        IPrioritizedInvokerPtr underlyingInvoker,
        i64 priority)
        : TInvokerWrapper(underlyingInvoker)
        , UnderlyingInvoker_(std::move(underlyingInvoker))
        , Priority_(priority)
    { }

    using TInvokerWrapper::Invoke;

    void Invoke(TClosure callback) override
    {
        return UnderlyingInvoker_->Invoke(std::move(callback), Priority_);
    }

private:
    const IPrioritizedInvokerPtr UnderlyingInvoker_;
    const i64 Priority_;
};

IInvokerPtr CreateFixedPriorityInvoker(
    IPrioritizedInvokerPtr underlyingInvoker,
    i64 priority)
{
    return New<TFixedPriorityInvoker>(
        std::move(underlyingInvoker),
        priority);
}

////////////////////////////////////////////////////////////////////////////////

class TBoundedConcurrencyInvoker;

YT_DEFINE_THREAD_LOCAL(TBoundedConcurrencyInvoker*, CurrentBoundedConcurrencyInvoker);

class TBoundedConcurrencyInvoker
    : public IBoundedConcurrencyInvoker
    , public TInvokerWrapper<true>
{
public:
    TBoundedConcurrencyInvoker(
        IInvokerPtr underlyingInvoker,
        int maxConcurrentInvocations)
        : TInvokerWrapper(std::move(underlyingInvoker))
        , MaxConcurrentInvocations_(maxConcurrentInvocations)
    { }

    using TInvokerWrapper::Invoke;

    void Invoke(TClosure callback) override
    {
        auto guard = Guard(SpinLock_);
        if (Semaphore_ < MaxConcurrentInvocations_ && !PendingMaxConcurrentInvocations_.has_value()) {
            YT_VERIFY(Queue_.empty());
            IncrementSemaphore(+1);
            guard.Release();
            RunCallback(std::move(callback));
        } else {
            Queue_.push(std::move(callback));
        }
    }

    void SetMaxConcurrentInvocations(int newMaxConcurrentInvocations) override
    {
        // XXX(apachee): Check that newMaxConcurrentInvocations >= 0? Verify? If condition with throw?

        auto guard = Guard(SpinLock_);

        if (newMaxConcurrentInvocations == MaxConcurrentInvocations_) {
            return;
        }

        if (newMaxConcurrentInvocations >= Semaphore_) {
            i64 diff = newMaxConcurrentInvocations - Semaphore_;
            i64 numberOfCallbacksToRun = std::min(diff, std::ssize(Queue_));
            if (numberOfCallbacksToRun == 0) {
                // Fast path.

                PendingMaxConcurrentInvocations_ = {};
                MaxConcurrentInvocations_ = newMaxConcurrentInvocations;
            } else {
                // Slow path.

                std::vector<TClosure> callbacksToRun;
                callbacksToRun.reserve(numberOfCallbacksToRun);

                for (int i = 0; i < numberOfCallbacksToRun; i++) {
                    YT_ASSERT(!Queue_.empty());
                    callbacksToRun.push_back(std::move(Queue_.front()));
                    Queue_.pop();
                }

                PendingMaxConcurrentInvocations_ = {};
                MaxConcurrentInvocations_ = newMaxConcurrentInvocations;
                IncrementSemaphore(numberOfCallbacksToRun);

                guard.Release();
                for (auto& callback : callbacksToRun) {
                    RunCallback(std::move(callback));
                }
            }
        } else /* newMaxConcurrentInvocations < Semaphore_ */ {
            // NB(apachee): We have to wait for some of the callbacks to finish before updating MaxConcurrentInvocations_.
            PendingMaxConcurrentInvocations_ = newMaxConcurrentInvocations;
        }
    }

private:
    YT_DECLARE_SPIN_LOCK(NThreading::TSpinLock, SpinLock_);
    // If set, it is the next value of MaxConcurrentInvocations_.
    // Used only when decrease of MaxConcurrentInvocations_ value is requested.
    std::optional<int> PendingMaxConcurrentInvocations_;
    int MaxConcurrentInvocations_;
    TRingQueue<TClosure> Queue_;
    int Semaphore_ = 0;

private:
    class TInvocationGuard
    {
    public:
        explicit TInvocationGuard(TIntrusivePtr<TBoundedConcurrencyInvoker> owner)
            : Owner_(std::move(owner))
        { }

        TInvocationGuard(TInvocationGuard&& other) = default;
        TInvocationGuard(const TInvocationGuard& other) = delete;

        ~TInvocationGuard()
        {
            if (Owner_) {
                Owner_->OnFinished();
            }
        }

    private:
        TIntrusivePtr<TBoundedConcurrencyInvoker> Owner_;
    };

    void IncrementSemaphore(int delta)
    {
        YT_ASSERT_SPINLOCK_AFFINITY(SpinLock_);

        Semaphore_ += delta;
        YT_ASSERT(Semaphore_ >= 0 && Semaphore_ <= MaxConcurrentInvocations_ && (!PendingMaxConcurrentInvocations_.has_value() || delta <= 0));

        if (PendingMaxConcurrentInvocations_.has_value() && Semaphore_ <= *PendingMaxConcurrentInvocations_) {
            MaxConcurrentInvocations_ = *PendingMaxConcurrentInvocations_;
            PendingMaxConcurrentInvocations_ = {};
        }
    }

    void RunCallback(TClosure callback)
    {
        // If UnderlyingInvoker_ is already terminated, Invoke may drop the guard right away.
        // Protect by setting CurrentSchedulingInvoker_ and checking it on entering ScheduleMore.
        CurrentBoundedConcurrencyInvoker() = this;

        UnderlyingInvoker_->Invoke(BIND_NO_PROPAGATE(
            &TBoundedConcurrencyInvoker::DoRunCallback,
            MakeStrong(this),
            std::move(callback),
            Passed(TInvocationGuard(this))));

        // Don't leave a dangling pointer behind.
        CurrentBoundedConcurrencyInvoker() = nullptr;
    }

    void DoRunCallback(const TClosure& callback, TInvocationGuard /*invocationGuard*/)
    {
        TCurrentInvokerGuard guard(UnderlyingInvoker_.Get()); // sic!
        callback();
    }

    void OnFinished()
    {
        auto guard = Guard(SpinLock_);
        // See RunCallback.
        if (Queue_.empty() || CurrentBoundedConcurrencyInvoker() == this || PendingMaxConcurrentInvocations_.has_value()) {
            IncrementSemaphore(-1);
        } else {
            auto callback = std::move(Queue_.front());
            Queue_.pop();
            guard.Release();
            RunCallback(std::move(callback));
        }
    }
};

IBoundedConcurrencyInvokerPtr CreateBoundedConcurrencyInvoker(
    IInvokerPtr underlyingInvoker,
    int maxConcurrentInvocations)
{
    return New<TBoundedConcurrencyInvoker>(
        std::move(underlyingInvoker),
        maxConcurrentInvocations);
}

////////////////////////////////////////////////////////////////////////////////

class TSuspendableInvoker
    : public TInvokerWrapper<true>
    , public virtual ISuspendableInvoker
{
public:
    explicit TSuspendableInvoker(IInvokerPtr underlyingInvoker)
        : TInvokerWrapper(std::move(underlyingInvoker))
    { }

    void Invoke(TClosure callback) override
    {
        Queue_.Enqueue(std::move(callback));
        ScheduleMore();
    }

    void Invoke(TMutableRange<TClosure> callbacks) override
    {
        Queue_.EnqueueAll(std::move(callbacks));
        ScheduleMore();
    }

    TFuture<void> Suspend() override
    {
        YT_VERIFY(!Suspended_.exchange(true));
        {
            auto guard = Guard(SpinLock_);
            FreeEvent_ = NewPromise<void>();
            if (ActiveInvocationCount_ == 0) {
                FreeEvent_.Set();
            }
        }
        return FreeEvent_;
    }

    void Resume() override
    {
        YT_VERIFY(Suspended_.exchange(false));
        {
            auto guard = Guard(SpinLock_);
            FreeEvent_.Reset();
        }
        ScheduleMore();
    }

    bool IsSuspended() override
    {
        return Suspended_;
    }

private:
    std::atomic<bool> Suspended_ = false;
    std::atomic<bool> SchedulingMore_ = false;
    std::atomic<int> ActiveInvocationCount_ = 0;

    YT_DECLARE_SPIN_LOCK(NThreading::TSpinLock, SpinLock_);

    TLockFreeQueue<TClosure> Queue_;

    TPromise<void> FreeEvent_;

    // TODO(acid): Think how to merge this class with implementation in other invokers.
    class TInvocationGuard
    {
    public:
        explicit TInvocationGuard(TIntrusivePtr<TSuspendableInvoker> owner)
            : Owner_(std::move(owner))
        { }

        TInvocationGuard(TInvocationGuard&& other) = default;
        TInvocationGuard(const TInvocationGuard& other) = delete;

        void Reset()
        {
            Owner_.Reset();
        }

        ~TInvocationGuard()
        {
            if (Owner_) {
                Owner_->OnFinished();
            }
        }

    private:
        TIntrusivePtr<TSuspendableInvoker> Owner_;
    };


    void RunCallback(TClosure callback, TInvocationGuard invocationGuard)
    {
        TCurrentInvokerGuard currentInvokerGuard(this);
        TOneShotContextSwitchGuard contextSwitchGuard([&] {
            invocationGuard.Reset();
            OnFinished();
        });
        callback();
    }

    void OnFinished()
    {
        YT_VERIFY(ActiveInvocationCount_ > 0);

        if (--ActiveInvocationCount_ == 0 && Suspended_) {
            auto guard = Guard(SpinLock_);
            if (FreeEvent_ && !FreeEvent_.IsSet()) {
                auto freeEvent = FreeEvent_;
                guard.Release();
                freeEvent.Set();
            }
        }
    }

    void ScheduleMore()
    {
        if (Suspended_ || SchedulingMore_.exchange(true)) {
            return;
        }

        while (!Suspended_) {
            ++ActiveInvocationCount_;
            TInvocationGuard guard(this);

            TClosure callback;
            if (Suspended_ || !Queue_.Dequeue(&callback)) {
                break;
            }

            UnderlyingInvoker_->Invoke(BIND_NO_PROPAGATE(
                &TSuspendableInvoker::RunCallback,
                MakeStrong(this),
                Passed(std::move(callback)),
                Passed(std::move(guard))));
        }

        SchedulingMore_ = false;
        if (!Queue_.IsEmpty()) {
            ScheduleMore();
        }
    }
};

ISuspendableInvokerPtr CreateSuspendableInvoker(IInvokerPtr underlyingInvoker)
{
    return New<TSuspendableInvoker>(std::move(underlyingInvoker));
}

////////////////////////////////////////////////////////////////////////////////

class TWatchdogInvoker
    : public TInvokerWrapper<false>
{
public:
    TWatchdogInvoker(
        IInvokerPtr invoker,
        const NLogging::TLogger& logger,
        TDuration threshold)
        : TInvokerWrapper(std::move(invoker))
        , Logger(logger)
        , Threshold_(DurationToCpuDuration(threshold))
    { }

    using TInvokerWrapper::Invoke;

    void Invoke(TClosure callback) override
    {
        UnderlyingInvoker_->Invoke(BIND_NO_PROPAGATE(
            &TWatchdogInvoker::RunCallback,
            MakeStrong(this),
            Passed(std::move(callback))));
    }

private:
    const NLogging::TLogger Logger;
    const TCpuDuration Threshold_;

    void RunCallback(TClosure callback)
    {
        TCurrentInvokerGuard currentInvokerGuard(this);
        TFiberSliceTimer fiberSliceTimer(Threshold_, [&, this] (TCpuDuration execution) {
            YT_LOG_WARNING("Callback executed for too long without interruptions (Callback: %v, Execution: %v)",
                callback.GetHandle(),
                CpuDurationToDuration(execution));
        });
        callback();
    }
};

IInvokerPtr CreateWatchdogInvoker(
    IInvokerPtr underlyingInvoker,
    const NLogging::TLogger& logger,
    TDuration threshold)
{
    return New<TWatchdogInvoker>(std::move(underlyingInvoker), logger, threshold);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NConcurrency
