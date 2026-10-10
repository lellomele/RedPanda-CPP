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
#ifndef ISSUESTABLE_H
#define ISSUESTABLE_H

#include <QTableView>
#include <vector>
#include "../common.h"
#include "../diagnosticcatalog.h"
#include <QAbstractTableModel>
#include <QStyledItemDelegate>
#include <QCache>

class DiagnosticItemDelegate : public QStyledItemDelegate {
public:
    explicit DiagnosticItemDelegate(QObject *parent = nullptr);
protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override;
private:
    mutable QCache<quint64, QColor> mTextColors;
};

class IssuesModel : public QAbstractTableModel {

    Q_OBJECT
public:
    explicit IssuesModel(QObject *parent = nullptr);

public slots:
    void addIssue(PCompileIssue issue);
    void clearIssues();

    void setErrorColor(QColor color);
    void setWarningColor(QColor color);
    PCompileIssue issue(int row);
    QString translatedDescription(int row) const;
    QString originalDescription(int row) const;
    QString diagnosticHint(int row) const;
    bool italianDiagnostics() const;
private:
    QVector<PCompileIssue> mIssues;
    QVector<DiagnosticCatalog::Translation> mTranslations;
    bool mItalianDiagnostics;
    QColor mErrorColor;
    QColor mWarningColor;

    // QAbstractItemModel interface
public:
    int count();
    int rowCount(const QModelIndex &parent) const override;
    int columnCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    const QVector<PCompileIssue> &issues() const;
};

class IssuesTable : public QTableView
{
    Q_OBJECT
public:

    explicit IssuesTable(QWidget* parent = nullptr);

    const QVector<PCompileIssue> & issues() const;

    IssuesModel* issuesModel();

    void setErrorColor(QColor color);
    void setWarningColor(QColor color);
    QString toHtml();
    QString toTxt();

public slots:
    void addIssue(PCompileIssue issue);

    PCompileIssue issue(const QModelIndex& index);
    PCompileIssue issue(const int row);
    int count();

    void clearIssues();

private:
    IssuesModel * mModel;
};

#endif // ISSUESTABLE_H
