#include "Navigation.hpp"
#include <algorithm>
#include <cmath>

namespace CatEngine {

// ============================================================================
// NavTriangle Implementation
// ============================================================================

bool NavTriangle::contains(const vec3& point) const {
    // Use barycentric coordinates to check if point is inside triangle
    vec3 v0 = vertices[1] - vertices[0];
    vec3 v1 = vertices[2] - vertices[0];
    vec3 v2 = point - vertices[0];

    float dot00 = v0.dot(v0);
    float dot01 = v0.dot(v1);
    float dot02 = v0.dot(v2);
    float dot11 = v1.dot(v1);
    float dot12 = v1.dot(v2);

    float invDenom = 1.0f / (dot00 * dot11 - dot01 * dot01);
    float u = (dot11 * dot02 - dot01 * dot12) * invDenom;
    float v = (dot00 * dot12 - dot01 * dot02) * invDenom;

    return (u >= 0.0f) && (v >= 0.0f) && (u + v <= 1.0f);
}

vec3 NavTriangle::closestPoint(const vec3& point) const {
    // If point is inside triangle, return point
    if (contains(point)) {
        return point;
    }

    // Find closest point on triangle edges
    auto closestPointOnSegment = [](const vec3& p, const vec3& a, const vec3& b) -> vec3 {
        vec3 ab = b - a;
        float t = (p - a).dot(ab) / ab.dot(ab);
        t = std::clamp(t, 0.0f, 1.0f);
        return a + ab * t;
    };

    vec3 c0 = closestPointOnSegment(point, vertices[0], vertices[1]);
    vec3 c1 = closestPointOnSegment(point, vertices[1], vertices[2]);
    vec3 c2 = closestPointOnSegment(point, vertices[2], vertices[0]);

    float d0 = (c0 - point).lengthSquared();
    float d1 = (c1 - point).lengthSquared();
    float d2 = (c2 - point).lengthSquared();

    if (d0 <= d1 && d0 <= d2) return c0;
    if (d1 <= d2) return c1;
    return c2;
}

// ============================================================================
// Pathfinder Implementation
// ============================================================================

Path Pathfinder::findPath(const vec3& start, const vec3& goal) {
    Path result;

    if (!navMesh_ || navMesh_->getTriangleCount() == 0) {
        return result;
    }

    // Find start and goal triangles
    int startTriId = navMesh_->findTriangle(start);
    int goalTriId = navMesh_->findTriangle(goal);

    if (startTriId == -1 || goalTriId == -1) {
        // Can't find path if start or goal is outside navmesh
        return result;
    }

    // If start and goal are in same triangle, direct path
    if (startTriId == goalTriId) {
        result.addWaypoint(start);
        result.addWaypoint(goal);
        return result;
    }

    // A* pathfinding
    size_t triCount = navMesh_->getTriangleCount();
    std::vector<Node> nodes(triCount);
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> openSet;
    std::vector<bool> inOpenSet(triCount, false);
    std::vector<bool> inClosedSet(triCount, false);

    // Initialize start node
    nodes[startTriId].triangleId = startTriId;
    nodes[startTriId].g = 0.0f;
    nodes[startTriId].h = heuristic(start, goal);
    nodes[startTriId].f = nodes[startTriId].h;
    nodes[startTriId].parent = -1;

    openSet.push(nodes[startTriId]);
    inOpenSet[startTriId] = true;

    // A* search
    while (!openSet.empty()) {
        Node current = openSet.top();
        openSet.pop();

        int currentId = current.triangleId;
        inOpenSet[currentId] = false;

        if (currentId == goalTriId) {
            // Found path - reconstruct it
            std::vector<int> trianglePath;
            int id = goalTriId;
            while (id != -1) {
                trianglePath.push_back(id);
                id = nodes[id].parent;
            }
            std::reverse(trianglePath.begin(), trianglePath.end());

            // Convert triangle path to waypoints
            result.addWaypoint(start);
            for (size_t i = 1; i < trianglePath.size(); ++i) {
                const NavTriangle* tri = navMesh_->getTriangle(trianglePath[i]);
                if (tri) {
                    result.addWaypoint(tri->center);
                }
            }
            result.addWaypoint(goal);

            return result;
        }

        if (inClosedSet[currentId]) {
            continue;
        }
        inClosedSet[currentId] = true;

        // Check neighbors
        const NavTriangle* currentTri = navMesh_->getTriangle(currentId);
        if (!currentTri) {
            continue;
        }

        for (int i = 0; i < 3; ++i) {
            int neighborId = currentTri->neighbors[i];
            if (neighborId == -1 || inClosedSet[neighborId]) {
                continue;
            }

            const NavTriangle* neighborTri = navMesh_->getTriangle(neighborId);
            if (!neighborTri) {
                continue;
            }

            float edgeCost = (neighborTri->center - currentTri->center).length();
            edgeCost *= neighborTri->cost;
            float tentativeG = current.g + edgeCost;

            if (!inOpenSet[neighborId]) {
                nodes[neighborId].triangleId = neighborId;
                nodes[neighborId].g = tentativeG;
                nodes[neighborId].h = heuristic(neighborTri->center, goal);
                nodes[neighborId].f = nodes[neighborId].g + nodes[neighborId].h;
                nodes[neighborId].parent = currentId;

                openSet.push(nodes[neighborId]);
                inOpenSet[neighborId] = true;
            } else if (tentativeG < nodes[neighborId].g) {
                nodes[neighborId].g = tentativeG;
                nodes[neighborId].f = nodes[neighborId].g + nodes[neighborId].h;
                nodes[neighborId].parent = currentId;

                openSet.push(nodes[neighborId]);
            }
        }
    }

    // No path found
    return result;
}

} // namespace CatEngine
