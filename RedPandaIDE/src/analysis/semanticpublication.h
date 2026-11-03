// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SEMANTICPUBLICATION_H
#define SEMANTICPUBLICATION_H
#include "semanticmemory.h"
#include "semanticsnapshot.h"
#include <QStringView>
#include <array>
#include <atomic>
#include <vector>

namespace Analysis {
struct PublicationIdentity {
    quint64 service = 0, job = 0, inputGeneration = 0, configurationGeneration = 0, workspaceGeneration = 0;
};
class SemanticIdentitySource {
public:
    // One source per unique service lifetime. Never recreate it with the same
    // service identity while an older result/handle may still exist.
    explicit SemanticIdentitySource(quint64 service): mService(service) {}
    quint64 service() const { return mService; }
    quint64 next() noexcept;
private:
    quint64 mService;
    std::atomic<quint64> mNext{0};
};
struct SymbolRef {
    quint64 service = 0, shard = 0;
    quint32 slot = NoNode;
    bool valid() const { return service != 0 && shard != 0 && slot != NoNode; }
    friend bool operator==(SymbolRef a, SymbolRef b) { return a.service == b.service && a.shard == b.shard && a.slot == b.slot; }
    friend bool operator!=(SymbolRef a, SymbolRef b) { return !(a == b); }
};
struct OutlineNodeRef {
    quint64 service = 0, projection = 0;
    quint32 slot = NoNode;
    bool valid() const { return service != 0 && projection != 0 && slot != NoNode; }
};
struct SemanticTextRef { quint32 offset = 0, size = 0; };
struct SemanticLocation { SemanticTextRef filename; int line = -1, column = -1; };
struct SemanticRecord {
    SemanticTextRef name, qualifiedName, type, arguments, signature, value, templateArguments, selectionKey;
    StatementKind kind{};
    StatementScope scope{};
    StatementAccessibility accessibility{};
    quint32 properties = 0;
    SemanticLocation declaration, definition;
};
struct SemanticMembership {
    SymbolRef symbol, parent;
    bool contextOnly = false;
};
struct SemanticOutlineNode {
    SymbolRef symbol;
    NodeIndex parent = NoNode;
    quint32 firstChild = 0, childCount = 0;
    bool contextOnly = false;
};
class SemanticPublicationBuilder;
class SemanticShard {
public:
    explicit SemanticShard(SemanticMemoryResource *memory): mText(memory), mRecords(memory) {}
    quint64 id() const { return mId; }
    const std::pmr::vector<SemanticRecord> &records() const { return mRecords; }
    QStringView text(SemanticTextRef ref) const noexcept;
private:
    friend class SemanticPublicationBuilder;
    quint64 mId = 0;
    std::pmr::vector<char16_t> mText;
    std::pmr::vector<SemanticRecord> mRecords;
};
using SemanticShardPtr = std::shared_ptr<const SemanticShard>;
class MembershipSegment {
public:
    explicit MembershipSegment(SemanticMemoryResource *memory): mMembers(memory) {}
    quint64 id() const { return mId; }
    const std::pmr::vector<SemanticMembership> &members() const { return mMembers; }
private:
    friend class SemanticPublicationBuilder;
    quint64 mId = 0;
    std::pmr::vector<SemanticMembership> mMembers;
};
using MembershipSegmentPtr = std::shared_ptr<const MembershipSegment>;
class OutlineProjection {
public:
    explicit OutlineProjection(SemanticMemoryResource *memory): mNodes(memory), mChildren(memory), mRoots(memory), mSelection(memory) {}
    quint64 id() const { return mId; }
    const std::pmr::vector<SemanticOutlineNode> &nodes() const { return mNodes; }
    const std::pmr::vector<NodeIndex> &children() const { return mChildren; }
    const std::pmr::vector<NodeIndex> &roots() const { return mRoots; }
private:
    friend class SemanticPublication;
    friend class SemanticPublicationBuilder;
    quint64 mId = 0;
    std::pmr::vector<SemanticOutlineNode> mNodes;
    std::pmr::vector<NodeIndex> mChildren, mRoots, mSelection;
};
using OutlineProjectionPtr = std::shared_ptr<const OutlineProjection>;
enum class SelectionResolution { Exact, UniqueBestEffort, Ambiguous, Missing };
struct SemanticSelectionMatches {
    SelectionResolution status = SelectionResolution::Missing;
    std::array<OutlineNodeRef, 16> candidates{};
    quint32 count = 0;
    bool hasMore = false;
};
class SemanticPublication {
public:
    explicit SemanticPublication(SemanticMemoryResource *memory): mShards(memory), mSegments(memory) {}
    const PublicationIdentity &identity() const { return mIdentity; }
    const std::pmr::vector<SemanticShardPtr> &shards() const { return mShards; }
    const std::pmr::vector<MembershipSegmentPtr> &segments() const { return mSegments; }
    const OutlineProjectionPtr &outline() const { return mOutline; }
    const SemanticRecord *symbol(SymbolRef ref) const noexcept;
    const SemanticOutlineNode *node(OutlineNodeRef ref) const noexcept;
    QStringView text(SymbolRef symbol, SemanticTextRef text) const noexcept;
    SemanticSelectionMatches selection(QStringView key, quint32 maximumCandidates = 16) const noexcept;
    SemanticSelectionMatches exact(OutlineNodeRef node) const noexcept;
private:
    friend class SemanticPublicationBuilder;
    PublicationIdentity mIdentity;
    std::pmr::vector<SemanticShardPtr> mShards;
    std::pmr::vector<MembershipSegmentPtr> mSegments;
    OutlineProjectionPtr mOutline;
};
using SemanticPublicationPtr = std::shared_ptr<const SemanticPublication>;
} // namespace Analysis
#endif
