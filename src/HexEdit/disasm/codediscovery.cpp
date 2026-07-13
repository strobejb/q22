#include "disasm/codediscovery.h"

#include "HexView/hexview.h"
#include "disasm/branchtarget.h"
#include "disasm/elfmetadata.h"
#include "disasm/pemetadata.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QThread>

#include <capstone/capstone.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

// Per-seed linear-walk read window. Generous for ordinary functions; a
// function longer than this gets its tail truncated rather than the read
// growing unboundedly -- an acceptable approximation given function-boundary
// detection here is already heuristic (same as real disassemblers).
constexpr int kFunctionScanWindow = 4096;
constexpr int kMaxFunctions        = 200000; // sanity cap against pathological binaries

struct Seed
{
    uint64_t       offset;
    bool           isFunctionSeed; // false for jmp-followed code: walked for coverage, not listed
    FunctionSource source;
    QString        name;
};

// Format-agnostic view of "the bits the scanner actually needs", built from
// either PeMetadata or ElfMetadata below so the rest of scan() doesn't care
// which file format it's looking at.
struct ExecSection { uint64_t fileOffset; uint64_t fileSize; };
struct SeedExport  { QString name; uint64_t fileOffset; };

// ELF_MACHINE values (see causeway/strata/elf.strata) this engine knows how
// to point Capstone at. Other e_machine values are left unhandled -- the
// scan simply finds nothing for that file rather than guessing wrong.
constexpr uint16_t kElfMachineX86    = 3;
constexpr uint16_t kElfMachineArm    = 40;
constexpr uint16_t kElfMachineX86_64 = 62;
constexpr uint16_t kElfMachineAarch64 = 183;

} // namespace

CodeDiscoveryEngine::CodeDiscoveryEngine(QObject *parent)
    : QObject(parent)
{
}

CodeDiscoveryEngine::~CodeDiscoveryEngine()
{
    if (m_cancelFlag)
        m_cancelFlag->store(true);
}

