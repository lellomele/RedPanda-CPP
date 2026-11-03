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
#include "projectcompiler.h"
#include "compilermanager.h"
#include "../systemconsts.h"
#include "qt_utils/charsetinfo.h"
#include "qt_utils/utils.h"
#include "../utils.h"
#include "../utils/escape.h"
#include "../utils/file.h"
#include "../utils/parsearg.h"
#include "../utils/parsemacros.h"
#include "../mainwindow.h"

#include <QCoreApplication>
#include <QDir>
#if defined(Q_OS_WIN) && MAKE_INTERFACE == MAKE_INTERFACE_mingw32
#include <windows.h>

namespace {
QString windowsMakeShell()
{
    wchar_t directory[MAX_PATH];
    const UINT length = GetSystemDirectoryW(directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        throw CompileError(QCoreApplication::translate("ProjectCompiler", "Can't locate the Windows command shell."));
    const QString shell = QDir::fromNativeSeparators(QString::fromWCharArray(directory, length)) + "/cmd.exe";
    if (!QFileInfo(shell).isFile())
        throw CompileError(QCoreApplication::translate("ProjectCompiler", "Windows command shell '%1' doesn't exist.").arg(shell));
    return shell;
}
}
#endif

ProjectCompiler::ProjectCompiler(Compilation::ProjectBuildSnapshot snapshot):
    Compiler(snapshot.configuration, snapshot.capabilitiesKnown),
    mOnlyClean(false),
    mBuild(std::move(snapshot))
{
    mProtectedInputPaths.append(mBuild.projectPath);
    for (const auto &unit : mBuild.units)
        mProtectedInputPaths.append(unit.path);
}

void ProjectCompiler::buildMakeFile()
{
    //we are using custom make file, don't overwrite it
    if (mBuild.options.useCustomMakefile && !mBuild.options.customMakefile.isEmpty())
        return;
    switch(mBuild.options.type) {
    case ProjectType::StaticLib:
        createStaticMakeFile();
        break;
    case ProjectType::DynamicLib:
        createDynamicMakeFile();
        break;
    default:
        createStandardMakeFile();
    }
}

void ProjectCompiler::createStandardMakeFile()
{
    validateGeneratedOutputPath(mBuild.makefilePath);
    Compilation::GeneratedTextFile file(mBuild.makefilePath, configuration().toolchain.makerUsingUTF8);
    bool genModuleDef;
    newMakeFile(file, genModuleDef);
    QString executable = extractRelativePath(mBuild.makefilePath, mBuild.outputPath);
    QString exeTarget = escapeFilenameForMakefileTarget(executable);
    QString exeCommand = escapeArgumentForMakefileRecipe(executable, false);
    writeln(file, exeTarget + ": $(OBJ)\n");
    if (!mOnlyCheckSyntax) {
        if (mBuild.options.isCpp) {
            writeln(file, "\t$(CXX) $(LINKOBJ) -o " + exeCommand + " $(LIBS)");
        } else
            writeln(file, "\t$(CC) $(LINKOBJ) -o " + exeCommand + " $(LIBS)");
    }
    writeMakeObjFilesRules(file);
    checkCancellation();
    const auto committed = file.commit();
    if (!committed.succeeded()) throw CompileError(committed.error);
}

void ProjectCompiler::createStaticMakeFile()
{
    validateGeneratedOutputPath(mBuild.makefilePath);
    Compilation::GeneratedTextFile file(mBuild.makefilePath, configuration().toolchain.makerUsingUTF8);
    newMakeFile(file);
    QString libFilename = extractRelativePath(mBuild.makefilePath, mBuild.outputPath);
    QString libTarget = escapeFilenameForMakefileTarget(libFilename);
    QString libCommand = escapeArgumentForMakefileRecipe(libFilename, false);
    writeln(file, libTarget + ": $(OBJ)");
    writeln(file, "\tar r " + libCommand + " $(LINKOBJ)");
    writeln(file, "\tranlib " + libCommand);
    writeMakeObjFilesRules(file);
    checkCancellation();
    const auto committed = file.commit();
    if (!committed.succeeded()) throw CompileError(committed.error);
}

void ProjectCompiler::createDynamicMakeFile()
{
    validateGeneratedOutputPath(mBuild.makefilePath);
    Compilation::GeneratedTextFile file(mBuild.makefilePath, configuration().toolchain.makerUsingUTF8);
    bool genModuleDef;
    newMakeFile(file, genModuleDef);
    QString dynamicLibFilename = extractRelativePath(mBuild.makefilePath, mBuild.outputPath);
    QString dynamicLibTarget = escapeFilenameForMakefileTarget(dynamicLibFilename);
    QString dynamicLibCommand = escapeArgumentForMakefileRecipe(dynamicLibFilename, false);
    writeln(file, dynamicLibTarget + ": $(DEF) $(OBJ)");
    if (genModuleDef) {
        if (mBuild.options.isCpp) {
            writeln(file, "\t$(CXX) -mdll $(LINKOBJ) -o " + dynamicLibCommand + " $(LIBS) $(DEF) -Wl,--output-def,$(OUTPUT_DEF),--out-implib,$(STATIC)");
        } else {
            writeln(file, "\t$(CC) -mdll $(LINKOBJ) -o " + dynamicLibCommand + " $(LIBS) $(DEF) -Wl,--output-def,$(OUTPUT_DEF),--out-implib,$(STATIC)");
        }
    } else {
        if (mBuild.options.isCpp) {
            writeln(file, "\t$(CXX) -mdll $(LINKOBJ) -o " + dynamicLibCommand + " $(LIBS) $(DEF) -Wl,--out-implib,$(STATIC)");
        } else {
            writeln(file, "\t$(CC) -mdll $(LINKOBJ) -o " + dynamicLibCommand + " $(LIBS) $(DEF) -Wl,--out-implib,$(STATIC)");
        }
    }
    writeMakeObjFilesRules(file);
    checkCancellation();
    const auto committed = file.commit();
    if (!committed.succeeded()) throw CompileError(committed.error);
}

void ProjectCompiler::newMakeFile(Compilation::GeneratedTextFile &file)
{
    bool dummy;
    newMakeFile(file, dummy);
}

void ProjectCompiler::newMakeFile(Compilation::GeneratedTextFile &file, bool &genModuleDef)
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
    writeMakeDefines(file, genModuleDef);

