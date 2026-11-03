// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ENGINESEMANTICADAPTER_H
#define ENGINESEMANTICADAPTER_H
// Private engine boundary. GUI consumers include semanticpublication.h only.
#include "semanticpublicationbuilder.h"
#include "../parser/parserutils.h"
#include <unordered_map>

namespace Analysis {
struct EngineSemanticFileView { QStringView filename; const StatementMap *statements = nullptr; };
class EngineSemanticGraphSource final : public SemanticGraphSource {
public:
    // File views, their names/maps and the engine graph must remain stable under
    // the writer reservation for this adapter's entire lifetime.
    EngineSemanticGraphSource(const EngineSemanticFileView *files, std::size_t count,
                              std::shared_ptr<SemanticMemoryResource> scratch);
    bool symbol(SemanticSourceId, SemanticSourceSymbol &) const override;
    bool roots(QStringView, SemanticSourceVisitor) const override;
    bool children(SemanticSourceId, SemanticSourceVisitor) const override;
private:
    SemanticSourceId remember(const PStatement &) const;
    struct ExactFilenameHash {
        std::size_t operator()(QStringView filename) const noexcept { return qHash(filename); }
    };
    std::shared_ptr<SemanticMemoryResource> mScratch;
    // The caller has already certified exact lexical spellings. Borrowed
    // names and maps stay inside the same writer reservation as this index.
    std::pmr::unordered_map<QStringView, const StatementMap *, ExactFilenameHash> mByFile;
    mutable std::pmr::unordered_map<SemanticSourceId, PStatement> mSources;
};
} // namespace Analysis
#endif
