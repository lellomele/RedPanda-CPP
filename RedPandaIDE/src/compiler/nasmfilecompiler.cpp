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
#include "nasmfilecompiler.h"
#include "../utils.h"
#include "compilermanager.h"
#include "../systemconsts.h"

#include <QFile>
#include <QFileInfo>
#include <QMessageBox>


NASMFileCompiler::NASMFileCompiler(Compilation::FileCompileRequest::Pointer request): Compiler(request)
{
}

bool NASMFileCompiler::prepareForCompile()
{
    if (mOnlyCheckSyntax) {
        throw CompileError(tr("NASM does not support this syntax-check request."));
    }
    log(tr("Compiling single file..."));
    log("------------------");
    log(tr("- Filename: %1").arg(mFilename));
    log(tr("- Compiler Set Name: %1").arg(configuration().toolchain.name));
    log("");

    QString strFileType = "C";
    mCompiler = configuration().preferences.nasmPath;

    QStringList arguments = getCCompileArguments(false);
#ifdef Q_OS_WIN
    if (arguments.contains("-m32") || QString("i686").compare(configuration().toolchain.target, Qt::CaseInsensitive)==0
            || QString("i386").compare(configuration().toolchain.target, Qt::CaseInsensitive)==0) {
        mArguments += {"-f","elf32"};
    } else if (arguments.contains("-m64")) {
        mArguments += {"-f","elf64"};
    } else {
        mArguments += {"-f","elf64"};
    }
#endif
    if (arguments.contains("-g3")) {
        mArguments += "-g";
    }


    if (!fileExists(mCompiler)) {
        throw CompileError(
                    tr("The NASM '%1' doesn't exists!").arg(mCompiler)
                    +"<br />"
                    +tr("Please check NASM settings."));
    }

    mOutputFile=changeFileExt(fileRequest()->values().sourcePath, configuration().toolchain.executableSuffix);
    mObjFilename= changeFileExt(fileRequest()->values().sourcePath, DEFAULT_ASSEMBLING_SUFFIX );
    validateGeneratedOutputPath(mOutputFile);
    validateGeneratedOutputPath(mObjFilename);

    mArguments += {fileRequest()->values().sourcePath, "-o", mObjFilename};
    mExtraCompilersList << configuration().toolchain.cCompiler;
    QStringList args = getLibraryArguments(FileType::CSource);
    if (configuration().preferences.nasmLinkCStandardLib) {
        args.removeAll("-nostdlib");
    } else {
        args += "-nostdlib";
    }
    args += {mObjFilename, "-o", mOutputFile};
    mExtraArgumentsList << args;
    mExtraOutputFilesList << QString();
    log(tr("Processing %1 source file:").arg(strFileType));
    log("------------------");
    log(tr("- %1 Compiler: %2").arg(strFileType, mCompiler));
    QString command = escapeCommandForLog(mCompiler, mArguments);
    log(tr("- Command: %1").arg(command));
    mDirectory = fileRequest()->values().workingDirectory;
    mStartCompileTime = QDateTime::currentDateTime();
    return true;
}

bool NASMFileCompiler::beforeRunExtraCommand(int idx)
{
    if (idx==0) {
        QFileInfo file(mObjFilename);
        return file.exists() && (file.lastModified()>mStartCompileTime);
    }
    return true;
}

bool NASMFileCompiler::prepareForRebuild()
{
    // Direct assembler/compiler invocations regenerate their own artifacts.
    // Cancellation before launch retains the previous confirmed files.
    return true;
}