    // Write PHONY and all targets
    writeMakeTarget(file);

    // Write list of includes
    writeMakeIncludes(file);

    // Write clean command
    writeMakeClean(file);

    // PCH
    if (mBuild.options.usePrecompiledHeader
            && fileExists(mBuild.options.precompiledHeader)) {
        QString pchH = extractRelativePath(mBuild.makefilePath, mBuild.options.precompiledHeader);
        QString pchHCommand = escapeArgumentForMakefileRecipe(pchH, false);
        QString pch = extractRelativePath(mBuild.makefilePath, mBuild.options.precompiledHeader + "." GCH_EXT);
        QString pchTarget = escapeFilenameForMakefileTarget(pch);
        QString pchCommand = escapeArgumentForMakefileRecipe(pch, false);
        writeln(file, pchTarget + ": $(PCH_H)");
        writeln(file, "\t$(CXX) -c " + pchHCommand + " -o " + pchCommand + " $(CXXFLAGS)");
        writeln(file);
    }
}

void ProjectCompiler::writeMakeHeader(Compilation::GeneratedTextFile &file)
{
    writeln(file,"# Project: " + mBuild.displayName);
    writeln(file,QString("# Makefile created by Red Panda C++ ") + REDPANDA_CPP_VERSION);
    writeln(file);
}

