// SPDX-License-Identifier: GPL-3.0-or-later
#include "semanticmemory.h"
#include <algorithm>
#include <mutex>
#include <utility>

namespace Analysis {
struct SemanticLedgerState {
    mutable std::mutex mutex;
    SemanticMemoryUsage usage;
};
struct SemanticReservationState {
    std::shared_ptr<SemanticLedgerState> ledger;
    std::size_t remaining = 0;
    bool closed = false;
    bool metadataCharged = false;
    ~SemanticReservationState();
};
namespace {
// Includes the fixed resource and shared-owner bookkeeping allowance. PMR
// payload/control-block capacity is charged at the exact requested size.
constexpr std::size_t ResourceMetadata = sizeof(SemanticMemoryResource) + 128;
constexpr std::size_t ReservationMetadata = sizeof(SemanticReservationState) + 128;
std::size_t &category(SemanticMemoryUsage &usage, SemanticMemoryKind kind, bool retired)
{
    if (retired) return usage.retired;
    return kind == SemanticMemoryKind::Scratch ? usage.scratch : usage.live;
}
void charge(SemanticReservationState &reservation, std::size_t bytes,
            SemanticMemoryKind kind, bool retired)
{
    auto &usage = reservation.ledger->usage;
    if (reservation.closed) throw SemanticBudgetExceeded();
    const std::size_t credit = std::min(bytes, reservation.remaining);
    const std::size_t growth = bytes - credit;
    if (growth > usage.limit - usage.admitted()) throw SemanticBudgetExceeded();
    reservation.remaining -= credit;
    usage.reserved -= credit;
    category(usage, kind, retired) += bytes;
    usage.peakAdmitted = std::max(usage.peakAdmitted, usage.admitted());
}
} // namespace
SemanticReservationState::~SemanticReservationState()
{
    if (!metadataCharged) return;
    std::lock_guard<std::mutex> lock(ledger->mutex);
    ledger->usage.live -= ReservationMetadata;
}
SemanticMemoryLedger::SemanticMemoryLedger(std::size_t maximumBytes): mState(std::make_shared<SemanticLedgerState>())
{ mState->usage.limit = maximumBytes; }
SemanticBuildReservation SemanticMemoryLedger::tryReserve(std::size_t bytes) const
{
    auto reservation = std::make_shared<SemanticReservationState>();
    reservation->ledger = mState;
    std::lock_guard<std::mutex> lock(mState->mutex);
    auto &usage = mState->usage;
    const auto available = usage.limit - usage.admitted();
    if (available < ReservationMetadata || bytes > available - ReservationMetadata) return {};
    reservation->remaining = bytes;
    reservation->metadataCharged = true;
    usage.live += ReservationMetadata;
    usage.reserved += bytes;
    usage.peakAdmitted = std::max(usage.peakAdmitted, usage.admitted());
    return SemanticBuildReservation(std::move(reservation));
}
SemanticMemoryUsage SemanticMemoryLedger::usage() const
{
    std::lock_guard<std::mutex> lock(mState->mutex);
    return mState->usage;
}
SemanticBuildReservation::SemanticBuildReservation(std::shared_ptr<SemanticReservationState> state): mState(std::move(state)) {}
SemanticBuildReservation::SemanticBuildReservation(SemanticBuildReservation &&other) noexcept: mState(std::move(other.mState)) {}
SemanticBuildReservation &SemanticBuildReservation::operator=(SemanticBuildReservation &&other) noexcept
{
    if (this != &other) { close(); mState = std::move(other.mState); }
    return *this;
}
SemanticBuildReservation::~SemanticBuildReservation() { close(); }
void SemanticBuildReservation::close() noexcept
{
    auto state = std::move(mState);
    if (!state) return;
    std::lock_guard<std::mutex> lock(state->ledger->mutex);
    if (!state->closed) {
        state->ledger->usage.reserved -= state->remaining;
        state->remaining = 0;
        state->closed = true;
    }
}
std::shared_ptr<SemanticMemoryResource> SemanticBuildReservation::resource(SemanticMemoryKind kind)
{
    if (!mState) throw SemanticBudgetExceeded();
    {
        std::lock_guard<std::mutex> lock(mState->ledger->mutex);
        charge(*mState, ResourceMetadata, kind, false);
    }
    SemanticMemoryResource *resource = nullptr;
    try { resource = new SemanticMemoryResource(mState, kind); }
    catch (...) {
        std::lock_guard<std::mutex> lock(mState->ledger->mutex);
        category(mState->ledger->usage, kind, false) -= ResourceMetadata;
        throw;
    }
    // shared_ptr invokes delete(resource) if allocating its control block
    // fails. Its destructor then returns the metadata charge exactly once.
    return std::shared_ptr<SemanticMemoryResource>(resource);
}
SemanticMemoryResource::SemanticMemoryResource(std::shared_ptr<SemanticReservationState> reservation, SemanticMemoryKind kind):
    mReservation(std::move(reservation)), mKind(kind), mBytes(ResourceMetadata) {}
SemanticMemoryResource::~SemanticMemoryResource()
{
    std::lock_guard<std::mutex> lock(mReservation->ledger->mutex);
    category(mReservation->ledger->usage, mKind, mRetired) -= ResourceMetadata;
}
void *SemanticMemoryResource::do_allocate(std::size_t bytes, std::size_t alignment)
{
    const std::size_t charged = std::max(bytes, std::size_t(1));
    {
        std::lock_guard<std::mutex> lock(mReservation->ledger->mutex);
        if (mRetired) throw SemanticBudgetExceeded();
        charge(*mReservation, charged, mKind, false);
        mBytes += charged;
    }
    try { return std::pmr::new_delete_resource()->allocate(bytes, alignment); }
    catch (...) {
        std::lock_guard<std::mutex> lock(mReservation->ledger->mutex);
        mBytes -= charged;
        category(mReservation->ledger->usage, mKind, false) -= charged;
        throw;
    }
}
void SemanticMemoryResource::do_deallocate(void *pointer, std::size_t bytes, std::size_t alignment)
{
    std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    std::lock_guard<std::mutex> lock(mReservation->ledger->mutex);
    const std::size_t charged = std::max(bytes, std::size_t(1));
    mBytes -= charged;
    category(mReservation->ledger->usage, mKind, mRetired) -= charged;
}
void SemanticMemoryResource::markRetired() noexcept
{
    std::lock_guard<std::mutex> lock(mReservation->ledger->mutex);
    if (mRetired) return;
    auto &usage = mReservation->ledger->usage;
    category(usage, mKind, false) -= mBytes;
    usage.retired += mBytes;
    mRetired = true;
}
std::size_t SemanticMemoryResource::chargedBytes() const noexcept
{
    std::lock_guard<std::mutex> lock(mReservation->ledger->mutex);
    return mBytes;
}
} // namespace Analysis
