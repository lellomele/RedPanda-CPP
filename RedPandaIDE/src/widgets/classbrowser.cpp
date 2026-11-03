/*
 * Copyright (C) 2020-2022 Roy Qu (royqh1979@gmail.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "classbrowser.h"
#include <QCoreApplication>
#include <QScopeGuard>
#include <QThread>
#include <limits>
#include <algorithm>

using Analysis::NodeIndex;
using Analysis::NoNode;

namespace {
constexpr unsigned slotBits = 19; // The supported projection has at most 250,000 nodes.
constexpr quintptr slotMask = (quintptr(1) << slotBits) - 1;
constexpr quint32 maximumEpoch = static_cast<quint32>(std::min<quint64>(
    std::numeric_limits<quintptr>::max() >> slotBits, std::numeric_limits<quint32>::max()));
}

bool ClassBrowserModel::onOwner() const
{
    return QThread::currentThread() == thread();
}

const Analysis::OutlineProjection *ClassBrowserModel::tree() const
{
    return mPublication && mPublication->outline() ? mPublication->outline().get() : nullptr;
}

OutlineApply ClassBrowserModel::expect(OutlineDemand demand, bool clearPreviousContext)
{
    if (!onOwner()) return OutlineApply::Invalid;
    if (mResetting) return OutlineApply::Busy;
    const bool differentContext = mDemand.service != demand.service || mDemand.workspace != demand.workspace;
    mDemand = demand;
    if (mPublication && (!demand.valid() || differentContext || clearPreviousContext))
        return replace({});
    return OutlineApply::Applied;
}

OutlineApply ClassBrowserModel::apply(OutlineDemand demand, Analysis::SemanticPublicationPtr publication)
{
    if (!onOwner() || !publication || !publication->outline() || !demand.valid() ||
            publication->identity().service != demand.service ||
            publication->identity().workspaceGeneration != demand.workspace ||
            publication->outline()->nodes().size() >= slotMask)
        return OutlineApply::Invalid;
    if (!(mDemand == demand)) return OutlineApply::Obsolete;
    return replace(std::move(publication));
}

OutlineApply ClassBrowserModel::replace(Analysis::SemanticPublicationPtr publication)
{
    if (mResetting) return OutlineApply::Busy;
    if (mEpoch == maximumEpoch) return OutlineApply::IndexEpochExhausted;
    mResetting = true;
    const auto resetFlag = qScopeGuard([this] { mResetting = false; });
    beginResetModel();
    auto previous = std::move(mPublication);
    mPublication = std::move(publication);
    ++mEpoch;
    endResetModel();
    return OutlineApply::Applied;
}

NodeIndex ClassBrowserModel::slot(const QModelIndex &index) const
{
    if (!index.isValid() || index.model() != this || index.column() != 0 || !tree())
        return NoNode;
    if (index.internalId() >> slotBits != mEpoch)
        return NoNode;
    const auto result = NodeIndex(index.internalId() & slotMask);
    if (result >= tree()->nodes().size() || tree()->nodes()[result].rowInParent != quint32(index.row()))
        return NoNode;
    return result;
}

QModelIndex ClassBrowserModel::indexForSlot(NodeIndex id) const
{
    const auto *outline = tree();
    if (!outline || id >= outline->nodes().size()) return {};
    return createIndex(int(outline->nodes()[id].rowInParent), 0, (quintptr(mEpoch) << slotBits) | id);
}

QModelIndex ClassBrowserModel::index(int row, int column, const QModelIndex &parent) const
{
    const auto *outline = tree();
    if (!outline || row < 0 || column != 0) return {};
    if (!parent.isValid()) {
        if (std::size_t(row) >= outline->roots().size()) return {};
        return indexForSlot(outline->roots()[row]);
    }
    const auto owner = slot(parent);
    if (owner == NoNode) return {};
    const auto &node = outline->nodes()[owner];
    if (quint32(row) >= node.childCount || node.firstChild + quint32(row) >= outline->children().size())
        return {};
    return indexForSlot(outline->children()[node.firstChild + row]);
}

QModelIndex ClassBrowserModel::parent(const QModelIndex &index) const
{
    const auto id = slot(index);
    return id == NoNode ? QModelIndex{} : indexForSlot(tree()->nodes()[id].parent);
}

int ClassBrowserModel::rowCount(const QModelIndex &parent) const
{
    const auto *outline = tree();
    if (!outline) return 0;
    if (!parent.isValid()) return int(outline->roots().size());
    const auto id = slot(parent);
    return id == NoNode ? 0 : int(outline->nodes()[id].childCount);
}

int ClassBrowserModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() && slot(parent) == NoNode ? 0 : 1;
}

QVariant ClassBrowserModel::data(const QModelIndex &index, int role) const
{
    const auto id = slot(index);
    if (id == NoNode) return {};
    const auto &node = tree()->nodes()[id];
    if (role == Qt::DisplayRole)
        return node.anonymous ? QCoreApplication::translate("ClassBrowserModel", "anonymous") : tree()->label(node).toString();
    if (role == Qt::ForegroundRole)
        return mAppearance.colors.value(int(node.color), mAppearance.text);
    if (role == Qt::DecorationRole)
        return mAppearance.icons.value(appearanceKey(node.icon));
    return {};
}

quint32 ClassBrowserModel::appearanceKey(Analysis::OutlineIconDescriptor icon)
{
    return (quint32(icon.kind) << 16) | (quint32(icon.scope) << 8) |
        (quint32(icon.accessibility) << 1) | quint32(icon.inherited);
}

void ClassBrowserModel::setAppearance(OutlineAppearance appearance)
{
    if (!onOwner() || mResetting) return;
    // The GUI owner schedules a viewport repaint. The immutable topology
    // and its indexes remain valid across changes to colors and icons.
    mAppearance = std::move(appearance);
}

OutlineNavigation ClassBrowserModel::navigation(const QModelIndex &index) const
{
    const auto id = slot(index);
    if (id == NoNode) return {};
    const auto symbol = tree()->nodes()[id].symbol;
    const auto *record = mPublication->symbol(symbol);
    if (!record) return {};
    return {mPublication->text(symbol, record->declaration.filename).toString(),
            mPublication->text(symbol, record->definition.filename).toString(),
            record->declaration.line, record->definition.line};
}

OutlineSelection ClassBrowserModel::selection(const QModelIndex &index) const
{
    const auto id = slot(index);
    if (id == NoNode) return {};
    const auto symbol = tree()->nodes()[id].symbol;
    const auto *record = mPublication->symbol(symbol);
    if (!record) return {};
    return {{symbol.service, tree()->id(), id}, mPublication->text(symbol, record->selectionKey).toString()};
}

QModelIndex ClassBrowserModel::indexFor(Analysis::OutlineNodeRef ref) const
{
    return mPublication && mPublication->node(ref) ? indexForSlot(ref.slot) : QModelIndex{};
}

QModelIndex ClassBrowserModel::uniqueIndexForHint(QStringView hint) const
{
    if (!mPublication || hint.isEmpty()) return {};
    const auto matches = mPublication->selection(hint, 1);
    return matches.status == Analysis::SelectionResolution::UniqueBestEffort ? indexFor(matches.candidates[0]) : QModelIndex{};
}
