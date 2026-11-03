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
#include "sdccfilecompiler.h"
#include "../utils.h"
#include "compilermanager.h"
#include "../systemconsts.h"

#include <QFile>
#include <QFileInfo>
#include <QMessageBox>


SDCCFileCompiler::SDCCFileCompiler(Compilation::FileCompileRequest::Pointer request):
    Compiler(request), mEncoding(request->values().sourceEncoding),
    mCompileType(request->values().compileType), mNoStartup(false)
{
}

bool SDCCFileCompiler::prepareForCompile()
{
    if (mOnlyCheckSyntax) {
        mCompiler = configuration().toolchain.cCompiler;
        mArguments += getCCompileArguments(false);
        mArguments += getCIncludeArguments();
        mArguments += getProjectIncludeArguments();
        mArguments += {"--syntax-only", fileRequest()->values().sourcePath};
        mDirectory = fileRequest()->values().workingDirectory;
        return true;
    }
    log(tr("Compiling single file..."));
    log("------------------");
    log(tr("- Filename: %1").arg(mFilename));
    log(tr("- Compiler Set Name: %1").arg(configuration().toolchain.name));
    log("");

    QString strFileType = "C";
    mCompiler = configuration().toolchain.cCompiler;
    mArguments += getCCompileArguments(false);
    mArguments += getCIncludeArguments();
    mArguments += getProjectIncludeArguments();

    if (!fileExists(mCompiler)) {
        throw CompileError(
                    tr("The Compiler '%1' doesn't exists!").arg(mCompiler)
                    +"<br />"
                    +tr("Please check the \"program\" page of compiler settings."));
    }

    mOutputFile=changeFileExt(fileRequest()->values().sourcePath, configuration().toolchain.executableSuffix);
    mIhxFilename = changeFileExt(fileRequest()->values().sourcePath,SDCC_IHX_SUFFIX);
    validateGeneratedOutputPath(mOutputFile);
    validateGeneratedOutputPath(mIhxFilename);

    QString val = configuration().toolchain.compileOptions.value(SDCC_OPT_NOSTARTUP);
    mNoStartup = (val==COMPILER_OPTION_ON);
    if (mNoStartup) {
        mRelFilename = changeFileExt(fileRequest()->values().sourcePath,SDCC_REL_SUFFIX);
        validateGeneratedOutputPath(mRelFilename);
        mArguments += {"-c", fileRequest()->values().sourcePath};
        mExtraCompilersList << mCompiler;
        QStringList args = getLibraryArguments(FileType::CSource);
        args += {"-o", mIhxFilename, mRelFilename};
        mExtraArgumentsList << args;
        mExtraOutputFilesList << "";
    } else {
        mArguments += getLibraryArguments(FileType::CSource);
        mArguments += {fileRequest()->values().sourcePath, "-o", mIhxFilename};
    }

    if (configuration().toolchain.executableSuffix == SDCC_HEX_SUFFIX) {
        QString packihx = configuration().toolchain.additionalPrograms.value(PACKIHX_PROGRAM);
        if (packihx.isEmpty()) {
            error(tr("Can't find \"%1\".\n").arg(PACKIHX_PROGRAM));
            return false;
        }
        mExtraCompilersList.append(packihx);
        QStringList args{mIhxFilename};
        mExtraArgumentsList << args;
        mExtraOutputFilesList << mOutputFile;
    } else if (configuration().toolchain.executableSuffix == SDCC_BIN_SUFFIX) {
        QString makebin = configuration().toolchain.additionalPrograms.value(MAKEBIN_PROGRAM);
        if (makebin.isEmpty()) {
            error(tr("Can't find \"%1\".\n").arg(PACKIHX_PROGRAM));
            return false;
        }
        mExtraCompilersList << makebin;
        QStringList args{mIhxFilename, mOutputFile};
        mExtraArgumentsList << args;
        mExtraOutputFilesList << "";
    }

    log(tr("Processing %1 source file:").arg(strFileType));
    log("------------------");
    log(tr("- %1 Compiler: %2").arg(strFileType, mCompiler));
    QString command = escapeCommandForLog(mCompiler, mArguments);
    log(tr("- Command: %1").arg(command));
    mDirectory = fileRequest()->values().workingDirectory;
    mStartCompileTime = QDateTime::currentDateTime();
    return true;
}

bool SDCCFileCompiler::beforeRunExtraCommand(int idx)
{
    if (mNoStartup) {
        if (idx==0) {
            QFileInfo file(mRelFilename);
            return file.exists() && (file.lastModified()>mStartCompileTime);
        } else if (idx==1) {
            QFileInfo file(mIhxFilename);
            return file.exists() && (file.lastModified()>mStartCompileTime);
        }
    } else {
        if (idx==0) {
            QFileInfo file(mIhxFilename);
            return file.exists() && (file.lastModified()>mStartCompileTime);
        }
    }
    return true;
}

bool SDCCFileCompiler::prepareForRebuild()
{
    // Direct assembler/compiler invocations regenerate their own artifacts.
    // Cancellation before launch retains the previous confirmed files.
    return true;
}
