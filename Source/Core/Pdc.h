#pragma once
// Plug-in delay compensation (PDC): works out how much every signal path into a summing node
// (bus, master, aux-out) must be delayed so that all of them line up, and provides the
// fixed-delay lines the audio thread uses to apply it. No JUCE, no allocation in process().
#include <vector>

namespace el {

// Single-channel fixed delay. prepare() allocates (message thread); process() is real-time safe.
class DelayLine {
public:
    void prepare (int delaySamples);
    void reset() noexcept;
    int delay() const noexcept { return delaySamples; }
    void process (float* data, int numSamples) noexcept;   // in place
    void copyStateFrom (const DelayLine& other);           // keeps audio flowing across rebuilds
private:
    std::vector<float> buf;
    int delaySamples = 0, writePos = 0;
};

struct StereoDelay {
    DelayLine l, r;
    void prepare (int d) { l.prepare (d); r.prepare (d); }
    int delay() const noexcept { return l.delay(); }
    void process (float* left, float* right, int n) noexcept { l.process (left, n); r.process (right, n); }
    void copyStateFrom (const StereoDelay& o) { l.copyStateFrom (o.l); r.copyStateFrom (o.r); }
};

namespace pdc {

// One node of the render graph, in processing (topological) order.
struct Node {
    int instLatency = 0, preLatency = 0, postLatency = 0;   // samples
    int outIndex = -1;                                      // node the main output sums into (-1: none / hardware)
    struct Send { int dest = -1; bool preFader = false; };
    std::vector<Send> sends;
    std::vector<int> auxChildren;                           // nodes fed straight from the instrument output
};

struct Result {
    std::vector<int> start;                    // delay of the signal entering each node
    std::vector<int> outDelay;                 // padding before the main output reaches outIndex
    std::vector<std::vector<int>> sendDelay;   // padding per send (same order as Node::sends)
    std::vector<std::vector<int>> auxDelay;    // padding per aux child (same order as Node::auxChildren)
    int total = 0;                             // latency of the whole mix at `masterIndex`'s output
};

Result compute (const std::vector<Node>& nodes, int masterIndex);

} // namespace pdc
} // namespace el
