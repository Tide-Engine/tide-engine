// tide's built-in C compiler: clang and lld, linked into tide (see
// cmake/LLVM.cmake), so users need no compiler of their own.
//
// `tide cc <clang arguments>` works like clang: its driver runs in this
// process, compiles in it (as clang does by default), and links with lld in it
// too, instead of running a linker program, but for a Mac's own programs,
// which Apple's linker links (link_apple). clang's own main isn't in a
// library, so this is a small version of it (clang/tools/driver in LLVM).

#include "cc.h"

#include <memory>
#include <string>
#include <vector>

#include "clang/Basic/DiagnosticFrontend.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/CodeGen/ObjectFilePCHContainerWriter.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/Job.h"
#include "clang/Driver/ToolChain.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/TextDiagnosticBuffer.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Frontend/Utils.h"
#include "clang/FrontendTool/Utils.h"
#include "clang/Serialization/ObjectFilePCHContainerReader.h"
#include "lld/Common/Driver.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

// The targets tide builds for: x86-64 and arm64, which are the machine's own
// and Android's, and the web.
extern "C" {
#define TIDE_TARGET(name)                                                                                              \
    void LLVMInitialize##name##TargetInfo();                                                                           \
    void LLVMInitialize##name##Target();                                                                               \
    void LLVMInitialize##name##TargetMC();                                                                             \
    void LLVMInitialize##name##AsmPrinter();                                                                           \
    void LLVMInitialize##name##AsmParser();
TIDE_TARGET(X86)
TIDE_TARGET(AArch64)
TIDE_TARGET(WebAssembly)
}

#define TIDE_INIT_TARGET_(name)                                                                                        \
    LLVMInitialize##name##TargetInfo();                                                                                \
    LLVMInitialize##name##Target();                                                                                    \
    LLVMInitialize##name##TargetMC();                                                                                  \
    LLVMInitialize##name##AsmPrinter();                                                                                \
    LLVMInitialize##name##AsmParser();
#define TIDE_INIT_TARGET(name) TIDE_INIT_TARGET_(name)

static void init_targets()
{
    static bool done;
    if (done) return;
    done = true;
    TIDE_INIT_TARGET(X86)
    TIDE_INIT_TARGET(AArch64)
    TIDE_INIT_TARGET(WebAssembly)
}

// lld's drivers register every target LLVM was built with, for link-time
// optimization, and naming a target's functions links the whole of it: 20 CPUs
// and GPUs in LLVM's Linux release, a third of tide. So those of the targets
// tide doesn't build for are defined here, empty, and the linker never takes
// theirs out of LLVM's libraries. LLVM's own lists say which there are, so a
// target a new LLVM adds is left out too.
#define TIDE_BUILDS_X86 ,
#define TIDE_BUILDS_AArch64 ,
#define TIDE_BUILDS_WebAssembly ,
#define TIDE_THIRD(first, second, third, ...) third
#define TIDE_PICK(...) TIDE_THIRD(__VA_ARGS__)
#define TIDE_KEPT(name, part)
#define TIDE_LEFT_OUT(name, part)                                                                                      \
    void LLVMInitialize##name##part() {}
// TIDE_LEFT_OUT, unless tide builds for the target: TIDE_BUILDS_<name> is then
// a comma, which puts TIDE_KEPT third.
#define TIDE_OTHER_TARGET(name, part) TIDE_PICK(TIDE_BUILDS_##name, TIDE_KEPT, TIDE_LEFT_OUT, )(name, part)
extern "C" {
#define LLVM_TARGET(name)                                                                                              \
    TIDE_OTHER_TARGET(name, TargetInfo)                                                                                \
    TIDE_OTHER_TARGET(name, Target)                                                                                    \
    TIDE_OTHER_TARGET(name, TargetMC)
#include "llvm/Config/Targets.def"
#define LLVM_ASM_PRINTER(name) TIDE_OTHER_TARGET(name, AsmPrinter)
#include "llvm/Config/AsmPrinters.def"
#define LLVM_ASM_PARSER(name) TIDE_OTHER_TARGET(name, AsmParser)
#include "llvm/Config/AsmParsers.def"
}

