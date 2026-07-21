//
//  hexviewfind.cpp
//
//  Boyer-Moore search for HexView.
//  Ported from HexEdit Win32 (HexViewFind.cpp) by James Brown.
//  Original algorithm borrowed from Michael Lecuyer's Java source, 1998.
//

#include "hexview.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <QCoreApplication>
#include <QElapsedTimer>

static constexpr unsigned MAX_PAT_LEN = 256;
static constexpr unsigned MAX_CHAR    = 256;

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

    for (k = 1; k <= m_nSearchLen; k++) {
        m_bmD[k-1] = (m_nSearchLen << 1) - k;
        m_bmSkip[m_pSearchPat[k-1]] = m_nSearchLen - k;
    }

    for (t = m_nSearchLen + 1, j = m_nSearchLen; j > 0; j--) {
        f[j-1] = t;

        while (t <= m_nSearchLen && m_pSearchPat[j-1] != m_pSearchPat[t-1]) {
            m_bmD[t-1] = std::min(m_bmD[t-1], m_nSearchLen - j);
            t = f[t-1];
        }

        t--;
    }

    q  = t;
    t  = m_nSearchLen + 1 - q;
    q1 = 1;
    t1 = 0;

    for (j = 1; j <= t; j++) {
        f[j-1] = t1;
        while (t1 >= 1 && pat[j-1] != pat[t1-1])
            t1 = f[t1-1];
        t1++;
    }

    while (q < m_nSearchLen) {
        for (k = q1; k <= q; k++)
            m_bmD[k-1] = std::min(m_bmD[k-1], m_nSearchLen + q - k);

        q1 = q + 1;
        q  = q + t - f[t-1];
        t  = f[t-1];
    }

    return true;
}

int HexView::searchBlock(const uint8_t *block, int start, int length, int *partial, bool matchCase) const
{
    int incr    = 0;
    int j       = 0;
    int blocklen = start + length;
    int k;

    *partial = -1;

    for (k = start + (int)m_nSearchLen - 1; k < blocklen; ) {
        if (matchCase) {
            for (j = (int)m_nSearchLen - 1; j >= 0 && block[k] == m_pSearchPat[j]; j--)
                k--;
        } else {
            for (j = (int)m_nSearchLen - 1; j >= 0 && toupper(block[k]) == toupper(m_pSearchPat[j]); j--)
                k--;
        }

        if (j < 0)
            return k + 1;

        incr = (int)std::max(m_bmSkip[block[k]], m_bmD[j]);
        k += incr;
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
    uint8_t block[1000];

    bool   selScope  = (options & HVFF_SCOPE_SELECTION) != 0;
    size_w searchidx = selScope ? selectionStart() : m_nCursorOffset;
    size_w searchlen = selScope ? selectionSize()  : size();

    bool   matchCase = (options & HVFF_CASE_INSENSITIVE) == 0;

    int  querycount = 0;
    m_findCancelled = false;

    if (m_nSearchLen == 0)
        return false;

    QElapsedTimer rateTimer;
    rateTimer.start();

    // ── Backward search ───────────────────────────────────────────────────────
    // Scan forward from the beginning up to (but not including) the current
    // selection start, keeping the last match found.  This naturally gives the
    // previous occurrence and won't re-find the currently-selected match.
    if (options & HVFF_BACKWARD) {
        size_w scanIdx  = selScope ? selectionStart() : 0;
        size_w scanEnd  = selScope ? selectionStart() : m_nSelectionStart;
        size_w rateBase = scanIdx;
        bool   found    = false;
        size_w lastPos  = 0;

        while (scanIdx < scanEnd) {
            size_w chunk = std::min((size_w)1000, scanEnd - scanIdx);
            int blen = (int)getData(scanIdx, block, (size_t)chunk);
            if (blen <= 0) break;

            int spos = 0, partial = -1, matchPos;
            while ((matchPos = searchBlock(block, spos, blen - spos, &partial, matchCase)) >= 0) {
                lastPos = scanIdx + matchPos;
                found   = true;
                spos    = matchPos + 1;
                if (spos >= blen) break;
            }
            scanIdx += blen;

            if (++querycount == 1024) {
                qint64 elapsedMs = rateTimer.elapsed();
                double bytes     = (double)(scanIdx - rateBase);
                double mbPerSec  = elapsedMs > 0 ? bytes / elapsedMs / 1000.0 : 0.0;
                rateTimer.restart();
                rateBase = scanIdx;
                queryProgressNotify(scanIdx, scanEnd, mbPerSec);
                if (m_findCancelled) return false;
                querycount = 0;
            }
        }

        if (found) { *result = lastPos; return true; }
        return false;
    }

    // ── Forward search ────────────────────────────────────────────────────────
    size_w rateBasePos = searchidx;

    while (int len = (int)getData(searchidx, block, 1000)) {
        int pos     = 0;
        int partial = -1;

        if (selScope && (searchidx < selectionStart() || searchidx >= selectionEnd()))
            break;

        while ((pos = searchBlock(block, pos, len - pos, &partial, matchCase)) >= 0) {
            *result = searchidx + pos;
            return true;
        }

        searchidx += len;

        if (++querycount == 1024) {
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

    const bool canWrap = (options & HVFF_WRAP_AROUND) &&
                         !(options & HVFF_SCOPE_SELECTION) &&
                         size() > 0;
    if (!canWrap)
        return HVFR_NotFound;

    const size_w originalStart = selectionStart();
    const size_w originalEnd = selectionEnd();
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