void ProjectCompiler::writeMakeDefines(Compilation::GeneratedTextFile &file, bool &genModuleDef)
{
    // Get list of object files
    QStringList objects;
    QStringList LinkObjects;
    QStringList cleanObjects;
    QStringList moduleDefines;

    genModuleDef = false;

    // Create a list of object files
    for (const auto &unit : mBuild.units) {

        // Only process source files
        FileType fileType = getFileType(unit.path);

        if (isC_CPP_ASMSourceFile(fileType)) {
            QString relativeName = extractRelativePath(mBuild.directory, unit.path);
            if (!mBuild.options.folderForObjFiles.isEmpty()) {
                // ofile = C:\MyProgram\obj\main.o
                QString fullObjFile = includeTrailingPathDelimiter(mBuild.options.folderForObjFiles)
                        + extractFileName(unit.path);
                QString relativeObjFile = extractRelativePath(mBuild.directory, changeFileExt(fullObjFile, OBJ_EXT));
                objects << relativeObjFile;
                cleanObjects << localizeMakefilePath(relativeObjFile);
                if (unit.link) {
                    LinkObjects << relativeObjFile;
                }
            } else {
                objects << changeFileExt(relativeName, OBJ_EXT);
                cleanObjects << localizeMakefilePath(changeFileExt(relativeName, OBJ_EXT));
                if (unit.link)
                    LinkObjects << changeFileExt(relativeName, OBJ_EXT);
            }
        }
        if (fileType == FileType::ModuleDef)
            moduleDefines.append(extractRelativePath(mBuild.makefilePath, unit.path));
    }
    // Get windres file
    QString objResFile;
    QString cleanRes;
#ifdef Q_OS_WIN
    if (!mBuild.options.privateResource.isEmpty()) {
        if (!mBuild.options.folderForObjFiles.isEmpty()) {
            QString fullResFile = includeTrailingPathDelimiter(mBuild.options.folderForObjFiles) +
                    changeFileExt(mBuild.options.privateResource, RES_EXT);

            QString relativeResFile = extractRelativePath(mBuild.directory, fullResFile);
            objResFile = relativeResFile;
            cleanRes = localizeMakefilePath(changeFileExt(relativeResFile, RES_EXT));
        } else {
            objResFile = changeFileExt(mBuild.options.privateResource, RES_EXT);
            cleanRes = localizeMakefilePath(changeFileExt(mBuild.options.privateResource, RES_EXT));
        }
    }
#endif

    // Mention progress in the logs
    if (!objResFile.isEmpty()) {
        QString absolutePath = generateAbsolutePath(mBuild.directory,objResFile);
        QString escaped = escapeArgumentForPlatformShell(absolutePath, false);
        log(tr("- Resource File: %1").arg(escaped));
    }
    log("");

    QString cxx = extractFileName(configuration().toolchain.cppCompiler);
    QString cc = extractFileName(configuration().toolchain.cCompiler);
#ifdef Q_OS_WIN
    QString windres = extractFileName(configuration().toolchain.resourceCompiler);
#endif

    // Get list of applicable flags
    QStringList cCompileArguments = getCCompileArguments(false);
    QStringList cxxCompileArguments = getCppCompileArguments(false);

    QStringList libraryArguments = getLibraryArguments(FileType::Project);
    QStringList cIncludeArguments = getCIncludeArguments();
    cIncludeArguments += getProjectIncludeArguments();
    QStringList cxxIncludeArguments = getCppIncludeArguments();
    cxxIncludeArguments += getProjectIncludeArguments();

#if defined(ARCH_X86_64) || defined(ARCH_X86)
    QStringList nasmArguments ;
#ifdef Q_OS_WIN
    if (cCompileArguments.contains("-m32") || QString("i686").compare(configuration().toolchain.target, Qt::CaseInsensitive)==0
            || QString("i386").compare(configuration().toolchain.target, Qt::CaseInsensitive)==0) {
        nasmArguments += {"-f","elf32"};
    } else if (cCompileArguments.contains("-m64")) {
        nasmArguments += {"-f","elf64"};
    } else {
        nasmArguments += {"-f","elf64"};
    }
#endif
    if (cCompileArguments.contains("-g3")) {
        nasmArguments += "-g";
    }
#endif

#ifdef Q_OS_WIN
    QStringList resourceArguments = parseArguments(mBuild.options.resourceCmd, macroVariables(), true);
#endif

    QString executable = extractRelativePath(mBuild.makefilePath, mBuild.outputPath);
    QString cleanExe = localizeMakefilePath(executable);
    QString pchHeader = extractRelativePath(mBuild.makefilePath, mBuild.options.precompiledHeader);
    QString pch = extractRelativePath(mBuild.makefilePath, mBuild.options.precompiledHeader + "." GCH_EXT);

    // Generated mingw32 recipes use cmd built-ins. An environment SHELL alone
    // does not stop Make from finding sh.exe on PATH.
#if defined(Q_OS_WIN) && MAKE_INTERFACE == MAKE_INTERFACE_mingw32
    const QString makeShell = windowsMakeShell();
    writeln(file, "SHELL := " + QString(makeShell).replace('$', "$$").replace('#', "\\#"));
    writeln(file, ".SHELLFLAGS := /d /c");
#endif

    // programs
    writeln(file, "CXX      = " + escapeArgumentForMakefileVariableValue(cxx, true));
    writeln(file, "CC       = " + escapeArgumentForMakefileVariableValue(cc, true));
#if defined(ARCH_X86_64) || defined(ARCH_X86)
    if (fileExists(configuration().preferences.nasmPath)) {
        writeln(file, "NASM       = " + escapeArgumentForMakefileVariableValue(configuration().preferences.nasmPath, true));
    }
#endif
#ifdef Q_OS_WIN
    writeln(file, "WINDRES  = " + escapeArgumentForMakefileVariableValue(windres, true));
#endif
    writeln(file, "RM       = " CLEAN_PROGRAM);
    writeln(file, "CD       = " CD_PROGRAM);

    // compiler flags
    writeln(file, "LIBS     = " + escapeArgumentsForMakefileVariableValue(libraryArguments));
    writeln(file, "INCS     = " + escapeArgumentsForMakefileVariableValue(cIncludeArguments));
    writeln(file, "CXXINCS  = " + escapeArgumentsForMakefileVariableValue(cxxIncludeArguments));
    writeln(file, "CXXFLAGS = $(CXXINCS) " + escapeArgumentsForMakefileVariableValue(cxxCompileArguments));
    writeln(file, "CFLAGS   = $(INCS) " + escapeArgumentsForMakefileVariableValue(cCompileArguments));

#if defined(ARCH_X86_64) || defined(ARCH_X86)
    writeln(file, "NASM_FLAGS   =  " + escapeArgumentsForMakefileVariableValue(nasmArguments));
#endif
#ifdef Q_OS_WIN
    writeln(file, "WINDRESFLAGS = " + escapeArgumentsForMakefileVariableValue(resourceArguments));
#endif

    // objects referenced in prerequisites
    // do not use them in targets or command arguments, they have different escaping rules
    if (!objResFile.isEmpty()) {
        writeln(file, "RES      = " + escapeFilenameForMakefilePrerequisite(objResFile));
        writeln(file, "OBJ      = " + escapeFilenamesForMakefilePrerequisite(objects) + " $(RES)");
    } else {
        writeln(file, "OBJ      = " + escapeFilenamesForMakefilePrerequisite(objects));
    };
    writeln(file, "BIN      = " + escapeFilenameForMakefilePrerequisite(executable));
    if (mBuild.options.usePrecompiledHeader
            && fileExists(mBuild.options.precompiledHeader)){
        writeln(file, "PCH_H    = " + escapeFilenameForMakefilePrerequisite(pchHeader));
        writeln(file, "PCH      = " + escapeFilenameForMakefilePrerequisite(pch));
    }

    // object referenced in command arguments
    // use them in targets or prerequisites, they have different escaping rules
    if (!objResFile.isEmpty()) {
        writeln(file, "LINKOBJ  = " + escapeArgumentsForMakefileVariableValue(LinkObjects) + " " + escapeArgumentForMakefileVariableValue(objResFile, false));
        writeln(file, "CLEANOBJ = " + escapeArgumentsForMakefileVariableValue(cleanObjects) + " " + escapeArgumentForMakefileVariableValue(cleanRes, false) + " " + escapeArgumentForMakefileVariableValue(cleanExe, false));
    } else {
        writeln(file, "LINKOBJ  = " + escapeArgumentsForMakefileVariableValue(LinkObjects));
        writeln(file, "CLEANOBJ = " + escapeArgumentsForMakefileVariableValue(cleanObjects) + " " + escapeArgumentForMakefileVariableValue(cleanExe, false));
    };

    // This needs to be put in before the clean command.
    if (mBuild.options.type == ProjectType::DynamicLib) {
        QString outputFileDir = extractFilePath(mBuild.outputPath);
        QString outputFilename = extractFileName(mBuild.outputPath);
        QString libOutputFile;
        if (!outputFilename.startsWith("lib")) {
            libOutputFile = includeTrailingPathDelimiter(outputFileDir) + "lib" + outputFilename;
        } else {
            libOutputFile = includeTrailingPathDelimiter(outputFileDir) + outputFilename;
        }
        if (QFileInfo(libOutputFile).absoluteFilePath()
                == mBuild.directory)
            libOutputFile = extractFileName(libOutputFile);
        else
            libOutputFile = extractRelativePath(mBuild.makefilePath, libOutputFile);

        QString defFile = localizeMakefilePath(changeFileExt(libOutputFile, DEF_EXT));
        QString staticFile = localizeMakefilePath(changeFileExt(libOutputFile, LIB_EXT));
        writeln(file,"DEF      = " + escapeFilenamesForMakefilePrerequisite(moduleDefines));
        writeln(file,"STATIC   = " + escapeFilenameForMakefilePrerequisite(staticFile));

        genModuleDef = !moduleDefines.contains(defFile);
        if (genModuleDef)
            writeln(file,"OUTPUT_DEF = " + escapeFilenameForMakefilePrerequisite(defFile));
    }
    writeln(file);
}

