/// \file main.cpp
/// \brief This file takes care of handling command-line parameters and loading
/// the appropriate flavour of libtinycode-*.so

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

// Standard includes
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

extern "C"
{
#include <dlfcn.h>
#include <libgen.h>
#include <unistd.h>
}

// LLVM includes
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELF.h"

// Local includes
#include "argparse.h"
#include "binaryfile.h"
#include "codegenerator.h"
#include "debug.h"
#include "ptcinterface.h"
#include "revamb.h"

PTCInterface ptc = {}; ///< The interface with the PTC library.
static std::string LibTinycodePath;
static std::string LibHelpersPath;

// 程序参数结构体
struct ProgramParameters { 
  const char *InputPath;     // 输入文件路径
  const char *OutputPath;    // 输出文件路径
  size_t EntryPointAddress;  // 入口地址
  DebugInfoType DebugInfo;   // 调试信息类型
  const char *DebugPath;     // 调试信息路径
  const char *LinkingInfoPath;  // 链接信息路径
  const char *CoveragePath;     // 覆盖率路径
  const char *BBSummaryPath;    // 基本块摘要路径
  bool NoOSRA;               // 是否使用OSRA编译器
  bool UseSections;          // 是否使用段
  bool DetectFunctionsBoundaries;  // 是否检测函数边界
  bool NoLink;               // 是否取消链接
  bool External;             // 是否为外部文件
};

using LibraryDestructor = GenericFunctor<decltype(&dlclose), &dlclose>;
using LibraryPointer = std::unique_ptr<void, LibraryDestructor>;

static const char *const Usage[] = {
    "revamb [options] [--] INFILE OUTFILE",
    nullptr,
};

/// 寻找 QEMU 的库
/// 通过检查路径找到与指定CPU架构对应的 libtinycode 和libtinycode-helpers库的路径
static void findQemu(const char *Architecture)
{
    // TODO: make this optional
    // 通过realpath函数获取当前执行程序的绝对路径
    char *FullPath = realpath("/proc/self/exe", nullptr);
    assert(FullPath != nullptr);
    // 通过realpath函数获取当前执行程序的绝对路径
    std::string Directory(dirname(FullPath));
    free(FullPath);

    // TODO: add other search paths?
    std::vector<std::string> SearchPaths;
// 检查是否定义宏，有则将宏路径作为搜索路径（+ "/lib"）
#ifdef QEMU_INSTALL_PATH
    SearchPaths.push_back(std::string(QEMU_INSTALL_PATH) + "/lib");
#endif
#ifdef INSTALL_PATH
    SearchPaths.push_back(std::string(INSTALL_PATH) + "/lib");
#endif
    SearchPaths.push_back(Directory + "/../lib");

    for (auto &Path : SearchPaths)
    {
         // 在搜索路径上构造libtinycode和libtinycode-helpers库的路径
        std::stringstream LibraryPath; // 动态链接库路径
        LibraryPath << Path << "/libtinycode-" << Architecture << ".so";
        std::stringstream HelpersPath; // helper函数路径
        HelpersPath << Path << "/libtinycode-helpers-" << Architecture << ".ll";
        // access函数检查文件是否存在
        if (access(LibraryPath.str().c_str(), F_OK) != -1 && access(HelpersPath.str().c_str(), F_OK) != -1)
        {
            // 把路径保存在全局变量LibTinycodePath和LibHelpersPath中
            LibTinycodePath = LibraryPath.str(); // TCG lib路径
            LibHelpersPath = HelpersPath.str();  // helper lib路径
            return;
        }
    }

    assert(false && "Couldn't find libtinycode and the helpers");
}

