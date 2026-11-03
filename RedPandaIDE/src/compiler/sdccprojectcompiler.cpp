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
#include "sdccprojectcompiler.h"
#include "compilermanager.h"
#include "../systemconsts.h"
#include "qt_utils/utils.h"
#include "../utils.h"
#include "../utils/escape.h"
#include "../utils/file.h"

#include <QDir>

SDCCProjectCompiler::SDCCProjectCompiler(Compilation::ProjectBuildSnapshot snapshot):
    ProjectCompiler(std::move(snapshot))
{
}

void SDCCProjectCompiler::buildMakeFile()
{
    //we are using custom make file, don't overwrite it
    if (mBuild.options.useCustomMakefile && !mBuild.options.customMakefile.isEmpty())
        return;
    createStandardMakeFile();
}

void SDCCProjectCompiler::createStandardMakeFile()
{
    validateGeneratedOutputPath(mBuild.makefilePath);
    Compilation::GeneratedTextFile file(mBuild.makefilePath, configuration().toolchain.makerUsingUTF8);
    newMakeFile(file);
    QString suffix = configuration().toolchain.executableSuffix;
    if (suffix == SDCC_IHX_SUFFIX) {
        writeln(file,"$(BIN_TAR): $(OBJ)");
        writeln(file,"\t$(CC) $(LIBS) -o $(BIN_ARG) $(LINKOBJ)");
    } else {
        writeln(file,"$(IHX_TAR): $(OBJ)\n");
        writeln(file,"\t$(CC) $(LIBS) -o $(IHX_ARG) $(LINKOBJ)");
        if (suffix == SDCC_HEX_SUFFIX) {
            writeln(file,"$(BIN_TAR): $(IHX_DEP)");
            writeln(file,"\t$(PACKIHX) $(IHX_ARG) > $(BIN_ARG)");
        } else {
            writeln(file,"$(BIN_TAR): $(IHX_DEP)");
            writeln(file,"\t$(MAKEBIN) $(IHX_ARG) $(BIN_ARG)");
        }
    }
    writeMakeObjFilesRules(file);
    checkCancellation();
    const auto committed = file.commit();
    if (!committed.succeeded()) throw CompileError(committed.error);
}

void SDCCProjectCompiler::newMakeFile(Compilation::GeneratedTextFile &file)
{
    // Create OBJ output directory
    if (!mBuild.options.folderForObjFiles.isEmpty() &&
            !QDir(mBuild.directory).mkpath(mBuild.options.folderForObjFiles))
        throw CompileError(tr("Can't create build directory '%1'.").arg(mBuild.options.folderForObjFiles));
    // Create executable output directory
    if (!mBuild.options.folderForOutput.isEmpty() &&
            !QDir(mBuild.directory).mkpath(mBuild.options.folderForOutput))
        throw CompileError(tr("Can't create build directory '%1'.").arg(mBuild.options.folderForOutput));
    // Write more information to the log file than before
    log(tr("Building makefile..."));
    log("--------");
    log(tr("- Filename: %1").arg(mBuild.makefilePath));

    // Create the actual file
    const auto opened = file.open();
    if (!opened.succeeded()) throw CompileError(opened.error);

    // Write header
    writeMakeHeader(file);

    // Writes definition list
    writeMakeDefines(file);

    // Write PHONY and all targets
    writeMakeTarget(file);

    // Write list of includes
    writeMakeIncludes(file);

    // Write clean command
    writeMakeClean(file);

}

void SDCCProjectCompiler::writeMakeHeader(Compilation::GeneratedTextFile &file)
{
    writeln(file,"# Project: " + mBuild.displayName);
    writeln(file,QString("# Makefile created by Red Panda C++ ") + REDPANDA_CPP_VERSION);
    writeln(file);
}

