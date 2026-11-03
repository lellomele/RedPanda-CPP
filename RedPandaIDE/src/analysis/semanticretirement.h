// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SEMANTICRETIREMENT_H
#define SEMANTICRETIREMENT_H
#include "semanticmemory.h"
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>

namespace Analysis {
enum class SemanticRetirementStatus { Running, StoppingDeferred, Stopped };
struct SemanticRetirementState {
    SemanticRetirementStatus status = SemanticRetirementStatus::Running;
    std::size_t outstanding = 0; // Includes live pins, queued and reclaiming owners.
    std::size_t queued = 0, reclaiming = 0, peakOutstanding = 0, peakQueued = 0;
    std::uint64_t reclaimed = 0, refusedCapacity = 0, refusedStopping = 0;
    std::thread::id workerThread;
    bool stopped() const noexcept { return status == SemanticRetirementStatus::Stopped; }
};
struct SemanticRetirementWorkerState;

// One stable worker per semantic service, shared by its publications/shards.
// The owner must call beginStop() and keep this handle until waitForStopped()
// acknowledges Stopped. Neither a timeout nor an empty queue proves shutdown:
// an old reader may still pin an admitted allocation. Waiting belongs on the
// lifecycle worker; timeout=0 provides a nonblocking GUI observation.
class SemanticRetirementScheduler final : public SemanticReclamationScheduler {
public:
    static std::shared_ptr<SemanticRetirementScheduler> create(std::size_t maximumOutstanding = 16384);
    ~SemanticRetirementScheduler() override;
    bool tryRegisterAllocation() noexcept override;
    void allocationReclaimed() noexcept override;
    void retire(RetiredSemanticAllocation *) noexcept override;
    void beginStop() noexcept;
    SemanticRetirementState state() const noexcept;
    SemanticRetirementState waitForStopped(std::chrono::milliseconds timeout) noexcept;
    SemanticRetirementState shutdown(std::chrono::milliseconds timeout) noexcept;
private:
    explicit SemanticRetirementScheduler(std::size_t maximumOutstanding);
    std::shared_ptr<SemanticRetirementWorkerState> mState;
    std::thread mWorker;
    std::mutex mJoinMutex;
};
} // namespace Analysis
#endif