void ProjectCompiler::writeMakeTarget(Compilation::GeneratedTextFile &file)
{
    writeln(file, ".PHONY: all all-before all-after clean clean-custom");
    writeln(file);
    writeln(file, "all: all-before $(BIN) all-after");
    writeln(file);

}

void ProjectCompiler::writeMakeIncludes(Compilation::GeneratedTextFile &file)
{
    foreach(const QString& s, mBuild.options.makeIncludes) {
        writeln(file, "include " + escapeFilenameForMakefileInclude(s));
    }
    if (!mBuild.options.makeIncludes.isEmpty()) {
        writeln(file);
    }
}

void ProjectCompiler::writeMakeClean(Compilation::GeneratedTextFile &file)
{
    writeln(file, "clean: clean-custom");
    QString target="$(CLEANOBJ)";
    if (mBuild.options.usePrecompiledHeader
            && fileExists(mBuild.options.precompiledHeader)) {
        QString pch = extractRelativePath(mBuild.makefilePath, mBuild.options.precompiledHeader + "." GCH_EXT);
        target += ' ' + escapeArgumentForMakefileRecipe(pch, false);
    }

    if (mBuild.options.type == ProjectType::DynamicLib) {
        target +=" $(STATIC)";
    }
    writeln(file, QString("\t-$(RM) %1 >%2 2>&1").arg(target,NULL_FILE));
    writeln(file);
}

