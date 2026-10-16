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

enum class FunctionSource { EntryPoint, Export, CallTarget, Structure };

struct DiscoveredFunction
{
    uint64_t startOffset = 0;
    uint64_t endOffset   = 0;
    QString  name;
    QString  architecture;
    FunctionSource source = FunctionSource::CallTarget;
};
Q_DECLARE_METATYPE(DiscoveredFunction)

// Unified, lazy code discovery. Native PE/ELF metadata remains the preferred
// provider and seeds Capstone recursive descent. Other formats fall back to
// declarative Strata [code] targets (for example WebAssembly function bodies).
// Everything runs on one background job over a stable logical-document
// snapshot; HexView's live edit buffer is never touched off the GUI thread.
// Calling scan() again cancels the prior run and discards its pending result.
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