void SDCCProjectCompiler::writeMakeDefines(Compilation::GeneratedTextFile &file)
{
    // Get list of object files
    QStringList Objects;
    QStringList LinkObjects;
    QStringList cleanObjects;

    // Create a list of object files
    for (const auto &unit : mBuild.units) {
        if (!unit.compile && !unit.link)
            continue;

        // Only process source files
        QString RelativeName = extractRelativePath(mBuild.directory, unit.path);
        FileType fileType = getFileType(RelativeName);

        if (isC_CPP_ASMSourceFile(fileType)) {
            if (!mBuild.options.folderForObjFiles.isEmpty()) {
                // ofile = C:\MyProgram\obj\main.o
                QString fullObjFile = includeTrailingPathDelimiter(mBuild.options.folderForObjFiles)
                        + extractFileName(unit.path);
                QString relativeObjFile = extractRelativePath(mBuild.directory, changeFileExt(fullObjFile, SDCC_REL_SUFFIX));
                Objects << relativeObjFile;
                cleanObjects << localizeMakefilePath(relativeObjFile);
                if (unit.link) {
                    LinkObjects << relativeObjFile;
                }
            } else {
                Objects << changeFileExt(RelativeName, SDCC_REL_SUFFIX);
                cleanObjects << localizeMakefilePath(changeFileExt(RelativeName, SDCC_REL_SUFFIX));
                if (unit.link)
                    LinkObjects << changeFileExt(RelativeName, SDCC_REL_SUFFIX);
            }
        }
    }

    QString cc = extractFileName(configuration().toolchain.cCompiler);

    QStringList cCompileArguments = getCCompileArguments(mOnlyCheckSyntax);
    QStringList libraryArguments = getLibraryArguments(FileType::Project);
    QStringList cIncludeArguments = getCIncludeArguments() + getProjectIncludeArguments();

    QString executable = extractRelativePath(mBuild.makefilePath, mBuild.outputPath);
    QString cleanExe = localizeMakefilePath(executable);
    QString ihx = extractRelativePath(mBuild.makefilePath, changeFileExt(mBuild.outputPath, SDCC_IHX_SUFFIX));
    QString cleanIhx = localizeMakefilePath(ihx);

    writeln(file, "CC       = " + escapeArgumentForMakefileVariableValue(cc, true));
    writeln(file, "PACKIHX  = " PACKIHX_PROGRAM);
    writeln(file, "MAKEBIN  = " MAKEBIN_PROGRAM);

    writeln(file, "OBJ      = " + escapeFilenamesForMakefilePrerequisite(Objects));
    writeln(file, "LINKOBJ  = " + escapeArgumentsForMakefileVariableValue(LinkObjects));
    writeln(file,"CLEANOBJ = " + escapeArgumentsForMakefileVariableValue(cleanObjects) + ' ' +
        escapeArgumentForMakefileVariableValue(cleanIhx, false) + ' ' +
        escapeArgumentForMakefileVariableValue(cleanExe, false));
    writeln(file, "LIBS     = " + escapeArgumentsForMakefileVariableValue(libraryArguments));
    writeln(file, "INCS     = " + escapeArgumentsForMakefileVariableValue(cIncludeArguments));
    writeln(file, "IHX_TAR  = " + escapeFilenameForMakefileTarget(ihx));
    writeln(file, "IHX_DEP  = " + escapeFilenameForMakefilePrerequisite(ihx));
    writeln(file, "IHX_ARG  = " + escapeArgumentForMakefileVariableValue(ihx, false));
    writeln(file, "BIN_TAR  = " + escapeFilenameForMakefileTarget(executable));
    writeln(file, "BIN_DEP  = " + escapeFilenameForMakefilePrerequisite(executable));
    writeln(file, "BIN_ARG  = " + escapeArgumentForMakefileVariableValue(executable, false));
    writeln(file, "CFLAGS   = $(INCS) " + escapeArgumentsForMakefileVariableValue(cCompileArguments));
    writeln(file, "RM       = " CLEAN_PROGRAM);
    writeln(file, "CD       = " CD_PROGRAM);

    writeln(file);
}

void SDCCProjectCompiler::writeMakeTarget(Compilation::GeneratedTextFile &file)
{
    writeln(file, ".PHONY: all all-before all-after clean clean-custom");
    writeln(file);
    writeln(file, "all: all-before $(BIN_DEP) all-after");
    writeln(file);

}

void SDCCProjectCompiler::writeMakeIncludes(Compilation::GeneratedTextFile &file)
{
    foreach(const QString& s, mBuild.options.makeIncludes) {
        writeln(file, "include " + escapeFilenameForMakefileInclude(s));
    }
    if (!mBuild.options.makeIncludes.isEmpty()) {
        writeln(file);
    }
}

void SDCCProjectCompiler::writeMakeClean(Compilation::GeneratedTextFile &file)
{
    writeln(file, "clean: clean-custom");
    writeln(file, QString("\t-$(RM) $(CLEANOBJ) >%1 2>&1||:").arg(NULL_FILE));
    writeln(file);
}