void ProjectCompiler::writeMakeObjFilesRules(Compilation::GeneratedTextFile &file)
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
        QString precompileStr;
        QString objStr = escapeFilenameForMakefilePrerequisite(shortFileName);
        // if we have scanned it, use scanned info
        const auto dependencies = mBuild.knownDependencies.constFind(unit.path);
        if (dependencies != mBuild.knownDependencies.cend()) {
            const auto &includedFiles = *dependencies;
            for (const auto &unit2 : projectUnits) {
                if (&unit2 == &unit)
                    continue;
                if (includedFiles.contains(unit2.path)) {
                    if (mBuild.options.usePrecompiledHeader &&
                           unit2.path == mBuild.options.precompiledHeader)
                        precompileStr = " $(PCH) ";
                    else {
                        QString prereq = extractRelativePath(mBuild.makefilePath, unit2.path);
                        objStr = objStr + ' ' + escapeFilenameForMakefilePrerequisite(prereq);
                    }
                }
            }
        } else {
            for (const auto &unit2 : projectUnits) {
                FileType fileType2 = getFileType(unit2.path);
                if (isC_CPPHeaderFile(fileType2)) {
                    QString prereq = extractRelativePath(mBuild.makefilePath, unit2.path);
                    objStr = objStr + ' ' + escapeFilenameForMakefilePrerequisite(prereq);
                }
            }
        }
        QString objFileNameTarget;
        QString objFileNameCommand;
        if (!mBuild.options.folderForObjFiles.isEmpty()) {
            QString fullObjname = includeTrailingPathDelimiter(mBuild.options.folderForObjFiles) +
                    extractFileName(unit.path);
            QString objectFile = extractRelativePath(mBuild.makefilePath, changeFileExt(fullObjname, OBJ_EXT));
            objFileNameTarget = escapeFilenameForMakefileTarget(objectFile);
            objFileNameCommand = escapeArgumentForMakefileRecipe(objectFile, false);
        } else {
            QString objectFile = changeFileExt(shortFileName, OBJ_EXT);
            objFileNameTarget = escapeFilenameForMakefileTarget(objectFile);
            objFileNameCommand = escapeArgumentForMakefileRecipe(objectFile, false);
        }

        objStr = objFileNameTarget + ": " + objStr + precompileStr;

        writeln(file,objStr);

        // Write custom build command
        if (unit.overrideBuildCommand && !unit.buildCommand.isEmpty()) {
            QString BuildCmd = unit.buildCommand;
            BuildCmd.replace("<CRTAB>", "\n\t");
            writeln(file, '\t' + BuildCmd);
            // Or roll our own
        } else {
            QString encodingStr;
            if (configuration().toolchain.supportConvertingCharset && mBuild.options.addCharset) {
                const QByteArray defaultSystemEncoding = configuration().preferences.systemEncoding;
                QByteArray encoding = mBuild.options.execEncoding;
                QByteArray targetEncoding;
                QByteArray sourceEncoding;
                if ( encoding == ENCODING_SYSTEM_DEFAULT || encoding.isEmpty()) {
                    targetEncoding = defaultSystemEncoding;
                } else if (encoding == ENCODING_OEM_DEFAULT) {
                    targetEncoding = configuration().preferences.consoleEncoding;
                } else if (encoding == ENCODING_UTF8_BOM) {
                    targetEncoding = "UTF-8";
                } else if (encoding == ENCODING_UTF16_BOM) {
                    targetEncoding = "UTF-16";
                } else if (encoding == ENCODING_UTF32_BOM) {
                    targetEncoding = "UTF-32";
                } else {
                    targetEncoding = encoding;
                }

                const QByteArray capturedSource = unit.sourceEncoding;
                if (capturedSource.isEmpty() || capturedSource == ENCODING_ASCII) {
                    sourceEncoding = targetEncoding;
                } else {
                    sourceEncoding = capturedSource;
                }
                if (sourceEncoding==ENCODING_SYSTEM_DEFAULT)
                    sourceEncoding = defaultSystemEncoding;

                if (QString::compare(sourceEncoding,targetEncoding,Qt::CaseInsensitive)!=0) {
                    encodingStr = QString(" -finput-charset=%1 -fexec-charset=%2")
                            .arg(QString(sourceEncoding),
                                 QString(targetEncoding));
                }
            }
            if (isC_CPPSourceFile(fileType)) {
                if (unit.compileAsCpp)
                    writeln(file, "\t$(CXX) -c " + escapeArgumentForMakefileRecipe(shortFileName, false) + " -o " + objFileNameCommand + " $(CXXFLAGS) " + encodingStr);
                else
                    writeln(file, "\t$(CC) -c " + escapeArgumentForMakefileRecipe(shortFileName, false) + " -o " + objFileNameCommand + " $(CFLAGS) " + encodingStr);
            } else if (fileType == FileType::GAS) {
                writeln(file, "\t$(CC) -c " + escapeArgumentForMakefileRecipe(shortFileName, false) + " -o " + objFileNameCommand + " $(CFLAGS) " + encodingStr);
            } else if (fileType == FileType::NASM) {
                writeln(file, "\t$(NASM) " + escapeArgumentForMakefileRecipe(shortFileName, false) + " -o " + objFileNameCommand + " $(NASM_FLAGS) " );
            }
        }
    }