void CodeDiscoveryEngine::scan(HexView *hv)
{
    if (m_cancelFlag)
        m_cancelFlag->store(true); // cancel any prior in-flight run; its result is discarded below

    if (!hv)
        return;

    auto cancelFlag = std::make_shared<std::atomic_bool>(false);
    m_cancelFlag = cancelFlag;

    const QString path = hv->filePath();
    const uint64_t fileSize = static_cast<uint64_t>(hv->size());
    QPointer<CodeDiscoveryEngine> guard(this);

    QThread *thread = QThread::create([guard, cancelFlag, path, fileSize]() {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return;

        const PeByteReader reader = [&file](uint64_t offset, uint8_t *buf, size_t len) -> size_t {
            if (!file.seek(static_cast<qint64>(offset)))
                return 0;
            const qint64 got = file.read(reinterpret_cast<char *>(buf), static_cast<qint64>(len));
            return got > 0 ? static_cast<size_t>(got) : 0;
        };

        // Format detection: try PE first (the common case for this app's
        // usual Windows-binary targets), then ELF. Whichever succeeds feeds
        // the same format-agnostic ExecSection/SeedExport lists below, so
        // the actual recursive-descent walk has no format-specific code at
        // all -- only the "how do I find entry point / exports / executable
        // ranges" step differs.
        bool imageValid = false;
        cs_arch arch = CS_ARCH_X86;
        cs_mode mode = CS_MODE_64;
        std::optional<uint64_t> entryFileOffset;
        std::vector<ExecSection> execSections;
        std::vector<SeedExport> seedExports;

        const PeMetadata pe = readPeMetadata(reader, fileSize);
        if (pe.isValid)
        {
            imageValid = true;
            arch = CS_ARCH_X86;
            mode = pe.is64Bit ? CS_MODE_64 : CS_MODE_32;
            entryFileOffset = rvaToFileOffset(pe, pe.entryPointRva);
            for (const PeSection &s : pe.sections)
                if (s.executable && s.fileSize > 0)
                    execSections.push_back({s.fileOffset, s.fileSize});
            for (const PeExport &exp : pe.exports)
                if (auto off = rvaToFileOffset(pe, exp.rva))
                    seedExports.push_back({exp.name, *off});
        }
        else
        {
            const ElfMetadata elf = readElfMetadata(reader, fileSize);
            bool archKnown = true;
            switch (elf.machine) {
            case kElfMachineX86:     arch = CS_ARCH_X86;   mode = CS_MODE_32;     break;
            case kElfMachineX86_64:  arch = CS_ARCH_X86;   mode = CS_MODE_64;     break;
            case kElfMachineArm:     arch = CS_ARCH_ARM;   mode = CS_MODE_ARM;    break;
            case kElfMachineAarch64: arch = CS_ARCH_ARM64; mode = (cs_mode)0;     break;
            default:                 archKnown = false;                          break;
            }
            if (elf.isValid && archKnown)
            {
                imageValid = true;
                entryFileOffset = vaddrToFileOffset(elf, elf.entryVaddr);
                for (const ElfSection &s : elf.sections)
                    if (s.executable && s.fileSize > 0)
                        execSections.push_back({s.fileOffset, s.fileSize});
                for (const ElfExport &exp : elf.exports)
                    if (auto off = vaddrToFileOffset(elf, exp.vaddr))
                        seedExports.push_back({exp.name, *off});
            }
        }

        QList<DiscoveredFunction> results;

        if (imageValid && !cancelFlag->load())
        {
            csh handle = 0;
            if (cs_open(arch, mode, &handle) == CS_ERR_OK)
            {
                cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);

                auto insideExecutableSection = [&execSections](uint64_t offset) {
                    for (const ExecSection &s : execSections)
                        if (offset >= s.fileOffset && offset < s.fileOffset + s.fileSize)
                            return true;
                    return false;
                };

                // Cache each executable section's bytes once -- the worklist
                // jumps around within them repeatedly, and thousands of
                // separate seek+read calls for 4KB windows is dominated by
                // I/O round-trip latency, not actual decode work.
                struct CachedSection { uint64_t fileOffset; QByteArray data; };
                std::vector<CachedSection> sectionCache;
                for (const ExecSection &s : execSections)
                    {
                        QByteArray data(static_cast<int>(qMin<uint64_t>(s.fileSize, 64u * 1024 * 1024)), '\0');
                        const size_t got = reader(s.fileOffset, reinterpret_cast<uint8_t *>(data.data()),
                                                   static_cast<size_t>(data.size()));
                        data.resize(static_cast<int>(got));
                        sectionCache.push_back({s.fileOffset, std::move(data)});
                    }
                const auto cachedRead = [&](uint64_t offset, uint8_t *dst, size_t len) -> size_t {
                    for (const CachedSection &cs : sectionCache)
                    {
                        if (offset >= cs.fileOffset && offset < cs.fileOffset + static_cast<uint64_t>(cs.data.size()))
                        {
                            const uint64_t avail = (cs.fileOffset + static_cast<uint64_t>(cs.data.size())) - offset;
                            const size_t n = static_cast<size_t>(qMin<uint64_t>(avail, len));
                            memcpy(dst, cs.data.constData() + (offset - cs.fileOffset), n);
                            return n;
                        }
                    }
                    return reader(offset, dst, len);
                };

                std::unordered_set<uint64_t> visited;
                std::unordered_map<uint64_t, DiscoveredFunction> discovered;

                cs_insn *insn = cs_malloc(handle);

                // MSVC's incremental-linking table ("/INCREMENTAL", the
                // default for many non-LTCG builds): every function's real
                // body is placed wherever the linker likes, and a stable
                // thunk -- the *entire* function body is just "jmp
                // realBody", nothing else -- sits at the address the export
                // table/entry point actually point to, so relinking only
                // has to patch the thunk table, not every caller. Without
                // resolving this, every export/entry-point-derived
                // "function" is a single misleading jmp instruction instead
                // of real code. Peek-decodes one instruction at a time
                // (independent of `visited`/the main walk below) and
                // follows a chain of these, bounded against the degenerate
                // case of a cycle or unusually long chain.
                constexpr int kMaxThunkChainHops = 8;
                const auto resolveThunkTarget = [&](uint64_t startAddr) -> uint64_t {
                    uint64_t resolvedAddr = startAddr;
                    for (int hop = 0; hop < kMaxThunkChainHops; ++hop)
                    {
                        if (!insideExecutableSection(resolvedAddr))
                            break;
                        uint8_t peekBuf[16] = {0}; // longest possible x86 instruction
                        const size_t peekGot = cachedRead(resolvedAddr, peekBuf, sizeof(peekBuf));
                        if (peekGot == 0)
                            break;
                        const uint8_t *peekCode = peekBuf;
                        size_t peekSize = peekGot;
                        uint64_t peekAddr = resolvedAddr;
                        if (!cs_disasm_iter(handle, &peekCode, &peekSize, &peekAddr, insn))
                            break;
                        if (!isUnconditionalJump(handle, *insn, arch))
                            break;
                        const auto target = branchTargetForInstruction(handle, *insn, arch);
                        if (!target)
                            break;
                        resolvedAddr = *target;
                    }
                    return resolvedAddr;
                };

                std::vector<Seed> worklist;
                if (entryFileOffset)
                    worklist.push_back({resolveThunkTarget(*entryFileOffset), true, FunctionSource::EntryPoint, QString()});
                for (const SeedExport &exp : seedExports)
                    worklist.push_back({resolveThunkTarget(exp.fileOffset), true, FunctionSource::Export, exp.name});

                size_t worklistIndex = 0;

                // Throttled (wall-clock, not per-item) so the UI gets
                // periodic updates without the snapshot-and-marshal cost
                // (sorting + copying the whole discovered list onto the
                // main thread) running on every single iteration.
                constexpr int kProgressEmitIntervalMs = 150;
                QElapsedTimer progressTimer;
                progressTimer.start();
                const auto emitProgress = [&](bool force) {
                    if (!force && progressTimer.elapsed() < kProgressEmitIntervalMs)
                        return;
                    progressTimer.restart();
                    QList<DiscoveredFunction> snapshot;
                    snapshot.reserve(static_cast<int>(discovered.size()));
                    for (auto &entry : discovered)
                        snapshot.push_back(entry.second);
                    std::sort(snapshot.begin(), snapshot.end(), [](const DiscoveredFunction &a, const DiscoveredFunction &b) {
                        return a.startOffset < b.startOffset;
                    });
                    const int discoveredCount = static_cast<int>(discovered.size());
                    const int processed = static_cast<int>(worklistIndex);
                    const int total = static_cast<int>(worklist.size());
                    QMetaObject::invokeMethod(qApp, [guard, cancelFlag, snapshot, discoveredCount, processed, total]() {
                        if (!guard || cancelFlag->load())
                            return;
                        emit guard->partialResults(snapshot);
                        emit guard->scanProgress(discoveredCount, processed, total);
                    }, Qt::QueuedConnection);
                };

                while (worklistIndex < worklist.size() && !cancelFlag->load()
                       && discovered.size() < static_cast<size_t>(kMaxFunctions))
                {
                    // Index, not iterator/reference: the vector grows while
                    // we walk (call/jmp targets get appended below).
                    const Seed seed = worklist[worklistIndex++];
                    if (visited.count(seed.offset) || !insideExecutableSection(seed.offset))
                        continue;

                    QByteArray buf(kFunctionScanWindow, '\0');
                    const size_t got = cachedRead(seed.offset, reinterpret_cast<uint8_t *>(buf.data()),
                                                   static_cast<size_t>(kFunctionScanWindow));
                    if (got == 0)
                        continue;
                    buf.resize(static_cast<int>(got));

                    // cs_disasm_iter decodes one instruction at a time, so the
                    // walk stops exactly at RET/an already-claimed address
                    // instead of -- as a plain cs_disasm(count=0) call would --
                    // eagerly decoding the entire 4KB window up front (most of
                    // which a typical short function never needs).
                    const uint8_t *code = reinterpret_cast<const uint8_t *>(buf.constData());
                    size_t codeSize = buf.size();
                    uint64_t addr = seed.offset;
                    uint64_t runEnd = seed.offset;
                    while (codeSize > 0 && cs_disasm_iter(handle, &code, &codeSize, &addr, insn))
                    {
                        if (visited.count(insn->address) && insn->address != seed.offset)
                            break; // ran into code already claimed by another discovered run

                        visited.insert(insn->address);
                        runEnd = insn->address + insn->size;

                        if (auto target = branchTargetForInstruction(handle, *insn, arch))
                        {
                            const bool isCall = cs_insn_group(handle, insn, CS_GRP_CALL) != 0;
                            // Internal calls don't normally go through the
                            // incremental-linking table (that's for
                            // externally-visible export/entry addresses
                            // that have to stay stable across relinks), but
                            // resolve anyway for correctness/consistency --
                            // the chain-following is cheap and bounded.
                            const uint64_t resolved = isCall ? resolveThunkTarget(*target) : *target;
                            if (insideExecutableSection(resolved) && !visited.count(resolved))
                                worklist.push_back({resolved, isCall, FunctionSource::CallTarget, QString()});
                        }
                        if (cs_insn_group(handle, insn, CS_GRP_RET))
                            break;
                        // Execution never falls through an unconditional
                        // jump -- whatever bytes physically follow it belong
                        // to a different thunk or unrelated data, not this
                        // run. Conditional branches (Jcc/B.cond) aren't
                        // covered by this check, since execution can fall
                        // through those.
                        if (isUnconditionalJump(handle, *insn, arch))
                            break;
                    }

                    if (seed.isFunctionSeed)
                    {
                        DiscoveredFunction fn;
                        fn.startOffset = seed.offset;
                        fn.endOffset   = runEnd;
                        fn.name        = seed.name;
                        fn.source      = seed.source;
                        discovered.emplace(seed.offset, fn);
                    }

                    emitProgress(/*force=*/false);
                }
                cs_free(insn, 1);

                cs_close(&handle);

                results.reserve(static_cast<int>(discovered.size()));
                for (auto &entry : discovered)
                    results.push_back(entry.second);
                std::sort(results.begin(), results.end(), [](const DiscoveredFunction &a, const DiscoveredFunction &b) {
                    return a.startOffset < b.startOffset;
                });
            }
        }

        if (!cancelFlag->load())
        {
            QMetaObject::invokeMethod(qApp, [guard, cancelFlag, results]() {
                if (guard && !cancelFlag->load())
                    emit guard->finished(results);
            }, Qt::QueuedConnection);
        }
    });

    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}
