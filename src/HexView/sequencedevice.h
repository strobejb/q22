#pragma once

#include <QIODevice>
#include <memory>

class sequence;

// Read-only QIODevice over a stable snapshot of a sequence's logical byte stream.
// Construct on the thread that owns/mutates the source sequence; after
// construction, the device can be moved to a worker thread for reading.
class SequenceDevice final : public QIODevice
{
  public:
    explicit SequenceDevice(const sequence &source, QObject *parent = nullptr);
    ~SequenceDevice() override;

    bool isValid() const;
    bool isSequential() const override
    {
        return false;
    }
    qint64 size() const override;
    bool seek(qint64 pos) override;

  protected:
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *data, qint64 maxSize) override;

  private:
    struct Private;
    std::unique_ptr<Private> d;
};
