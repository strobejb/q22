#ifndef DISASM_CODEDISCOVERY_H
#define DISASM_CODEDISCOVERY_H

#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>

#include <atomic>
#include <cstdint>
#include <memory>

class HexView;

enum class FunctionSource { EntryPoint, Export, CallTarget };

struct DiscoveredFunction
{
    uint64_t startOffset = 0;
    uint64_t endOffset   = 0;
    QString  name;
    FunctionSource source = FunctionSource::CallTarget;
};
Q_DECLARE_METATYPE(DiscoveredFunction)

// Recursive-descent code discovery: seeds a worklist from the PE entry point
// and export table, then follows every direct call/jmp target reachable from
// there (mirroring how IDA/Ghidra bootstrap a disassembly). Runs entirely on
// a background thread reading through its own QFile handle -- HexView's live
// edit buffer is never touched off the GUI thread. Calling scan() again
// before a prior run finishes cancels the prior run; any result it was about
// to deliver is discarded instead.
class CodeDiscoveryEngine : public QObject
{
    Q_OBJECT
public:
    explicit CodeDiscoveryEngine(QObject *parent = nullptr);
    ~CodeDiscoveryEngine() override;

    void scan(HexView *hv);

signals:
    void finished(QList<DiscoveredFunction> functions);
    // Emitted periodically (throttled, not per-item) while a scan is in
    // flight so the UI can populate incrementally instead of staying empty
    // for the whole run -- the full snapshot discovered so far, not just
    // what's new since the last emission, so a slot can just replace its
    // current list each time without tracking deltas itself.
    void partialResults(QList<DiscoveredFunction> functionsSoFar);
    // worklistTotal grows as new call/jmp targets are discovered, so this
    // percentage isn't perfectly monotonic, but it trends upward and always
    // reaches 100% at completion, since worklistTotal stops growing once no
    // further new targets are found.
    void scanProgress(int discoveredCount, int worklistProcessed, int worklistTotal);

private:
    std::shared_ptr<std::atomic_bool> m_cancelFlag;
};

#endif // DISASM_CODEDISCOVERY_H
