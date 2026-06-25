#include "disasm/codediscovery.h"

#include "HexView/hexview.h"
#include "disasm/branchtarget.h"
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

        const PeMetadata pe = readPeMetadata(reader, fileSize);
        QList<DiscoveredFunction> results;

        if (pe.isValid && !cancelFlag->load())
        {
            constexpr cs_arch arch = CS_ARCH_X86;
            const cs_mode mode = pe.is64Bit ? CS_MODE_64 : CS_MODE_32;
            csh handle = 0;
            if (cs_open(arch, mode, &handle) == CS_ERR_OK)
            {
                cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);

                auto insideExecutableSection = [&pe](uint64_t offset) {
                    for (const PeSection &s : pe.sections)
                        if (s.executable && offset >= s.fileOffset && offset < s.fileOffset + s.fileSize)
                            return true;
                    return false;
                };

                // Cache each executable section's bytes once -- the worklist
                // jumps around within them repeatedly, and thousands of
                // separate seek+read calls for 4KB windows is dominated by
                // I/O round-trip latency, not actual decode work.
                struct CachedSection { uint64_t fileOffset; QByteArray data; };
                std::vector<CachedSection> sectionCache;
                for (const PeSection &s : pe.sections)
                    if (s.executable && s.fileSize > 0)
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

                std::vector<Seed> worklist;
                if (auto entryOffset = rvaToFileOffset(pe, pe.entryPointRva))
                    worklist.push_back({*entryOffset, true, FunctionSource::EntryPoint, QString()});
                for (const PeExport &exp : pe.exports)
                    if (auto exportOffset = rvaToFileOffset(pe, exp.rva))
                        worklist.push_back({*exportOffset, true, FunctionSource::Export, exp.name});

                std::unordered_set<uint64_t> visited;
                std::unordered_map<uint64_t, DiscoveredFunction> discovered;

                cs_insn *insn = cs_malloc(handle);
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
                            if (insideExecutableSection(*target) && !visited.count(*target))
                                worklist.push_back({*target, cs_insn_group(handle, insn, CS_GRP_CALL) != 0,
                                                     FunctionSource::CallTarget, QString()});
                        if (cs_insn_group(handle, insn, CS_GRP_RET))
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
