// SPDX-License-Identifier: GPL-3.0-or-later
#include "semanticretirement.h"
#include <algorithm>
#include <condition_variable>
#include <limits>
#include <stdexcept>

namespace Analysis {
struct SemanticRetirementWorkerState {
    std::mutex mutex;
    std::condition_variable wake;
    SemanticRetirementState counts;
    std::size_t maximumOutstanding = 0;
    RetiredSemanticAllocation *first = nullptr, *last = nullptr;
    bool stopping = false, finished = false;
};
namespace {
void increment(std::uint64_t &value) noexcept
{ if (value != std::numeric_limits<std::uint64_t>::max()) ++value; }
SemanticRetirementState snapshot(const SemanticRetirementWorkerState &state) noexcept
{
    auto result = state.counts;
    result.status = state.finished ? SemanticRetirementStatus::Stopped
        : state.stopping ? SemanticRetirementStatus::StoppingDeferred : SemanticRetirementStatus::Running;
    return result;
}
void reclaimLoop(const std::shared_ptr<SemanticRetirementWorkerState> &state) noexcept
{
    std::unique_lock<std::mutex> lock(state->mutex);
    state->counts.workerThread = std::this_thread::get_id();
    for (;;) {
        state->wake.wait(lock, [&] { return state->first || (state->stopping && state->counts.outstanding == 0); });
        if (!state->first) {
            // Registrations are completed only after destruction. Recursively
            // retired children are registered already and prevent this exit.
            state->finished = true;
            lock.unlock();
            state->wake.notify_all();
            return;
        }
        auto *allocation = state->first;
        state->first = allocation->nextRetired;
        if (!state->first) state->last = nullptr;
        allocation->nextRetired = nullptr;
        --state->counts.queued;
        ++state->counts.reclaiming;
        lock.unlock();
        allocation->reclaim();
        lock.lock();
        --state->counts.reclaiming;
        increment(state->counts.reclaimed);
        state->wake.notify_all();
    }
}
} // namespace
std::shared_ptr<SemanticRetirementScheduler> SemanticRetirementScheduler::create(std::size_t maximumOutstanding)
{
    if (!maximumOutstanding) throw std::invalid_argument("Semantic retirement capacity must be positive");
    return std::shared_ptr<SemanticRetirementScheduler>(new SemanticRetirementScheduler(maximumOutstanding));
}
SemanticRetirementScheduler::SemanticRetirementScheduler(std::size_t maximumOutstanding):
    mState(std::make_shared<SemanticRetirementWorkerState>())
{
    mState->maximumOutstanding = maximumOutstanding;
    mWorker = std::thread([state = mState] { reclaimLoop(state); });
}
SemanticRetirementScheduler::~SemanticRetirementScheduler()
{
    beginStop();
    std::lock_guard<std::mutex> joinLock(mJoinMutex);
    if (!mWorker.joinable()) return;
    // Each registered SemanticOwner retains the scheduler. Ordinarily the
    // lifecycle owner joins after Stopped. If it relinquishes ownership early,
    // the final queued allocation may release this last handle on the worker.
    // Its independently owned state then finishes the same worker safely.
    if (mWorker.get_id() == std::this_thread::get_id()) mWorker.detach();
    else {
        bool finished;
        { std::lock_guard<std::mutex> lock(mState->mutex); finished = mState->finished; }
        if (finished) mWorker.join();
        else mWorker.detach(); // Safe fallback, never an acknowledged shutdown.
    }
}
bool SemanticRetirementScheduler::tryRegisterAllocation() noexcept
{
    std::lock_guard<std::mutex> lock(mState->mutex);
    if (mState->stopping) { increment(mState->counts.refusedStopping); return false; }
    if (mState->counts.outstanding >= mState->maximumOutstanding) {
        increment(mState->counts.refusedCapacity); return false;
    }
    ++mState->counts.outstanding;
    mState->counts.peakOutstanding = std::max(mState->counts.peakOutstanding, mState->counts.outstanding);
    return true;
}
void SemanticRetirementScheduler::allocationReclaimed() noexcept
{
    {
        std::lock_guard<std::mutex> lock(mState->mutex);
        // Only makeSemanticOwner registration/reclaim pairs may call this.
        // An unmatched release is a programming error, not a zero-work ack.
        if (!mState->counts.outstanding) std::terminate();
        --mState->counts.outstanding;
    }
    mState->wake.notify_all();
}
void SemanticRetirementScheduler::retire(RetiredSemanticAllocation *allocation) noexcept
{
    if (!allocation) return;
    {
        std::lock_guard<std::mutex> lock(mState->mutex);
        allocation->nextRetired = nullptr;
        if (mState->last) mState->last->nextRetired = allocation;
        else mState->first = allocation;
        mState->last = allocation;
        ++mState->counts.queued;
        mState->counts.peakQueued = std::max(mState->counts.peakQueued, mState->counts.queued);
    }
    mState->wake.notify_one();
}
void SemanticRetirementScheduler::beginStop() noexcept
{
    { std::lock_guard<std::mutex> lock(mState->mutex); mState->stopping = true; }
    mState->wake.notify_all();
}
SemanticRetirementState SemanticRetirementScheduler::state() const noexcept
{
    std::lock_guard<std::mutex> lock(mState->mutex);
    return snapshot(*mState);
}
SemanticRetirementState SemanticRetirementScheduler::waitForStopped(std::chrono::milliseconds timeout) noexcept
{
    SemanticRetirementState result;
    {
        std::unique_lock<std::mutex> lock(mState->mutex);
        if (mState->counts.workerThread != std::this_thread::get_id() && timeout.count() > 0)
            mState->wake.wait_for(lock, timeout, [&] { return mState->finished; });
        result = snapshot(*mState);
    }
    if (result.stopped()) {
        std::lock_guard<std::mutex> joinLock(mJoinMutex);
        if (mWorker.joinable() && mWorker.get_id() != std::this_thread::get_id()) mWorker.join();
    }
    return result;
}
SemanticRetirementState SemanticRetirementScheduler::shutdown(std::chrono::milliseconds timeout) noexcept
{
    beginStop();
    return waitForStopped(timeout);
}
} // namespace Analysis