void SDCCProjectCompiler::writeMakeObjFilesRules(Compilation::GeneratedTextFile &file)
{

    const auto &projectUnits = mBuild.units;
    for (const auto &unit : projectUnits) {
        if (!unit.compile)
            continue;
        FileType fileType = getFileType(unit.path);
        // Only process source files
        if (!isC_CPP_ASMSourceFile(fileType))
            continue;

        QString shortFileName = extractRelativePath(mBuild.makefilePath,unit.path);

        writeln(file);
        QString objStr = escapeFilenameForMakefilePrerequisite(shortFileName);
        // if we have scanned it, use scanned info
        const auto dependencies = mBuild.knownDependencies.constFind(unit.path);
        if (dependencies != mBuild.knownDependencies.cend()) {
            const auto &includedFiles = *dependencies;
            for (const auto &unit2 : projectUnits) {
                if (&unit2 == &unit)
                    continue;
                if (includedFiles.contains(unit2.path)) {
                    QString header = extractRelativePath(mBuild.makefilePath,unit2.path);
                    objStr = objStr + ' ' + escapeFilenameForMakefilePrerequisite(header);
                }
            }
        } else {
            for (const auto &unit2 : projectUnits) {
                FileType fileType = getFileType(unit2.path);
                if (isC_CPPHeaderFile(fileType)) {
                    QString header = extractRelativePath(mBuild.makefilePath,unit2.path);
                    objStr = objStr + ' ' + escapeFilenameForMakefilePrerequisite(header);
                }
            }
        }
        QString objFileNameTarget;
        QString objFileNameCommand;
        if (!mBuild.options.folderForObjFiles.isEmpty()) {
            QString fullObjname = includeTrailingPathDelimiter(mBuild.options.folderForObjFiles) +
                    extractFileName(unit.path);
            QString objFile = extractRelativePath(mBuild.makefilePath, changeFileExt(fullObjname, SDCC_REL_SUFFIX));
            objFileNameTarget = escapeFilenameForMakefileTarget(objFile);
            objFileNameCommand = escapeArgumentForMakefileRecipe(objFile, false);
        } else {
            QString objFile = changeFileExt(shortFileName, SDCC_REL_SUFFIX);
            objFileNameTarget = escapeFilenameForMakefileTarget(objFile);
            objFileNameCommand = escapeArgumentForMakefileRecipe(objFile, false);
        }

        objStr = objFileNameTarget + ": " + objStr;

        writeln(file, objStr);

        // Write custom build command

        if (unit.overrideBuildCommand && !unit.buildCommand.isEmpty()) {
            QString BuildCmd = unit.buildCommand;
            BuildCmd.replace("<CRTAB>", "\n\t");
            writeln(file, '\t' + BuildCmd);
            // Or roll our own
        } else {
            if (fileType==FileType::CSource) {
                if(mBuild.options.folderForObjFiles.isEmpty()) {
                    writeln(file, "\t$(CC) $(CFLAGS) -c " + escapeArgumentForMakefileRecipe(shortFileName, false));
                }else{
                    QString fullObjDir = includeTrailingPathDelimiter(mBuild.options.folderForObjFiles);
                    QString relativeObjDir = extractRelativePath(mBuild.directory,fullObjDir);
                    QString objfile=extractRelativePath(generateAbsolutePath(mBuild.directory,relativeObjDir),unit.path);
                    writeln(file, "\t$(CD) "+ localizeMakefilePath(relativeObjDir)+" && $(CC) $(CFLAGS) -c " + localizeMakefilePath(objfile));
                }

                }
        }
    }

}

void SDCCProjectCompiler::writeln(Compilation::GeneratedTextFile &file, const QString &s)
{
    checkCancellation();
    const auto written = file.appendLine(s);
    if (!written.succeeded()) throw CompileError(written.error);
}

bool SDCCProjectCompiler::prepareForRebuild()
{
    //we use make argument to clean
    return true;
}

bool SDCCProjectCompiler::prepareForCompile()
{
    //initProgressForm();
    log(tr("Compiling project changes..."));
    log("--------");
    log(tr("- Project Filename: %1").arg(mBuild.projectPath));
    log(tr("- Compiler Set Name: %1").arg(configuration().toolchain.name));
    log("");

    const bool generated = !mBuild.options.useCustomMakefile || mBuild.options.customMakefile.isEmpty();
    if (generated && !mOnlyGenerateMakefile) validateGeneratedOutputPath(mBuild.outputPath);
    buildMakeFile();
    if (mOnlyGenerateMakefile) {
        if (!QFileInfo(mBuild.makefilePath).isFile())
            throw CompileError(tr("Makefile '%1' does not exist.").arg(mBuild.makefilePath));
        return true;
    }

    mCompiler = configuration().toolchain.make;

    if (!programExists(mCompiler)) {
        throw CompileError(
                    tr("Make program '%1' doesn't exists!").arg(mCompiler)
                    +"<br />"
                    +tr("Please check the \"program\" page of compiler settings."));
    }

    QString parallelParam;
    if (mBuild.options.allowParallelBuilding) {
        if (mBuild.options.parellelBuildingJobs==0) {
            parallelParam = "--jobs";
        } else {
            parallelParam = QString("-j%1").arg(mBuild.options.parellelBuildingJobs);
        }
    } else {
        parallelParam = "-j1";
    }

    QString makefile =
            extractRelativePath(mBuild.directory, mBuild.makefilePath);
    QStringList cleanArgs{
        "-f",
        makefile,
        "clean",
    };
    QStringList makeAllArgs{
        parallelParam,
        "-f",
        makefile,
        "all",
    };
    if (onlyClean()) {
        mArguments = cleanArgs;
    } else if (mRebuild) {
        mArguments = cleanArgs;
        mExtraCompilersList << mCompiler;
        mExtraOutputFilesList << "";
        mExtraArgumentsList << makeAllArgs;
    } else {
        mArguments = makeAllArgs;
    }
    mDirectory = mBuild.directory;

    mOutputFile = mBuild.outputPath;
    log(tr("Processing makefile:"));
    log("--------");
    log(tr("- makefile processer: %1").arg(mCompiler));
    QString command = escapeCommandForLog(mCompiler, mArguments);
    log(tr("- Command: %1").arg(command));
    log("");

    return true;
}