#ifdef Q_OS_WIN
    if (!mBuild.options.privateResource.isEmpty()) {
        // Concatenate all resource include directories
        QString ResIncludes(" ");
        for (int i=0;i<mBuild.options.resourceIncludes.count();i++) {
            QString filename = mBuild.options.resourceIncludes[i];
            if (!filename.isEmpty())
                ResIncludes += " --include-dir " + escapeArgumentForMakefileRecipe(filename, false);
        }

        QString resFiles;
        // Concatenate all resource filenames (not created when syntax checking)
        for (const auto &unit : mBuild.units) {
            if (getFileType(unit.path)!=FileType::WindowsResourceSource)
                continue;
            if (fileExists(unit.path)) {
                QString ResFile = extractRelativePath(mBuild.makefilePath, unit.path);
                resFiles = resFiles + escapeFilenameForMakefilePrerequisite(ResFile) + ' ';
            }
        }

        // Determine resource output file
        QString fullName;
        if (!mBuild.options.folderForObjFiles.isEmpty()) {
            fullName = includeTrailingPathDelimiter(mBuild.options.folderForObjFiles) +
                  changeFileExt(mBuild.options.privateResource, RES_EXT);
        } else {
            fullName = changeFileExt(mBuild.options.privateResource, RES_EXT);
        }
        QString objFile = extractRelativePath(mBuild.projectPath, fullName);
        QString objFileNameCommand = escapeArgumentForMakefileRecipe(objFile, false);
        QString objFileNameTarget = escapeFilenameForMakefileTarget(objFile);
        QString privRes = extractRelativePath(mBuild.projectPath, mBuild.options.privateResource);
        QString privResNameCommand = escapeArgumentForMakefileRecipe(privRes, false);
        QString privResNamePrereq = escapeFilenameForMakefilePrerequisite(privRes);

        // Build final cmd
        QString windresArgs;

        if (mBuild.options.compilerOptions.value(CC_CMD_OPT_POINTER_SIZE)=="32")
              windresArgs = " -F pe-i386";

        writeln(file);
        writeln(file, objFileNameTarget + ": " + privResNamePrereq + ' ' + resFiles);
        writeln(file, "\t$(WINDRES) -i " + privResNameCommand + windresArgs + " --input-format=rc -o " + objFileNameCommand + " -O coff $(WINDRESFLAGS)"
            + ResIncludes);
        writeln(file);
    }
