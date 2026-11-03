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
#ifndef PROJECTCOMPILER_H
#define PROJECTCOMPILER_H

#include "compiler.h"
#include "projectbuildsnapshot.h"
#include <QObject>
#include "generatedtextfile.h"

class ProjectCompiler : public Compiler
{
    Q_OBJECT
public:
    explicit ProjectCompiler(Compilation::ProjectBuildSnapshot snapshot);
    ProjectCompiler(const ProjectCompiler&)=delete;
    ProjectCompiler& operator=(const ProjectCompiler&)=delete;
    virtual void buildMakeFile();

    bool onlyClean() const;
    void setOnlyClean(bool newOnlyClean);
    void setOnlyGenerateMakefile(bool enabled) { mOnlyGenerateMakefile = enabled; }

private:
    void createStandardMakeFile();
    void createStaticMakeFile();
    void createDynamicMakeFile();
    void newMakeFile(Compilation::GeneratedTextFile& file);
    void newMakeFile(Compilation::GeneratedTextFile& file, bool &genModuleDef);
    void writeMakeHeader(Compilation::GeneratedTextFile& file);
    void writeMakeDefines(Compilation::GeneratedTextFile& file, bool &genModuleDef);
    void writeMakeTarget(Compilation::GeneratedTextFile& file);
    void writeMakeIncludes(Compilation::GeneratedTextFile& file);
    void writeMakeClean(Compilation::GeneratedTextFile& file);
    void writeMakeObjFilesRules(Compilation::GeneratedTextFile& file);
    void writeln(Compilation::GeneratedTextFile& file, const QString& s="");
    // Compiler interface
private:
    bool mOnlyClean;
protected:
    const Compilation::ProjectBuildSnapshot mBuild;
    bool prepareForCompile() override;
    bool prepareForRebuild() override;
};

#endif // PROJECTCOMPILER_H
