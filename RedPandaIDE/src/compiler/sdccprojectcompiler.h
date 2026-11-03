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
#ifndef SDCCPROJECTCOMPILER_H
#define SDCCPROJECTCOMPILER_H

#include "projectcompiler.h"
#include <QObject>
#include "generatedtextfile.h"

class Project;
class SDCCProjectCompiler : public ProjectCompiler
{
    Q_OBJECT
public:
    explicit SDCCProjectCompiler(Compilation::ProjectBuildSnapshot snapshot);
    SDCCProjectCompiler(const SDCCProjectCompiler&)=delete;
    SDCCProjectCompiler& operator=(const SDCCProjectCompiler&)=delete;
    void buildMakeFile() override;

private:
    void createStandardMakeFile();
    void newMakeFile(Compilation::GeneratedTextFile& file);
    void writeMakeHeader(Compilation::GeneratedTextFile& file);
    void writeMakeDefines(Compilation::GeneratedTextFile& file);
    void writeMakeTarget(Compilation::GeneratedTextFile& file);
    void writeMakeIncludes(Compilation::GeneratedTextFile& file);
    void writeMakeClean(Compilation::GeneratedTextFile& file);
    void writeMakeObjFilesRules(Compilation::GeneratedTextFile& file);
    void writeln(Compilation::GeneratedTextFile& file, const QString& s="");
    // Compiler interface
protected:
    bool prepareForCompile() override;
    bool prepareForRebuild() override;
};

#endif // PROJECTCOMPILER_H
