// SPDX-License-Identifier: GPL-3.0-or-later
#include "semanticpublication.h"
#include <algorithm>
#include <limits>

namespace Analysis {
quint64 SemanticIdentitySource::next() noexcept
{
    quint64 value = mNext.load(std::memory_order_relaxed);
    while (value != std::numeric_limits<quint64>::max()) {
        if (mNext.compare_exchange_weak(value, value + 1, std::memory_order_relaxed)) return value + 1;
    }
    return 0;
}
QStringView SemanticShard::text(SemanticTextRef ref) const noexcept
{
    if (ref.offset > mText.size() || ref.size > mText.size() - ref.offset) return {};
    if (ref.size == 0) return {};
    return QStringView(mText.data() + ref.offset, ref.size);
}
const SemanticRecord *SemanticPublication::symbol(SymbolRef ref) const noexcept
{
    if (ref.service != mIdentity.service || !ref.valid()) return nullptr;
    for (const auto &shard : mShards)
        if (shard->id() == ref.shard)
            return ref.slot < shard->records().size() ? &shard->records()[ref.slot] : nullptr;
    return nullptr;
}
const SemanticOutlineNode *SemanticPublication::node(OutlineNodeRef ref) const noexcept
{
    if (!mOutline || ref.service != mIdentity.service || ref.projection != mOutline->id()
            || ref.slot >= mOutline->nodes().size()) return nullptr;
    return &mOutline->nodes()[ref.slot];
}
QStringView SemanticPublication::text(SymbolRef ref, SemanticTextRef value) const noexcept
{
    if (ref.service != mIdentity.service) return {};
    for (const auto &shard : mShards)
        if (shard->id() == ref.shard && ref.slot < shard->records().size()) return shard->text(value);
    return {};
}
SemanticSelectionMatches SemanticPublication::exact(OutlineNodeRef ref) const noexcept
{
    SemanticSelectionMatches result;
    if (node(ref)) { result.status = SelectionResolution::Exact; result.candidates[0] = ref; result.count = 1; }
    return result;
}
SemanticSelectionMatches SemanticPublication::selection(QStringView key, quint32 maximum) const noexcept
{
    SemanticSelectionMatches result;
    if (!mOutline) return result;
    maximum = std::min(maximum, quint32(result.candidates.size()));
    const auto keyFor = [this](NodeIndex index) {
        const auto ref = mOutline->mNodes[index].symbol;
        const auto *value = symbol(ref);
        return value ? text(ref, value->selectionKey) : QStringView{};
    };
    const auto &entries = mOutline->mSelection;
    auto found = std::lower_bound(entries.begin(), entries.end(), key,
        [&](NodeIndex index, QStringView value) { return keyFor(index).compare(value) < 0; });
    quint32 matches = 0;
    for (; found != entries.end() && keyFor(*found) == key; ++found) {
        ++matches;
        if (result.count < maximum)
            result.candidates[result.count++] = {mIdentity.service, mOutline->id(), *found};
        else { result.hasMore = true; break; }
    }
    if (matches != 0) result.status = matches == 1 && !result.hasMore
        ? SelectionResolution::UniqueBestEffort : SelectionResolution::Ambiguous;
    return result;
}
} // namespace Analysis