// The same for what LLVM's release builds into every pass pipeline (Polly, in
// its Linux one: an optimizer that only runs when asked for, which tide never
// does): each is an extension that registers nothing.
#define HANDLE_EXTENSION(name)                                                                                         \
    llvm::PassPluginLibraryInfo get##name##PluginInfo()                                                                \
    {                                                                                                                  \
        return {LLVM_PLUGIN_API_VERSION, #name, "", [](llvm::PassBuilder &) {}};                                       \
    }
#include "llvm/Support/Extension.def"

// The linkers games need: the web's, Android's (ELF, as Linux's), and the
// machine's own.
LLD_HAS_DRIVER(wasm)
LLD_HAS_DRIVER(elf)
#if defined(_WIN32)
LLD_HAS_DRIVER(coff)
LLD_HAS_DRIVER(mingw)
static const lld::DriverDef linkers[] = {{lld::Wasm, &lld::wasm::link}, {lld::Gnu, &lld::elf::link},
                                         {lld::WinLink, &lld::coff::link}, {lld::MinGW, &lld::mingw::link}};
#elif defined(__APPLE__)
LLD_HAS_DRIVER(macho)
static const lld::DriverDef linkers[] = {{lld::Wasm, &lld::wasm::link}, {lld::Gnu, &lld::elf::link},
                                         {lld::Darwin, &lld::macho::link}};
#else
static const lld::DriverDef linkers[] = {{lld::Wasm, &lld::wasm::link}, {lld::Gnu, &lld::elf::link}};
#endif

// ---------------------------------------------------------------------------
// Compiling: clang -cc1, as clang/tools/driver/cc1_main.cpp does it

static void backend_error(void *user, const char *message, bool)
{
    auto &diags = *static_cast<clang::DiagnosticsEngine *>(user);
    diags.Report(clang::diag::err_fe_error_backend) << message;
    llvm::sys::RunInterruptHandlers(); // Removes unfinished output files
    llvm::sys::Process::Exit(1);
}

// `args` starts with -cc1, and `self` is tide's path.
static int cc1(llvm::ArrayRef<const char *> args, const char *self)
{
    init_targets();
    auto pch = std::make_shared<clang::PCHContainerOperations>();
    pch->registerWriter(std::make_unique<clang::ObjectFilePCHContainerWriter>());
    pch->registerReader(std::make_unique<clang::ObjectFilePCHContainerReader>());

    // Diagnostics about the arguments wait until the real diagnostics exist.
    clang::DiagnosticOptions diag_opts;
    auto *buffer = new clang::TextDiagnosticBuffer;
    clang::DiagnosticsEngine diags(clang::DiagnosticIDs::create(), diag_opts, buffer);
    auto invocation = std::make_shared<clang::CompilerInvocation>();
    bool ok = clang::CompilerInvocation::CreateFromArgs(*invocation, args, diags, self);

    auto clang = std::make_unique<clang::CompilerInstance>(std::move(invocation), std::move(pch));
    clang->createVirtualFileSystem(llvm::vfs::getRealFileSystem(), buffer);
    clang->createDiagnostics();
    llvm::install_fatal_error_handler(backend_error, &clang->getDiagnostics());
    buffer->FlushDiagnostics(clang->getDiagnostics());
    if (ok) ok = clang::ExecuteCompilerInvocation(clang.get());
    llvm::remove_fatal_error_handler();
    return ok ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Linking: the driver's link command, run by lld in this process

#ifdef __APPLE__
// A Mac's program is linked by Apple's own linker, from the command line tools
// tide already needs for the SDK: /usr/bin/ld runs the one of the developer
// folder xcode-select names. It reads the SDK's stubs (.tbd) whatever
// targets they list, where lld rejects a stub that lists one it doesn't know,
// and with it everything that stub defines: Apple's SDKs from October 2026
// list arm64e.x1, which LLVM 23's lld doesn't, so every game failed to link.
// clang's driver writes ld64's arguments for it, as it does for lld.
static const char apple_ld[] = "/usr/bin/ld";

static int link_apple(const llvm::opt::ArgStringList &args)
{
    std::vector<llvm::StringRef> argv{"ld"};
    for (const char *arg : args) argv.push_back(arg);
    std::string error;
    bool failed = false;
    const int code = llvm::sys::ExecuteAndWait(apple_ld, argv, std::nullopt, {}, 0, 0, &error, &failed);
    if (failed) {
        llvm::errs() << "tide: error: Apple's linker (" << apple_ld << ") didn't run: " << error << "\n";
        return 1;
    }
    return code;
}
#endif

static int link(const llvm::Triple &triple, const llvm::opt::ArgStringList &args)
{
#ifdef __APPLE__
    if (triple.isOSDarwin() && llvm::sys::fs::can_execute(apple_ld)) return link_apple(args);
#endif
    // lld picks its flavor from the program's name, as the driver would have
    // run it; ld.lld also links MinGW, which the driver asks for with -m.
    const char *name = triple.isWasm()                        ? "wasm-ld"
                       : triple.isOSDarwin()                  ? "ld64.lld"
                       : triple.isWindowsMSVCEnvironment()    ? "lld-link"
                                                              : "ld.lld";
    std::vector<const char *> argv{name};
    argv.insert(argv.end(), args.begin(), args.end());
    const lld::Result result = lld::lldMain(argv, llvm::outs(), llvm::errs(), linkers);
    return result.retCode;
}

// ---------------------------------------------------------------------------
// The driver, as clang/tools/driver/driver.cpp runs it

static int driver(llvm::SmallVectorImpl<const char *> &args)
{
    init_targets();
    const std::string self = args[0];
    auto vfs = llvm::vfs::getRealFileSystem();

    std::unique_ptr<clang::DiagnosticOptions> diag_opts = clang::CreateAndPopulateDiagOpts(args);
    auto *printer = new clang::TextDiagnosticPrinter(llvm::errs(), *diag_opts);
    printer->setPrefix("tide");
    clang::DiagnosticsEngine diags(clang::DiagnosticIDs::create(), *diag_opts, printer);
    clang::ProcessWarningOptions(diags, *diag_opts, *vfs, /*ReportDiags=*/false);

    clang::driver::Driver driver(self, llvm::sys::getDefaultTargetTriple(), diags, "tide", vfs);
    // Compiles run in this process: clang runs -cc1 itself.
    auto run_cc1 = [&](llvm::SmallVectorImpl<const char *> &argv) {
        llvm::cl::ResetAllOptionOccurrences();
        return cc1(llvm::ArrayRef<const char *>(argv).slice(1), self.c_str());
    };
    driver.CC1Main = run_cc1;
    llvm::CrashRecoveryContext::Enable();

    // The driver looks for a linker program; point it at tide, which never runs
    // as one: link commands go to lld here. Only when linking for a target that
    // asks for it (wasm's doesn't), or clang warns that the argument is unused.
    bool links = true;
    for (size_t i = 1; i < args.size(); i++) {
        const llvm::StringRef a(args[i]);
        if (a == "-c" || a == "-S" || a == "-E" || a == "-fsyntax-only" || a == "-M" || a == "-MM") links = false;
        llvm::StringRef target;
        if ((a == "-target" || a == "--target") && i + 1 < args.size()) target = args[i + 1];
        else if (a.starts_with("--target=")) target = a.substr(sizeof "--target=" - 1);
        if (target.starts_with("wasm")) links = false;
    }
    const std::string ld_path = "--ld-path=" + self;
    if (links) args.push_back(ld_path.c_str());

    std::unique_ptr<clang::driver::Compilation> compilation(driver.BuildCompilation(args));
    if (!compilation || compilation->containsError()) return 1;
    const llvm::Triple &triple = compilation->getDefaultToolChain().getTriple();
    for (const clang::driver::Command &job : compilation->getJobs()) {
        int code;
        const llvm::opt::ArgStringList &job_args = job.getArguments();
        if (!job_args.empty() && llvm::StringRef(job_args[0]) == "-cc1") {
            std::string error;
            bool failed = false;
            code = job.Execute({}, &error, &failed);
            if (failed) llvm::errs() << "tide: error: " << error << "\n";
        } else {
            code = link(triple, job_args);
        }
        if (code != 0) return code;
    }
    return 0;
}

int tide_cc(const int argc, const char **argv)
{
    // clang's paths are relative to tide's: its headers are in ../lib/clang.
    llvm::SmallVector<const char *, 64> args(argv, argv + argc);
    const std::string self = llvm::sys::fs::getMainExecutable(argv[0], reinterpret_cast<void *>(&tide_cc));
    args[0] = self.c_str();
    if (args.size() > 1 && llvm::StringRef(args[1]) == "cc") args.erase(args.begin() + 1);
    if (args.size() > 1 && llvm::StringRef(args[1]) == "-cc1") {
        return cc1(llvm::ArrayRef<const char *>(args).slice(1), self.c_str());
    }
    return driver(args);
}
