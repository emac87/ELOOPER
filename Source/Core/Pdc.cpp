#include "Pdc.h"
#include <algorithm>

namespace el {

void DelayLine::prepare (int d)
{
    delaySamples = std::max (0, d);
    buf.assign ((size_t) delaySamples + 1, 0.0f);
    writePos = 0;
}

void DelayLine::reset() noexcept
{
    std::fill (buf.begin(), buf.end(), 0.0f);
    writePos = 0;
}

void DelayLine::process (float* data, int n) noexcept
{
    if (delaySamples <= 0 || buf.empty()) return;
    const int size = (int) buf.size();   // delaySamples + 1
    for (int i = 0; i < n; ++i) {
        buf[(size_t) writePos] = data[i];
        int readPos = writePos - delaySamples;
        if (readPos < 0) readPos += size;
        data[i] = buf[(size_t) readPos];
        if (++writePos == size) writePos = 0;
    }
}

void DelayLine::copyStateFrom (const DelayLine& o)
{
    if (o.delaySamples != delaySamples || o.buf.size() != buf.size()) return;
    buf = o.buf;
    writePos = o.writePos;
}

namespace pdc {

Result compute (const std::vector<Node>& nodes, int masterIndex)
{
    const size_t n = nodes.size();
    Result r;
    r.start.assign (n, 0);
    r.outDelay.assign (n, 0);
    r.sendDelay.resize (n);
    r.auxDelay.resize (n);
    for (size_t i = 0; i < n; ++i) {
        r.sendDelay[i].assign (nodes[i].sends.size(), 0);
        r.auxDelay[i].assign (nodes[i].auxChildren.size(), 0);
    }

    // Every contribution into a node: where it comes from and how late it arrives.
    enum class Kind { Out, Send, Aux };
    struct Contribution { size_t src; Kind kind; size_t k; int arrival; };
    std::vector<std::vector<Contribution>> into (n);

    auto valid = [n] (int i) { return i >= 0 && (size_t) i < n; };
    for (size_t i = 0; i < n; ++i) {
        // All contributions into i come from earlier nodes (topological order), so they are known.
        int start = 0;
        for (auto& c : into[i]) start = std::max (start, c.arrival);
        r.start[i] = start;
        for (auto& c : into[i]) {
            const int pad = start - c.arrival;
            switch (c.kind) {
                case Kind::Out:  r.outDelay[c.src] = pad; break;
                case Kind::Send: r.sendDelay[c.src][c.k] = pad; break;
                case Kind::Aux:  r.auxDelay[c.src][c.k] = pad; break;
            }
        }
        const auto& nd = nodes[i];
        const int afterInst = start + nd.instLatency;
        const int afterPre = afterInst + nd.preLatency;
        const int afterPost = afterPre + nd.postLatency;
        if (valid (nd.outIndex) && (size_t) nd.outIndex > i) into[(size_t) nd.outIndex].push_back ({ i, Kind::Out, 0, afterPost });
        for (size_t k = 0; k < nd.sends.size(); ++k) {
            const int d = nd.sends[k].dest;
            if (valid (d) && (size_t) d > i) into[(size_t) d].push_back ({ i, Kind::Send, k, nd.sends[k].preFader ? afterPre : afterPost });
        }
        for (size_t k = 0; k < nd.auxChildren.size(); ++k) {
            const int d = nd.auxChildren[k];
            if (valid (d) && (size_t) d > i) into[(size_t) d].push_back ({ i, Kind::Aux, k, afterInst });
        }
    }
    if (valid (masterIndex)) {
        const auto& m = nodes[(size_t) masterIndex];
        r.total = r.start[(size_t) masterIndex] + m.instLatency + m.preLatency + m.postLatency;
    }
    return r;
}

} // namespace pdc
} // namespace el
