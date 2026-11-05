#include "Routing.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace el::routing {

bool wouldCreateCycle (const std::vector<RouteEdge>& edges, Id from, Id to) {
    if (from == to) return true;
    // Is `from` reachable from `to`?
    std::vector<Id> stack { to };
    std::unordered_set<Id> seen;
    while (! stack.empty()) {
        const Id n = stack.back(); stack.pop_back();
        if (n == from) return true;
        if (! seen.insert (n).second) continue;
        for (const auto& e : edges) if (e.from == n) stack.push_back (e.to);
    }
    return false;
}

bool topologicalOrder (const std::vector<Id>& nodes, const std::vector<RouteEdge>& edges, std::vector<Id>& order) {
    std::unordered_map<Id, int> indegree;
    for (auto n : nodes) indegree[n] = 0;
    for (const auto& e : edges)
        if (indegree.count (e.from) && indegree.count (e.to)) ++indegree[e.to];
    order.clear();
    std::vector<Id> ready;
    for (auto n : nodes) if (indegree[n] == 0) ready.push_back (n);
    std::unordered_set<Id> placed;
    // Keep input order stable among ready nodes.
    while (! ready.empty()) {
        const Id n = ready.front(); ready.erase (ready.begin());
        order.push_back (n); placed.insert (n);
        for (auto m : nodes) {
            bool fed = false;
            for (const auto& e : edges) if (e.from == n && e.to == m) { fed = true; if (--indegree[m] == 0) ready.push_back (m); }
            (void) fed;
        }
    }
    const bool ok = order.size() == nodes.size();
    for (auto n : nodes) if (! placed.count (n)) order.push_back (n);
    return ok;
}

} // namespace el::routing
