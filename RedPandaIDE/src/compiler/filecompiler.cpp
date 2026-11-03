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
#include "filecompiler.h"
#include "../utils.h"
#include "compilermanager.h"
#include "qsynedit/syntaxer/asm.h"
#include "../systemconsts.h"

#include <QFile>
#include <QFileInfo>
#include <QMessageBox>


FileCompiler::FileCompiler(Compilation::FileCompileRequest::Pointer request):
    Compiler(request), mEncoding(request->values().sourceEncoding),
    mCompileType(request->values().compileType), mFileType(request->values().fileType)
{
}

bool FileCompiler::prepareForCompile()
{
    CompilerSet::CompilationStage stage = CompilerSet::CompilationStage::GenerateExecutable;
    switch(mCompileType) {
    case CppCompileType::PreprocessOnly:
        stage = CompilerSet::CompilationStage::PreprocessingOnly;
        break;
    case CppCompileType::GenerateAssemblyOnly:
        stage = CompilerSet::CompilationStage::CompilationProperOnly;
        mSuppressDebugInfo = configuration().preferences.noDebugDirectivesWhenGenerateASM;
        break;
    case CppCompileType::GenerateGimpleOnly:
        stage = CompilerSet::CompilationStage::GenerateGimple;
        break;
    default:
        break;
    }
    if (mOnlyCheckSyntax) {
        log(tr("Checking single file..."));
    } else {
        log(tr("Compiling single file..."));
    }
    log("------------------");
    log(tr("- Filename: %1").arg(mFilename));
    log(tr("- Compiler Set Name: %1").arg(configuration().toolchain.name));
    log("");

    CompilerType compilerType = configuration().toolchain.compilerType;

    // GCC `import std;` sources should be added before the main file to generate GCM cache
    if (mFileType == FileType::CppSource &&
        (compilerType == CompilerType::GCC)) {
        mArguments += getCppGccImportStdSources(mOnlyCheckSyntax);
    }

    mArguments += QStringList{localizePath(fileRequest()->values().sourcePath)};
    if (!mOnlyCheckSyntax) {
        switch(stage) {
        case CompilerSet::CompilationStage::PreprocessingOnly:
            mOutputFile=changeFileExt(fileRequest()->values().sourcePath,configuration().toolchain.preprocessingSuffix);
            mArguments << "-E";
            break;
        case CompilerSet::CompilationStage::CompilationProperOnly:
            mOutputFile=changeFileExt(fileRequest()->values().sourcePath,configuration().toolchain.compilationProperSuffix);
            mArguments += {"-S", "-fverbose-asm"};
            break;
        case CompilerSet::CompilationStage::GenerateGimple:
            mOutputFile=changeFileExt(fileRequest()->values().sourcePath,configuration().toolchain.compilationProperSuffix);
            validateGeneratedOutputPath(changeFileExt(fileRequest()->values().sourcePath,"gimple"));
            mArguments += {"-S", QString("-fdump-tree-gimple=%1").arg(localizePath(changeFileExt(fileRequest()->values().sourcePath,"gimple")))};
            break;
        case CompilerSet::CompilationStage::AssemblingOnly:
            mOutputFile=changeFileExt(fileRequest()->values().sourcePath,configuration().toolchain.assemblingSuffix);
            mArguments << "-c";
            break;
        case CompilerSet::CompilationStage::GenerateExecutable:
            mOutputFile = changeFileExt(fileRequest()->values().sourcePath,configuration().toolchain.executableSuffix);
        }
#ifdef ENABLE_SDCC
        if (configuration().toolchain.compilerType==CompilerType::SDCC) {
            if (configuration().toolchain.executableSuffix==SDCC_IHX_SUFFIX) {

            }
        }
#endif
        mOutputFile = localizePath(mOutputFile);
        validateGeneratedOutputPath(mOutputFile);
        mArguments += {"-o",  mOutputFile};

#if defined(ARCH_X86_64) || defined(ARCH_X86)
        if (mCompileType == CppCompileType::GenerateAssemblyOnly) {
            if (configuration().preferences.noSEHDirectivesWhenGenerateASM)
                mArguments << "-fno-asynchronous-unwind-tables";
            if (configuration().preferences.intelASMDialect)
                mArguments << "-masm=intel";
        }
#endif
        // Preserve the previous artifact during preparation and cancellation.
        // The external compiler owns replacement after its process is launched.
    }

    mArguments += getCharsetArgument(mEncoding, mFileType, mOnlyCheckSyntax);
    QString strFileType;
    switch(mFileType) {
    case FileType::GAS:
        mArguments += getCCompileArguments(mOnlyCheckSyntax);
        mArguments += getCIncludeArguments();
        mArguments += getProjectIncludeArguments();
        if (configuration().preferences.gasLinkCStandardLib) {
            mArguments.removeAll("-nostdlib");
        } else {
            mArguments += "-nostdlib";
        }
        strFileType = tr("GNU Assembler");
        mCompiler = configuration().toolchain.cCompiler;
        break;
    case FileType::CSource:
        mArguments += getCCompileArguments(mOnlyCheckSyntax);
        mArguments += getCIncludeArguments();
        mArguments += getProjectIncludeArguments();
        strFileType = "C";
        mCompiler = configuration().toolchain.cCompiler;
        break;
    case FileType::CppSource:
        mArguments += getCppCompileArguments(mOnlyCheckSyntax);
        mArguments += getCppIncludeArguments();
        mArguments += getProjectIncludeArguments();
        strFileType = "C++";
        mCompiler = configuration().toolchain.cppCompiler;
        break;
    default:
        throw CompileError(tr("Can't find the compiler for file %1").arg(mFilename));
    }
    if (!mOnlyCheckSyntax)
        mArguments += getLibraryArguments(mFileType);

//    if (isASMSourceFile(fileType)) {
//        bool hasStart=false;
//        QStringList lines=readFileToLines(mFilename);
//        QSynedit::ASMSyntaxer syntaxer;
//        syntaxer.resetState();
//        QString lastToken;
//        QString token;
//        QSynedit::PTokenAttribute attr;
//        for (int i=0;i<lines.count();i++) {
//            QString line=lines[i];
//            syntaxer.setLine(line,i+1);
//            lastToken="";
//            while(!syntaxer.eol()) {
//                token=syntaxer.getToken();
//                if (token==":" && lastToken=="_start") {
//                    hasStart=true;
//                    break;
//                }
//                attr = syntaxer.getTokenAttribute();
//                if (attr->tokenType() != QSynedit::TokenType::Space
//                        && attr->tokenType()!=QSynedit::TokenType::String
//                        && attr->tokenType()!=QSynedit::TokenType::Character)
//                    lastToken=token;
//                syntaxer.next();
//            }
//            if (hasStart)
//                break;
//        }
//        if (hasStart) {
//            mArguments << "-nostartfiles";
//        }
//    }

    if (!fileExists(mCompiler)) {
        throw CompileError(
                    tr("The Compiler '%1' doesn't exists!").arg(mCompiler)
                    +"<br />"
                    +tr("Please check the \"program\" page of compiler settings."));
    }

    log(tr("Processing %1 source file:").arg(strFileType));
    log("------------------");
    log(tr("%1 Compiler: %2").arg(strFileType,mCompiler));
    QString command = escapeCommandForLog(mCompiler, mArguments);
    log(tr("Command: %1").arg(command));
    mDirectory = fileRequest()->values().workingDirectory;
    return true;
}

bool FileCompiler::prepareForRebuild()
{
    // Direct file compilation already rebuilds the complete translation unit.
    // Do not delete a confirmed output before the new process starts.
    return true;
}
