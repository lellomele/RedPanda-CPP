// SPDX-License-Identifier: GPL-3.0-or-later
#include "enginesemanticadapter.h"
#include <cstdint>

namespace Analysis {
EngineSemanticGraphSource::EngineSemanticGraphSource(const EngineSemanticFileView *files, std::size_t count,
                                                   std::shared_ptr<SemanticMemoryResource> scratch):
    mFiles(files), mFileCount(count), mScratch(std::move(scratch)), mSources(mScratch.get()) {}
SemanticSourceId EngineSemanticGraphSource::remember(const PStatement &statement) const
{
    if (!statement) return 0;
    const auto id = static_cast<SemanticSourceId>(reinterpret_cast<std::uintptr_t>(statement.get()));
    if (mSources.find(id) == mSources.end()) mSources.emplace(id, statement);
    return id;
}
bool EngineSemanticGraphSource::symbol(SemanticSourceId id, SemanticSourceSymbol &value) const
{
    const auto found = mSources.find(id);
    if (found == mSources.end()) return false;
    const auto &statement = *found->second;
    value.parent = remember(statement.parentScope.lock());
    value.name = statement.command; value.qualifiedName = statement.fullName;
    value.type = statement.type; value.arguments = statement.args; value.signature = statement.noNameArgs;
    value.value = statement.value; value.templateArguments = statement.templateSpecializationParams;
    value.kind = statement.kind; value.scope = statement.scope; value.accessibility = statement.accessibility;
    value.properties = static_cast<quint32>(static_cast<int>(statement.properties));
    value.declaration = {statement.fileName, statement.line, -1};
    value.definition = {statement.definitionFileName, statement.definitionLine, -1};
    value.selectionKey = {};
    value.scopeGroup = statement.kind == StatementKind::Namespace ? QStringView(statement.fullName) : QStringView{};
    return true;
}
bool EngineSemanticGraphSource::roots(QStringView filename, SemanticSourceVisitor visitor) const
{
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    for (std::size_t index = 0; index < mFileCount; ++index) {
        if (mFiles[index].filename.compare(filename, sensitivity) != 0) continue;
        if (!mFiles[index].statements) return false;
        for (const auto &statement : *mFiles[index].statements)
            if (!visitor(remember(statement))) return false;
        return true;
    }
    return false;
}
bool EngineSemanticGraphSource::children(SemanticSourceId id, SemanticSourceVisitor visitor) const
{
    const auto found = mSources.find(id);
    if (found == mSources.end()) return false;
    // A remembered shared owner pins the source, but it is still read only
    // under the engine writer reservation. It is never transferred to output.
    const auto statement = found->second;
    for (const auto &child : statement->children)
        if (!visitor(remember(child))) return false;
    return true;
}
} // namespace Analysis
