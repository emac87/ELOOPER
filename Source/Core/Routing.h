#pragma once
#include "Types.h"
#include <vector>

namespace el {

// Directed signal-flow graph between tracks/buses: edge a -> b means "a feeds b"
// (main output, send, or multi-out parent -> aux track).
struct RouteEdge { Id from; Id to; };

namespace routing {
    // Would adding from -> to create a feedback loop?
    bool wouldCreateCycle (const std::vector<RouteEdge>& edges, Id from, Id to);
    // Processing order: every node after all nodes feeding it. Nodes on a cycle (which the
    // controller never allows) are appended at the end in input order. Returns false if a cycle was found.
    bool topologicalOrder (const std::vector<Id>& nodes, const std::vector<RouteEdge>& edges, std::vector<Id>& order);
}

} // namespace el