#endif
}

void ProjectCompiler::writeln(Compilation::GeneratedTextFile &file, const QString &s)
{
    checkCancellation();
    const auto written = file.appendLine(s);
    if (!written.succeeded()) throw CompileError(written.error);
}

bool ProjectCompiler::onlyClean() const
{
    return mOnlyClean;
}

void ProjectCompiler::setOnlyClean(bool newOnlyClean)
{
    mOnlyClean = newOnlyClean;
}

bool ProjectCompiler::prepareForRebuild()
{
    //we use make argument to clean
    return true;
}

bool ProjectCompiler::prepareForCompile()
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

    QString makefile = extractRelativePath(mBuild.directory, mBuild.makefilePath);
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
#if defined(Q_OS_WIN) && MAKE_INTERFACE == MAKE_INTERFACE_mingw32
    if (!mBuild.options.useCustomMakefile || mBuild.options.customMakefile.isEmpty()) {
        // Command-line values also protect generated recipes from inherited
        // MAKEFLAGS or make-includes that select an incompatible shell.
        const QStringList shellArguments{
            "SHELL=" + windowsMakeShell().replace('$', "$$"),
            ".SHELLFLAGS=/d /c",
        };
        cleanArgs += shellArguments;
        makeAllArgs += shellArguments;
    }
#endif
    if (mOnlyClean) {
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
