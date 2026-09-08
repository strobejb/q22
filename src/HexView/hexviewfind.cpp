//
//  hexviewfind.cpp
//
//  Boyer-Moore search for HexView.
//  Ported from HexEdit Win32 (HexViewFind.cpp) by James Brown.
//  Original algorithm borrowed from Michael Lecuyer's Java source, 1998.
//

#include "hexview.h"
#include "sequencedevice.h"
#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QIODevice>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>

static constexpr unsigned MAX_PAT_LEN  = 256;
static constexpr unsigned MAX_CHAR     = 256;
static constexpr size_w   SEARCH_CHUNK = 1000;

namespace
{

qint64 readDeviceAt(QIODevice &source, size_w offset, uint8_t *data, size_w len)
{
    if (!data || len == 0)
        return 0;
    if (offset > static_cast<size_w>(std::numeric_limits<qint64>::max()) ||
        len > static_cast<size_w>(std::numeric_limits<qint64>::max()))
        return -1;
    if (!source.seek(static_cast<qint64>(offset)))
        return -1;
    return source.read(reinterpret_cast<char *>(data), static_cast<qint64>(len));
}

size_w chunkReadLength(size_w chunkStart, size_w chunkLength, size_w rangeEnd, unsigned patternLength)
{
    size_w readLength = chunkLength;
    if (patternLength > 1 && chunkStart + chunkLength < rangeEnd)
    {
        const size_w following  = rangeEnd - (chunkStart + chunkLength);
        readLength             += std::min<size_w>(following, static_cast<size_w>(patternLength - 1));
    }
    return readLength;
}

} // namespace

bool HexView::findInit(const uint8_t *pat, size_t length)
{
    return searchCompile(pat, length);
}

bool HexView::searchCompile(const uint8_t *pat, size_t length)
{
    unsigned j, k, t, t1, q, q1;
    unsigned f[MAX_PAT_LEN];

    m_nSearchLen = (unsigned)length;

    if (m_nSearchLen == 0 || m_nSearchLen > MAX_PAT_LEN)
        return false;

    memcpy(m_pSearchPat, pat, m_nSearchLen);

    for (k = 0; k < MAX_CHAR; k++)
        m_bmSkip[k] = m_nSearchLen;

    for (k = 1; k <= m_nSearchLen; k++)
    {
        m_bmD[k - 1]                  = (m_nSearchLen << 1) - k;
        m_bmSkip[m_pSearchPat[k - 1]] = m_nSearchLen - k;
    }

    for (t = m_nSearchLen + 1, j = m_nSearchLen; j > 0; j--)
    {
        f[j - 1] = t;

        while (t <= m_nSearchLen && m_pSearchPat[j - 1] != m_pSearchPat[t - 1])
        {
            m_bmD[t - 1] = std::min(m_bmD[t - 1], m_nSearchLen - j);
            t            = f[t - 1];
        }

        t--;
    }

    q  = t;
    t  = m_nSearchLen + 1 - q;
    q1 = 1;
    t1 = 0;

    for (j = 1; j <= t; j++)
    {
        f[j - 1] = t1;
        while (t1 >= 1 && pat[j - 1] != pat[t1 - 1])
            t1 = f[t1 - 1];
        t1++;
    }

    while (q < m_nSearchLen)
    {
        for (k = q1; k <= q; k++)
            m_bmD[k - 1] = std::min(m_bmD[k - 1], m_nSearchLen + q - k);

        q1 = q + 1;
        q  = q + t - f[t - 1];
        t  = f[t - 1];
    }

    return true;
}

int HexView::searchBlock(const uint8_t *block, int start, int length, int *partial, bool matchCase) const
{
    int incr     = 0;
    int j        = 0;
    int blocklen = start + length;
    int k;

    *partial = -1;

    for (k = start + (int)m_nSearchLen - 1; k < blocklen;)
    {
        if (matchCase)
        {
            for (j = (int)m_nSearchLen - 1; j >= 0 && block[k] == m_pSearchPat[j]; j--)
                k--;
        }
        else
        {
            for (j = (int)m_nSearchLen - 1; j >= 0 && toupper(block[k]) == toupper(m_pSearchPat[j]); j--)
                k--;
        }

        if (j < 0)
            return k + 1;

        incr  = (int)std::max(m_bmSkip[block[k]], m_bmD[j]);
        k    += incr;
    }

    if (k >= blocklen && j > 0)
        *partial = k - incr - 1;

    return -1;
}

void HexView::queryProgressNotify(size_w pos, size_w len, double mbPerSec)
{
    emit findProgress(pos, len, mbPerSec);
    QCoreApplication::processEvents();
}

bool HexView::findNext(size_w *result, uint options)
{
    const sequence *sourceSequence = dataSequence();
    if (!sourceSequence)
        return false;

    SequenceDevice source(*sourceSequence);
    if (!source.isValid() || !source.open(QIODevice::ReadOnly))
        return false;

    return findNext(source, size(), result, options);
}