/// 给出一个体系结构名字，加载合适的 PTC 版本库，
/// 并初始化 PTC 接口
/// Given an architecture name, loads the appropriate version of the PTC library,
/// and initializes the PTC interface.
///
/// \param Architecture 体系结构名称 the name of the architecture, e.g. "arm".
/// \param PTCLibrary PTC 库的引用 a reference to the library handler.
///
/// \return EXIT_SUCCESS 如果 PTC 库被成功加载 if the library has been successfully loaded.
static int loadPTCLibrary(LibraryPointer &PTCLibrary)
{
    ptc_load_ptr_t ptc_load = nullptr;
    void *LibraryHandle = nullptr;

    // 在系统路径中查找库
    LibraryHandle = dlopen(LibTinycodePath.c_str(), RTLD_LAZY);

    if (LibraryHandle == nullptr)
    {
        fprintf(stderr, "Couldn't load the PTC library: %s\n", dlerror());
        return EXIT_FAILURE;
    }

    // The library has been loaded, initialize the pointer, the caller will take
    // care of dlclose it from now on
    // 加载库，初始化指针
    PTCLibrary.reset(LibraryHandle);

    // Obtain the address of the ptc_load entry point
    // 获取 ptc_load 入口点的地址
    ptc_load = (ptc_load_ptr_t)dlsym(LibraryHandle, "ptc_load");
    
    if (ptc_load == nullptr)
    {
        fprintf(stderr, "Couldn't find ptc_load: %s\n", dlerror());
        return EXIT_FAILURE;
    }

    // Initialize the ptc interface
    // 初始化ptc接口
    if (ptc_load(LibraryHandle, &ptc) != 0)
    {
        fprintf(stderr, "Couldn't find PTC functions.\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/// 在程序中解析输入参数
/// Parses the input arguments to the program.
///
/// \param Argc 参数的数量 number of arguments.
/// \param Argv 包含参数的string数组 array of strings containing the arguments.
/// \param Parameters 存放待解析参数值 where to store the parsed parameters.
///
/// \return EXIT_SUCCESS 如果参数顺利被解析 if the parameters have been successfully parsed.
static int parseArgs(int Argc, const char *Argv[],
                     ProgramParameters *Parameters)
{
    const char *DebugString = nullptr;
    const char *DebugLoggingString = nullptr;
    const char *EntryPointAddressString = nullptr;
    long long EntryPointAddress = 0;

    // 初始化参数解析器
    struct argparse Arguments;
    struct argparse_option Options[] = {
        OPT_HELP(),
        OPT_GROUP("Input description"),
        OPT_STRING('e', "entry",
                   &EntryPointAddressString,
                   "virtual address of the entry point where to start."),
        OPT_STRING('s', "debug-path",
                   &Parameters->DebugPath,
                   "destination path for the generated debug source."),
        OPT_STRING('c', "coverage-path",
                   &Parameters->CoveragePath,
                   "destination path for the CSV containing translated ranges."),
        OPT_STRING('i', "linking-info",
                   &Parameters->LinkingInfoPath,
                   "destination path for the CSV containing linking info."),
        OPT_STRING('g', "debug-info",
                   &DebugString,
                   "emit debug information. Possible values are 'none' for no debug"
                   " information, 'asm' for debug information referring to the"
                   " assembly of the input file, 'ptc' for debug information"
                   " referred to the Portable Tiny Code, or 'll' for debug"
                   " information referred to the LLVM IR."),
        OPT_STRING('d', "debug",
                   &DebugLoggingString,
                   "enable verbose logging."),
        OPT_BOOLEAN('O', "no-osra", &Parameters->NoOSRA,
                    "disable OSRA."),
        OPT_BOOLEAN('L', "no-link", &Parameters->NoLink,
                    "do not link the output to QEMU helpers."),
        OPT_BOOLEAN('E', "external", &Parameters->External,
                    "set CSVs linkage to external, useful for debugging purposes."),
        OPT_BOOLEAN('S', "use-sections", &Parameters->UseSections,
                    "use section informations, if available."),
        OPT_STRING('b', "bb-summary",
                   &Parameters->BBSummaryPath,
                   "destination path for the CSV containing the statistics about "
                   "the translated basic blocks."),
        OPT_BOOLEAN('f', "functions-boundaries",
                    &Parameters->DetectFunctionsBoundaries,
                    "enable functions boundaries detection."),
        OPT_END(),
    };

    argparse_init(&Arguments, Options, Usage, 0);
    argparse_describe(&Arguments, "\nrevamb.",
                      "\nTranslates a binary into a program for a different "
                      "architecture.\n");
    Argc = argparse_parse(&Arguments, Argc, Argv);

    // Handle positional arguments
    if (Argc != 2)
    {
        fprintf(stderr, "Too many arguments.\n");
        return EXIT_FAILURE;
    }

    Parameters->InputPath = Argv[0];
    Parameters->OutputPath = Argv[1];

    // Check parameters
    if (EntryPointAddressString != nullptr)
    {
        if (sscanf(EntryPointAddressString, "%lld", &EntryPointAddress) != 1)
        {
            fprintf(stderr, "Entry point parameter (-e, --entry) is not a"
                            " number.\n");
            return EXIT_FAILURE;
        }

        Parameters->EntryPointAddress = (size_t)EntryPointAddress;
    }

    if (DebugString != nullptr)
    {
        if (strcmp("none", DebugString) == 0)
        {
            Parameters->DebugInfo = DebugInfoType::None;
        }
        else if (strcmp("asm", DebugString) == 0)
        {
            Parameters->DebugInfo = DebugInfoType::OriginalAssembly;
        }
        else if (strcmp("ptc", DebugString) == 0)
        {
            Parameters->DebugInfo = DebugInfoType::PTC;
        }
        else if (strcmp("ll", DebugString) == 0)
        {
            Parameters->DebugInfo = DebugInfoType::LLVMIR;
        }
        else
        {
            fprintf(stderr, "Unexpected value for the debug type parameter"
                            " (-g, --debug).\n");
            return EXIT_FAILURE;
        }
    }

    if (DebugLoggingString != nullptr)
    {
        DebuggingEnabled = true;
        std::string Input(DebugLoggingString);
        std::stringstream Stream(Input);
        std::string Type;
        while (std::getline(Stream, Type, ','))
            enableDebugFeature(Type.c_str());
    }

    if (Parameters->DebugPath == nullptr)
        Parameters->DebugPath = "";

    if (Parameters->LinkingInfoPath == nullptr)
        Parameters->LinkingInfoPath = "";

    if (Parameters->CoveragePath == nullptr)
        Parameters->CoveragePath = "";

    if (Parameters->BBSummaryPath == nullptr)
        Parameters->BBSummaryPath = "";

    return EXIT_SUCCESS;
}

// 主程序入口
int main(int argc, const char *argv[])
{
    // 1. 解析参数 Parse arguments
    ProgramParameters Parameters{};
    if (parseArgs(argc, argv, &Parameters) != EXIT_SUCCESS)
        return EXIT_FAILURE;

    // 2. 读取二进制文件 && 查找 QEMU
    BinaryFile TheBinary(Parameters.InputPath, Parameters.UseSections);

    findQemu(TheBinary.architecture().name());

    // 3. 加载合适版本的libtyncode库 Load the appropriate libtyncode version
    LibraryPointer PTCLibrary;  
    if (loadPTCLibrary(PTCLibrary) != EXIT_SUCCESS)
        return EXIT_FAILURE;

    Architecture TargetArchitecture;
    // 4. 初始化代码生成器对象    
    CodeGenerator Generator(TheBinary,
                            TargetArchitecture,
                            std::string(Parameters.OutputPath),
                            LibHelpersPath,
                            Parameters.DebugInfo,
                            std::string(Parameters.DebugPath),
                            std::string(Parameters.LinkingInfoPath),
                            std::string(Parameters.CoveragePath),
                            std::string(Parameters.BBSummaryPath),
                            !Parameters.NoOSRA,
                            Parameters.DetectFunctionsBoundaries,
                            !Parameters.NoLink,
                            Parameters.External);

    // 5. 翻译中间代码
    Generator.translate(Parameters.EntryPointAddress);
    // 6.将结果序列化
    Generator.serialize();
    // 7.程序结束
    return EXIT_SUCCESS;
}
