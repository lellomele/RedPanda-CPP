// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SEMANTICMEMORY_H
#define SEMANTICMEMORY_H
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <new>
#include <utility>

namespace Analysis {
enum class SemanticMemoryKind { Payload, Projection, Scratch };
struct SemanticMemoryUsage {
    std::size_t limit = 0;
    std::size_t reserved = 0;
    std::size_t live = 0; // Includes old objects still pinned by a reader.
    std::size_t scratch = 0;
    std::size_t retired = 0; // Not credited until actual destruction/deallocation.
    std::size_t peakAdmitted = 0;
    std::size_t admitted() const { return reserved + live + scratch + retired; }
};
class SemanticBudgetExceeded final : public std::bad_alloc {
public:
    const char *what() const noexcept override { return "Semantic memory budget exceeded"; }
};
struct SemanticLedgerState;
struct SemanticReservationState;
class SemanticMemoryResource;
class SemanticBuildReservation {
public:
    SemanticBuildReservation() = default;
    SemanticBuildReservation(SemanticBuildReservation &&) noexcept;
    SemanticBuildReservation &operator=(SemanticBuildReservation &&) noexcept;
    SemanticBuildReservation(const SemanticBuildReservation &) = delete;
    SemanticBuildReservation &operator=(const SemanticBuildReservation &) = delete;
    ~SemanticBuildReservation();
    explicit operator bool() const { return bool(mState); }
    void close() noexcept;
    std::shared_ptr<SemanticMemoryResource> resource(SemanticMemoryKind kind);
private:
    friend class SemanticMemoryLedger;
    explicit SemanticBuildReservation(std::shared_ptr<SemanticReservationState> state);
    std::shared_ptr<SemanticReservationState> mState;
};
class SemanticMemoryLedger {
public:
    // Charges requested PMR/control-block capacities plus conservative fixed
    // bookkeeping allowances. This is an aggregate semantic admission cap,
    // not a bound on the parser/Qt/OS working set or upstream allocator headers.
    explicit SemanticMemoryLedger(std::size_t maximumBytes);
    SemanticBuildReservation tryReserve(std::size_t initialBytes = 0) const;
    SemanticMemoryUsage usage() const;
private:
    std::shared_ptr<SemanticLedgerState> mState;
};
class SemanticMemoryResource final : public std::pmr::memory_resource {
public:
    ~SemanticMemoryResource() override;
    void markRetired() noexcept;
    std::size_t chargedBytes() const noexcept;
private:
    friend class SemanticBuildReservation;
    SemanticMemoryResource(std::shared_ptr<SemanticReservationState>, SemanticMemoryKind);
    void *do_allocate(std::size_t, std::size_t) override;
    void do_deallocate(void *, std::size_t, std::size_t) override;
    bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override { return this == &other; }
    std::shared_ptr<SemanticReservationState> mReservation;
    SemanticMemoryKind mKind;
    std::size_t mBytes = 0;
    bool mRetired = false;
};
// The root supplies one structural scheduler, not a thread/task per drop.
// retire() must only enqueue this pre-existing intrusive record, without waiting
// for the GUI/engine or allocating another large payload. The scheduler owns it
// until reclaim() is called off the GUI. Capacity remains charged meanwhile.
struct RetiredSemanticAllocation {
    RetiredSemanticAllocation *nextRetired = nullptr;
    virtual void reclaim() noexcept = 0;
protected:
    virtual ~RetiredSemanticAllocation() = default;
};
class SemanticReclamationScheduler {
public:
    virtual ~SemanticReclamationScheduler() = default;
    // Admission happens before payload allocation. Once admitted, retirement
    // must always be accepted, including while the service is stopping.
    // Defaults retain compatibility with deterministic/manual reclaimers.
    virtual bool tryRegisterAllocation() noexcept { return true; }
    virtual void allocationReclaimed() noexcept {}
    virtual void retire(RetiredSemanticAllocation *) noexcept = 0;
};
// Control-block storage is accounted by the same resource as the payload.
template<class T> class SemanticAllocator {
public:
    using value_type = T;
    explicit SemanticAllocator(std::shared_ptr<SemanticMemoryResource> resource): resource(std::move(resource)) {}
    template<class U> SemanticAllocator(const SemanticAllocator<U> &other): resource(other.resource) {}
    T *allocate(std::size_t count) {
        if (count > std::size_t(-1) / sizeof(T)) throw SemanticBudgetExceeded();
        return static_cast<T *>(resource->allocate(count * sizeof(T), alignof(T)));
    }
    void deallocate(T *pointer, std::size_t count) { resource->deallocate(pointer, count * sizeof(T), alignof(T)); }
    template<class U> bool operator==(const SemanticAllocator<U> &other) const { return resource == other.resource; }
    template<class U> bool operator!=(const SemanticAllocator<U> &other) const { return !(*this == other); }
    std::shared_ptr<SemanticMemoryResource> resource;
};
namespace Detail {
template<class T> struct SemanticOwner final : RetiredSemanticAllocation {
    template<class... Args> SemanticOwner(std::shared_ptr<SemanticMemoryResource> memory,
            std::shared_ptr<SemanticReclamationScheduler> scheduler, Args &&...args):
        memory(std::move(memory)), scheduler(std::move(scheduler)), value(std::forward<Args>(args)...) {}
    void reclaim() noexcept override {
        auto keepMemory = memory;
        auto keepScheduler = scheduler;
        this->~SemanticOwner();
        keepMemory->deallocate(this, sizeof(SemanticOwner), alignof(SemanticOwner));
        keepMemory.reset();
        keepScheduler->allocationReclaimed();
    }
    std::shared_ptr<SemanticMemoryResource> memory;
    std::shared_ptr<SemanticReclamationScheduler> scheduler;
    T value;
};
template<class T, class... Args> std::shared_ptr<T> makeSemanticOwner(
        const std::shared_ptr<SemanticMemoryResource> &memory,
        const std::shared_ptr<SemanticReclamationScheduler> &scheduler, Args &&...args)
{
    using Owner = SemanticOwner<T>;
    if (!scheduler || !scheduler->tryRegisterAllocation()) throw SemanticBudgetExceeded();
    struct Registration {
        SemanticReclamationScheduler *scheduler;
        bool active = true;
        ~Registration() { if (active) scheduler->allocationReclaimed(); }
    } registration{scheduler.get()};
    void *storage = memory->allocate(sizeof(Owner), alignof(Owner));
    Owner *owner = nullptr;
    try { owner = new(storage) Owner(memory, scheduler, std::forward<Args>(args)...); }
    catch (...) { memory->deallocate(storage, sizeof(Owner), alignof(Owner)); throw; }
    // From here the owner owns the registration. If allocating the shared_ptr
    // control block fails, shared_ptr invokes the deleter and retires it.
    registration.active = false;
    std::shared_ptr<Owner> ownership(owner, [scheduler](Owner *retired) noexcept {
        retired->memory->markRetired();
        scheduler->retire(retired);
    }, SemanticAllocator<Owner>(memory));
    return std::shared_ptr<T>(std::move(ownership), &owner->value);
}
} // namespace Detail
} // namespace Analysis
#endif