bool HexView::findNext(QIODevice &source, size_w sourceSize, size_w *result, uint options)
{
    QByteArray block(static_cast<int>(SEARCH_CHUNK + MAX_PAT_LEN - 1), Qt::Uninitialized);

    const bool   selScope  = (options & HVFF_SCOPE_SELECTION) != 0;
    size_w       searchidx = selScope ? selectionStart() : m_nCursorOffset;
    const size_w searchlen = selScope ? selectionSize() : sourceSize;

    const bool matchCase = (options & HVFF_CASE_INSENSITIVE) == 0;

    int querycount  = 0;
    m_findCancelled = false;

    if (m_nSearchLen == 0 || !source.isOpen())
        return false;

    QElapsedTimer rateTimer;
    rateTimer.start();

    // ── Backward search ───────────────────────────────────────────────────────
    // Scan forward from the beginning up to (but not including) the current
    // selection start, keeping the last match found.  This naturally gives the
    // previous occurrence and won't re-find the currently-selected match.
    if (options & HVFF_BACKWARD)
    {
        size_w scanIdx  = selScope ? selectionStart() : 0;
        size_w scanEnd  = selScope ? selectionStart() : m_nSelectionStart;
        scanIdx         = std::min(scanIdx, sourceSize);
        scanEnd         = std::min(scanEnd, sourceSize);
        size_w rateBase = scanIdx;
        bool   found    = false;
        size_w lastPos  = 0;

        while (scanIdx < scanEnd)
        {
            const size_w chunk      = std::min(SEARCH_CHUNK, scanEnd - scanIdx);
            const size_w readLength = chunkReadLength(scanIdx, chunk, scanEnd, m_nSearchLen);
            const qint64 read = readDeviceAt(source, scanIdx, reinterpret_cast<uint8_t *>(block.data()), readLength);
            int          blen = read > 0 ? static_cast<int>(read) : 0;
            if (blen <= 0)
                break;

            int spos = 0, partial = -1, matchPos;
            while ((matchPos = searchBlock(reinterpret_cast<const uint8_t *>(block.constData()), spos, blen - spos,
                                           &partial, matchCase)) >= 0)
            {
                if (static_cast<size_w>(matchPos) >= chunk)
                    break;
                lastPos = scanIdx + matchPos;
                found   = true;
                spos    = matchPos + 1;
                if (spos >= blen)
                    break;
            }
            const size_w advanced  = std::min(chunk, static_cast<size_w>(blen));
            scanIdx               += advanced;
            if (advanced < chunk)
                break;

            if (++querycount == 1024)
            {
                qint64 elapsedMs = rateTimer.elapsed();
                double bytes     = (double)(scanIdx - rateBase);
                double mbPerSec  = elapsedMs > 0 ? bytes / elapsedMs / 1000.0 : 0.0;
                rateTimer.restart();
                rateBase = scanIdx;
                queryProgressNotify(scanIdx, scanEnd, mbPerSec);
                if (m_findCancelled)
                    return false;
                querycount = 0;
            }
        }

        if (found)
        {
            *result = lastPos;
            return true;
        }
        return false;
    }

    // ── Forward search ────────────────────────────────────────────────────────
    size_w       rateBasePos = searchidx;
    const size_w searchEnd   = selScope ? selectionEnd() : sourceSize;
    searchidx                = std::min(searchidx, sourceSize);

    while (searchidx < searchEnd)
    {
        const size_w chunk      = std::min(SEARCH_CHUNK, searchEnd - searchidx);
        const size_w readLength = chunkReadLength(searchidx, chunk, searchEnd, m_nSearchLen);
        const qint64 read = readDeviceAt(source, searchidx, reinterpret_cast<uint8_t *>(block.data()), readLength);
        int          len  = read > 0 ? static_cast<int>(read) : 0;
        if (len <= 0)
            break;

        int pos     = 0;
        int partial = -1;

        if (selScope && (searchidx < selectionStart() || searchidx >= selectionEnd()))
            break;

        while ((pos = searchBlock(reinterpret_cast<const uint8_t *>(block.constData()), pos, len - pos, &partial,
                                  matchCase)) >= 0)
        {
            if (static_cast<size_w>(pos) >= chunk)
                break;
            *result = searchidx + pos;
            return true;
        }

        const size_w advanced  = std::min(chunk, static_cast<size_w>(len));
        searchidx             += advanced;
        if (advanced < chunk)
            break;

        if (++querycount == 1024)
        {
            qint64 elapsedMs = rateTimer.elapsed();
            double bytes     = (double)(searchidx - rateBasePos);
            double mbPerSec  = elapsedMs > 0 ? bytes / elapsedMs / 1000.0 : 0.0;
            rateTimer.restart();
            rateBasePos = searchidx;
            queryProgressNotify(searchidx, searchlen, mbPerSec);
            if (m_findCancelled)
                return false;
            querycount = 0;
        }
    }

    return false;
}

HvFindResult HexView::findNextEx(size_w *result, uint options)
{
    const uint findOptions = options & ~HVFF_WRAP_AROUND;
    if (findNext(result, findOptions))
        return HVFR_Found;
    if (isFindCancelled())
        return HVFR_Cancelled;

    const bool canWrap = (options & HVFF_WRAP_AROUND) && !(options & HVFF_SCOPE_SELECTION) && size() > 0;
    if (!canWrap)
        return HVFR_NotFound;

    const size_w originalStart  = selectionStart();
    const size_w originalEnd    = selectionEnd();
    const size_w originalCursor = cursorOffset();

    if (options & HVFF_BACKWARD)
        setCurSel(size(), size());
    else
        setCurSel(0, 0);

    if (findNext(result, findOptions))
        return HVFR_FoundWrapped;
    if (isFindCancelled())
        return HVFR_Cancelled;

    if (originalStart == originalEnd)
        setCurSel(originalCursor, originalCursor);
    else
        setCurSel(originalStart, originalEnd);
    return HVFR_NotFound;
}
