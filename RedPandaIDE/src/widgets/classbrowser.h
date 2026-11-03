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
#pragma once

#include "../analysis/outlinejob.h"
#include <QAbstractItemModel>
#include <QColor>
#include <QHash>
#include <QIcon>

struct OutlineNavigation {
    QString declaration;
    QString definition;
    int declarationLine = -1;
    int definitionLine = -1;
};

struct OutlineSelection {
    Analysis::OutlineNodeRef exact;
    QString hint;
};

struct OutlineAppearance {
    QColor text;
    QHash<int, QColor> colors;
    QHash<quint32, QIcon> icons;
};

enum class OutlineApply { Applied, Obsolete, Invalid, Busy, IndexEpochExhausted };

class ClassBrowserModel final : public QAbstractItemModel {
    Q_OBJECT
public:
    using QAbstractItemModel::QAbstractItemModel;

    OutlineApply expect(OutlineDemand demand, bool clearPreviousContext);
    OutlineApply apply(OutlineDemand demand, Analysis::SemanticPublicationPtr publication);
    void setAppearance(OutlineAppearance appearance);
    static quint32 appearanceKey(Analysis::OutlineIconDescriptor descriptor);

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    OutlineNavigation navigation(const QModelIndex &) const;
    OutlineSelection selection(const QModelIndex &) const;
    QModelIndex indexFor(Analysis::OutlineNodeRef) const;
    QModelIndex uniqueIndexForHint(QStringView) const;

private:
    OutlineApply replace(Analysis::SemanticPublicationPtr publication);
    Analysis::NodeIndex slot(const QModelIndex &) const;
    QModelIndex indexForSlot(Analysis::NodeIndex) const;
    const Analysis::OutlineProjection *tree() const;
    bool onOwner() const;

    OutlineDemand mDemand;
    Analysis::SemanticPublicationPtr mPublication;
    OutlineAppearance mAppearance;
    quint32 mEpoch = 0;
    bool mResetting = false;
};
