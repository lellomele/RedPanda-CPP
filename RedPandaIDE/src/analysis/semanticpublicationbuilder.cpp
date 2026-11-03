// SPDX-License-Identifier: GPL-3.0-or-later
#include "semanticpublicationbuilder.h"
#include "../parser/parserutils.h" // Value enum definitions, no graph ownership.
#include <algorithm>
#include <QDeadlineTimer>
#include <charconv>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Analysis {
namespace {
struct Stopped { SemanticBuildStatus status; };

bool excluded(const SemanticSourceSymbol &symbol)
{
    return symbol.scope == StatementScope::Local || symbol.kind == StatementKind::Block
        || symbol.kind == StatementKind::Lambda;
}
bool inherited(const SemanticRecord &record)
{
    return (record.properties & static_cast<quint32>(StatementProperty::Inherited)) != 0;
}

bool hidden(const SemanticRecord &record, const SemanticShard &shard, const SemanticPublicationRequest &request)
{
    const auto &options = request.outline;
    if (!options.showInherited && inherited(record)) return true;
    const auto name = shard.text(record.name);
    if (!name.startsWith(u'_')) return false;
    const QStringView active(request.primaryFile);
    if (shard.text(record.declaration.filename) == active || shard.text(record.definition.filename) == active)
        return false;
    if (name.startsWith(u"__") && options.hideDoubleUnderscore) return true;
    return options.hideSingleUnderscore && record.kind != StatementKind::LiteralOperator;
}

StatementKind displayedKind(const SemanticRecord &record, const SemanticMembership &member)
{
    if (record.kind == StatementKind::Variable && !member.parent.valid())
        return StatementKind::GlobalVariable;
    if (record.kind == StatementKind::Parameter ||
        (record.kind == StatementKind::Variable && record.scope == StatementScope::Local))
        return StatementKind::LocalVariable;
    return record.kind;
}

void append(std::pmr::u16string &output, QStringView text)
{
    for (const auto character : text) output.push_back(character.unicode());
}

std::pmr::u16string displayLabel(const SemanticRecord &record, const SemanticShard &shard,
                                std::pmr::memory_resource *memory)
{
    const auto name = shard.text(record.name);
    const auto type = shard.text(record.type);
    const auto arguments = shard.text(record.arguments);
    const auto value = shard.text(record.value);
    std::pmr::u16string result(memory);
    append(result, name);
    if (!type.isEmpty()) {
        switch (record.kind) {
        case StatementKind::Variable:
        case StatementKind::Typedef:
            result += u" : ";
            append(result, type);
            if (arguments.startsWith(u'(')) result += u" (*)";
            append(result, arguments);
            return result;
        case StatementKind::Function:
            append(result, arguments);
            result += u" : ";
            append(result, type);
            return result;
        case StatementKind::OverloadedOperator: {
            const bool word = !name.isEmpty() && ((name.startsWith(u"::") && name.size() > 2)
                || name.front() == u'_' || name.front().isLetter());
            result = word ? u"operator " : u"operator";
            append(result, name);
            append(result, arguments);
            result += u" : ";
            append(result, type);
            if (word && name != u"new" && name != u"delete") {
                result += u" ";
                append(result, name);
            }
            return result;
        }
        case StatementKind::LiteralOperator:
            result = u"operator \"\"";
            append(result, name);
            append(result, arguments);
            result += u" : ";
            append(result, type);
            return result;
        default:
            break;
        }
    }
    if (record.kind == StatementKind::Enum) {
        if (!value.isEmpty()) {
            append(result, arguments);
            result += u'(';
            append(result, value);
            result += u')';
        }
    } else {
        append(result, arguments);
    }
    return result;
}

std::uint64_t textHash(QStringView text)
{
    std::uint64_t value = 1469598103934665603ULL;
    for (const QChar character : text) { value ^= character.unicode(); value *= 1099511628211ULL; }
    return value;
}
} // namespace
SemanticPublicationBuild SemanticPublicationBuilder::build(const SemanticGraphSource &source,
        const SemanticPublicationRequest &request, SemanticIdentitySource &identities,
        const SemanticMemoryLedger &ledger, std::shared_ptr<SemanticReclamationScheduler> scheduler)
{
    SemanticPublicationBuild result;
    if (!scheduler || !identities.service() || request.identity.service != identities.service()
            || request.identity.job == 0 || (!request.wholeProject && request.files.isEmpty()) ||
            request.maximumMilliseconds <= 0 || request.maximumMilliseconds > 30000) return result;
    QDeadlineTimer deadline(request.maximumMilliseconds);
    const auto checkpoint = [&] {
        if (request.cancelled && request.cancelled->load(std::memory_order_acquire))
            throw Stopped{SemanticBuildStatus::Cancelled};
        if (deadline.hasExpired())
            throw Stopped{SemanticBuildStatus::TimedOut};
    };
    if (request.cancelled && request.cancelled->load(std::memory_order_acquire)) {
        result.status = SemanticBuildStatus::Cancelled;
        return result;
    }
    SemanticBuildReservation reservation;
    try { reservation = ledger.tryReserve(request.initialReservation); }
    catch (const std::bad_alloc &) { result.status = SemanticBuildStatus::AllocationFailure; return result; }
    if (!reservation) { result.status = SemanticBuildStatus::BudgetExceeded; return result; }
    auto &diagnostics = result.diagnostics;
    auto &graph = diagnostics.graph;
    bool exhausted = false;
    const auto work = [&] {
        if (graph.workItems >= request.limits.maximumWorkItems) {
            graph.workLimitReached = true; exhausted = true; return false;
        }
        if ((graph.workItems++ & 1023) == 0) checkpoint();
        return true;
    };
    try {
        const auto scratch = reservation.resource(SemanticMemoryKind::Scratch);
        const auto shardMemory = reservation.resource(SemanticMemoryKind::Payload);
        const auto segmentMemory = reservation.resource(SemanticMemoryKind::Payload);
        const auto projectionMemory = reservation.resource(SemanticMemoryKind::Projection);
        const auto rootMemory = reservation.resource(SemanticMemoryKind::Payload);
        auto shard = Detail::makeSemanticOwner<SemanticShard>(shardMemory, scheduler, shardMemory.get());
        auto segment = Detail::makeSemanticOwner<MembershipSegment>(segmentMemory, scheduler, segmentMemory.get());
        auto projection = Detail::makeSemanticOwner<OutlineProjection>(projectionMemory, scheduler, projectionMemory.get());
        auto publication = Detail::makeSemanticOwner<SemanticPublication>(rootMemory, scheduler, rootMemory.get());
        shard->mId = identities.next(); segment->mId = identities.next(); projection->mId = identities.next();
        if (!shard->mId || !segment->mId || !projection->mId) {
            diagnostics.identityExhausted = true; return result;
        }
        publication->mIdentity = request.identity;
        publication->mShards.push_back(shard);
        publication->mSegments.push_back(segment);
        publication->mOutline = projection;
        std::pmr::unordered_map<SemanticSourceId, NodeIndex> exported(scratch.get());
        std::pmr::unordered_map<std::pmr::u16string, NodeIndex> groups(scratch.get());
        std::pmr::unordered_multimap<std::uint64_t, SemanticTextRef> strings(scratch.get());
        const auto text = [&](QStringView value) -> SemanticTextRef {
            const auto size = static_cast<quint64>(value.size());
            if (size > request.maximumTextUnitsExamined - diagnostics.textUnitsExamined) {
                diagnostics.textLimitReached = true; exhausted = true; return {};
            }
            diagnostics.textUnitsExamined += size;
            if (value.isEmpty()) return {};
            const auto hash = textHash(value);
            const auto existing = strings.equal_range(hash);
            for (auto item = existing.first; item != existing.second; ++item)
                if (shard->text(item->second) == value) return item->second;
            if (size > std::numeric_limits<quint32>::max() - shard->mText.size()) {
                diagnostics.textLimitReached = true; exhausted = true; return {};
            }
            const SemanticTextRef ref{static_cast<quint32>(shard->mText.size()), static_cast<quint32>(size)};
            const auto required = shard->mText.size() + static_cast<std::size_t>(size);
            if (required > shard->mText.capacity()) {
                const auto capacity = shard->mText.capacity();
                const auto growth = capacity <= std::numeric_limits<quint32>::max() / 2
                    ? std::max(std::size_t(64), capacity * 2) : std::size_t(std::numeric_limits<quint32>::max());
                shard->mText.reserve(std::max(required, growth));
            }
            for (const QChar character : value) shard->mText.push_back(character.unicode());
            strings.emplace(hash, ref);
            return ref;
        };
        const auto string = [&](QStringView value) {
            std::pmr::u16string copy(scratch.get());
            copy.reserve(value.size());
            for (const QChar character : value) copy.push_back(character.unicode());
            return copy;
        };
        const auto valueRecord = [&](const SemanticSourceSymbol &value) {
            SemanticRecord record;
            record.name = text(value.name); record.qualifiedName = text(value.qualifiedName);
            record.type = text(value.type); record.arguments = text(value.arguments); record.signature = text(value.signature);
            record.value = text(value.value); record.templateArguments = text(value.templateArguments);
            record.kind = value.kind; record.scope = value.scope; record.accessibility = value.accessibility;
            record.properties = value.properties;
            record.declaration = {text(value.declaration.filename), value.declaration.line, value.declaration.column};
            record.definition = {text(value.definition.filename), value.definition.line, value.definition.column};
            if (!value.selectionKey.isEmpty()) record.selectionKey = text(value.selectionKey);
            else {
                auto key = string(value.declaration.filename);
                key.push_back(u'\0');
                key += string(value.qualifiedName); key.push_back(u'\0'); key += string(value.signature);
                key.push_back(u'\0');
                char number[32]; const auto count = std::to_chars(number, number + sizeof(number), static_cast<int>(value.kind));
                for (auto at = number; at != count.ptr; ++at) key.push_back(*at);
                record.selectionKey = text(QStringView(key.data(), key.size()));
            }
            return record;
        };
        const auto read = [&](SemanticSourceId id, SemanticSourceSymbol &value) {
            if (!work()) return false;
            if (!source.symbol(id, value)) { diagnostics.sourceUnavailable = true; exhausted = true; return false; }
            return true;
        };
        struct Parent { NodeIndex node = NoNode; bool allowed = true; };
        const auto ensure = [&](SemanticSourceId id) -> Parent {
            std::pmr::vector<SemanticSourceId> chain(scratch.get());
            std::pmr::unordered_set<SemanticSourceId> ancestors(scratch.get());
            NodeIndex parent = NoNode;
            while (id && !exhausted) {
                const auto present = exported.find(id);
                if (present != exported.end()) { parent = present->second; break; }
                if (!ancestors.insert(id).second) { graph.cycleDetected = true; break; }
                if (chain.size() >= request.limits.maximumParentDepth) {
                    graph.parentDepthLimited = true; exhausted = true; return {NoNode, false};
                }
                SemanticSourceSymbol value;
                if (!read(id, value)) return {NoNode, false};
                ++graph.parentSteps;
                if (excluded(value)) return {NoNode, false};
                chain.push_back(id);
                id = value.parent;
            }
            while (!chain.empty() && !exhausted) {
                const auto current = chain.back(); chain.pop_back();
                SemanticSourceSymbol value;
                if (!read(current, value)) return {NoNode, false};
                auto group = string(value.scopeGroup);
                const auto existing = group.empty() ? groups.end() : groups.find(group);
                if (existing != groups.end()) {
                    parent = existing->second;
                    exported.emplace(current, parent);
                    ++graph.mergedScopes;
                    const auto &old = shard->mRecords[parent];
                    if (value.declaration.filename == QStringView(request.primaryFile)
                            && shard->text(old.declaration.filename) != QStringView(request.primaryFile)) {
                        auto replacement = valueRecord(value);
                        if (!exhausted) {
                            shard->mRecords[parent] = replacement;
                            segment->mMembers[parent].contextOnly = false;
                        }
                    }
                    continue;
                }
                if (shard->mRecords.size() >= request.limits.maximumNodes || shard->mRecords.size() >= NoNode) {
                    graph.nodeLimitReached = true; exhausted = true; return {NoNode, false};
                }
                auto record = valueRecord(value);
                if (exhausted) return {NoNode, false};
                const NodeIndex index = static_cast<NodeIndex>(shard->mRecords.size());
                shard->mRecords.push_back(record);
                const SymbolRef ref{request.identity.service, shard->mId, index};
                const SymbolRef owner = parent == NoNode ? SymbolRef{} : SymbolRef{request.identity.service, shard->mId, parent};
                const bool context = request.wholeProject
                    ? !(value.properties & static_cast<quint32>(StatementProperty::InProject))
                    : value.declaration.filename != QStringView(request.primaryFile) && value.definition.filename != QStringView(request.primaryFile);
                segment->mMembers.push_back({ref, owner, context});
                exported.emplace(current, index);
                if (!group.empty()) groups.emplace(std::move(group), index);
                parent = index;
            }
            return {parent, !exhausted};
        };
        struct Frame { SemanticSourceId source; bool leaving; };
        std::pmr::vector<Frame> pending(scratch.get());
        std::pmr::unordered_set<SemanticSourceId> visited(scratch.get()), active(scratch.get());
        const auto schedule = [&](SemanticSourceId id, bool leaving) {
            if (pending.size() >= request.limits.maximumPendingFrames) {
                graph.pendingLimitReached = true; exhausted = true; return false;
            }
            pending.push_back({id, leaving}); ++graph.scheduledFrames;
            graph.peakPendingFrames = std::max(graph.peakPendingFrames, quint64(pending.size()));
            return true;
        };
        auto push = [&](SemanticSourceId id) {
            if (exhausted || !work()) return false;
            ++graph.sourceEntriesExamined;
            if (!id) return true;
            if (visited.count(id)) { if (active.count(id)) graph.cycleDetected = true; return true; }
            return schedule(id, false);
        };
        using Push = decltype(push);
        const SemanticSourceVisitor visitor{&push, [](void *context, SemanticSourceId id) {
            return (*static_cast<Push *>(context))(id);
        }};
        for (const auto &file : request.files) {
            if (exhausted) break;
            ++diagnostics.sourceFiles;
            if (!source.roots(QStringView(file), visitor) && !exhausted) {
                diagnostics.sourceUnavailable = true; exhausted = true;
            }
        }
        while (!pending.empty() && !exhausted) {
            const auto frame = pending.back(); pending.pop_back(); ++graph.processedFrames;
            if (frame.leaving) { active.erase(frame.source); continue; }
            if (visited.count(frame.source)) { if (active.count(frame.source)) graph.cycleDetected = true; continue; }
            if (graph.visitedStatements >= request.limits.maximumSourceStatements) {
                graph.sourceLimitReached = true; exhausted = true; break;
            }
            visited.insert(frame.source); ++graph.visitedStatements;
            SemanticSourceSymbol value;
            if (!read(frame.source, value)) break;
            if (excluded(value) || (request.wholeProject && !(value.properties & static_cast<quint32>(StatementProperty::InProject)))) continue;
            const auto node = ensure(frame.source);
            if (!node.allowed) continue;
            active.insert(frame.source);
            if (!work() || !schedule(frame.source, true)) break;
            if (!source.children(frame.source, visitor) && !exhausted) {
                diagnostics.sourceUnavailable = true; exhausted = true;
            }
        }
        // The union owns one payload set; file maps are overlapping membership
        // sources. Projection construction never repeats the engine traversal.
        const auto count = segment->mMembers.size();
        std::pmr::vector<quint8> visible(count, 0, scratch.get()), context(count, 0, scratch.get());
        for (std::size_t index = count; index-- > 0;) {
            if ((index & 1023) == 0) checkpoint();
            const auto &member = segment->mMembers[index];
            const bool filtered = hidden(shard->mRecords[index], *shard, request);
            if (!filtered) visible[index] = true;
            if (!visible[index]) continue;
            context[index] = member.contextOnly || filtered;
            if (member.parent.valid()) visible[member.parent.slot] = true;
        }

        std::pmr::vector<NodeIndex> projected(count, NoNode, scratch.get());
        std::pmr::vector<SemanticTextRef> sortKeys(scratch.get());
        std::pmr::vector<char16_t> sortText(scratch.get());
        const auto storeText = [](auto &buffer, const auto &source) {
            const auto remaining = std::numeric_limits<quint32>::max() - buffer.size();
            if (source.size() > remaining) throw SemanticBudgetExceeded{};
            const SemanticTextRef ref{static_cast<quint32>(buffer.size()), static_cast<quint32>(source.size())};
            buffer.insert(buffer.end(), source.begin(), source.end());
            return ref;
        };
        projection->mNodes.reserve(std::count(visible.begin(), visible.end(), 1));
        for (NodeIndex index = 0; index < count; ++index) {
            if ((index & 1023) == 0) checkpoint();
            if (!visible[index]) continue;
            const auto &member = segment->mMembers[index];
            const auto &record = shard->mRecords[index];
            const auto name = shard->text(record.name);
            SemanticOutlineNode node;
            node.symbol = member.symbol;
            node.parent = member.parent.valid() ? projected[member.parent.slot] : NoNode;
            node.contextOnly = context[index];
            node.anonymous = name == u"{";
            if (!node.anonymous)
                node.label = storeText(projection->mText, displayLabel(record, *shard, scratch.get()));
            node.color = displayedKind(record, member);
            node.icon = {node.color, record.scope, record.accessibility, inherited(record)};
            if (node.color == StatementKind::Keyword && name.startsWith(u'#'))
                node.color = StatementKind::Preprocessor;
            if (request.outline.alphabetical) {
                const auto lower = name.toString().toLower();
                auto key = string(QStringView(lower));
                sortKeys.push_back(storeText(sortText, key));
            }
            projected[index] = projection->mNodes.size();
            projection->mNodes.push_back(node);
        }

        const auto nodeCount = projection->mNodes.size();
        std::pmr::vector<quint32> childCounts(nodeCount, 0, scratch.get()), writeOffsets(nodeCount, 0, scratch.get());
        projection->mSelection.reserve(nodeCount);
        for (NodeIndex index = 0; index < nodeCount; ++index) {
            const auto parent = projection->mNodes[index].parent;
            if (parent == NoNode) projection->mRoots.push_back(index);
            else ++childCounts[parent];
        }
        quint32 total = 0;
        for (NodeIndex index = 0; index < nodeCount; ++index) {
            auto &node = projection->mNodes[index];
            node.firstChild = total;
            node.childCount = childCounts[index];
            writeOffsets[index] = total;
            total += node.childCount;
            projection->mSelection.push_back(index);
        }
        projection->mChildren.resize(total);
        for (NodeIndex index = 0; index < nodeCount; ++index) {
            const auto parent = projection->mNodes[index].parent;
            if (parent != NoNode) projection->mChildren[writeOffsets[parent]++] = index;
        }
        const auto recordFor = [&](NodeIndex index) -> const SemanticRecord & {
            return shard->mRecords[projection->mNodes[index].symbol.slot];
        };
        const auto key = [&](NodeIndex index) { return shard->text(recordFor(index).selectionKey); };
        const auto alphabetical = [&](NodeIndex index) {
            const auto ref = sortKeys[index];
            return ref.size ? QStringView(sortText.data() + ref.offset, ref.size) : QStringView{};
        };
        quint64 comparisons = 0;
        const auto order = [&](NodeIndex a, NodeIndex b) {
            if ((comparisons++ & 1023) == 0) checkpoint();
            const auto &left = recordFor(a), &right = recordFor(b);
            if (request.outline.groupByKind && left.kind != right.kind) return left.kind < right.kind;
            if (request.outline.alphabetical) {
                const int name = alphabetical(a).compare(alphabetical(b));
                if (name) return name < 0;
            }
            if (request.wholeProject) {
                const int file = shard->text(left.declaration.filename).compare(shard->text(right.declaration.filename));
                if (file) return file < 0;
            }
            if (left.declaration.line != right.declaration.line) return left.declaration.line < right.declaration.line;
            const int selection = key(a).compare(key(b));
            return selection ? selection < 0 : a < b;
        };
        std::sort(projection->mRoots.begin(), projection->mRoots.end(), order);
        for (quint32 row = 0; row < projection->mRoots.size(); ++row)
            projection->mNodes[projection->mRoots[row]].rowInParent = row;
        for (const auto &node : projection->mNodes) {
            const auto children = projection->mChildren.begin() + node.firstChild;
            std::sort(children, children + node.childCount, order);
            for (quint32 row = 0; row < node.childCount; ++row)
                projection->mNodes[children[row]].rowInParent = row;
        }
        std::sort(projection->mSelection.begin(), projection->mSelection.end(), [&](NodeIndex a, NodeIndex b) {
            if ((comparisons++ & 1023) == 0) checkpoint();
            const int compare = key(a).compare(key(b)); return compare ? compare < 0 : a < b;
        });
        checkpoint();
        diagnostics.uniquePayloads = shard->mRecords.size();
        diagnostics.textCodeUnits = shard->mText.size();
        result.publication = std::move(publication);
        result.status = graph.complete() && !diagnostics.sourceUnavailable && !diagnostics.textLimitReached
            ? SemanticBuildStatus::Complete : SemanticBuildStatus::Partial;
    } catch (const Stopped &stopped) { result.status = stopped.status; }
    catch (const SemanticBudgetExceeded &) { result.status = SemanticBuildStatus::BudgetExceeded; }
    catch (const std::bad_alloc &) { result.status = SemanticBuildStatus::AllocationFailure; }
    catch (...) { diagnostics.sourceUnavailable = true; result.status = SemanticBuildStatus::SourceFailure; }
    return result;
}
} // namespace Analysis
