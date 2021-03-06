#include "KdTreeAcc.h"
#include "Triangle.h"
#include "Sphere.h"
#include "Utils.h"

#include <algorithm>

KdTreeAcc::~KdTreeAcc()
{
    deleteTree(root);
}

bool cmpGeometryXAxis(const Geometry *g1, const Geometry *g2)
{
    return g1->getCenter().x < g2->getCenter().x;
}

bool cmpGeometryYAxis(const Geometry *g1, const Geometry *g2)
{
    return g1->getCenter().y < g2->getCenter().y;
}

bool cmpGeometryZAxis(const Geometry *g1, const Geometry *g2)
{
    return g1->getCenter().z < g2->getCenter().z;
}

bool cmpKdEvent(const KdTreeAcc::KdEvent a, const KdTreeAcc::KdEvent b)
{
    return (a.position < b.position) || 
        ((a.position == b.position) && (int)a.type < (int)b.type);
}

void KdTreeAcc::buildKdTree(KdNode *node, std::vector<Geometry *> &list, int depth, int &numLeaves, int &leafElements)
{
#define DUMP_TREE 0

    if (list.size() <= 8 || depth > 18) // This should be leaf node
    {
#if DUMP_TREE
        Utils::SysDbgPrint("%02d ", depth);
        for (int i = 0; i < depth; i++)
        {
            Utils::SysDbgPrint("  ");
        }
        Utils::SysDbgPrint("Leaf (%d)\n", list.size());
#endif
        node->axis = NoAxis;
        node->splitPlane = 0.0f; // whatever
        node->left = NULL;
        node->right = NULL;
        node->list.assign(list.begin(), list.end());
        numLeaves += 1;
        leafElements += list.size();
        return;
    }

    // Split the triangle list
    int axis;
    double median;
    double sah;

#define __STR2__(x) #x
#define __STR1__(x) __STR2__(x)
#define __LOC__ __FILE__ "("__STR1__(__LINE__)") : Warning: "
#pragma message(__LOC__"Fix this part later")

    if (0) // tunnel->algorithm == Tunnel::KdTreeStandard)
    {
        // Select axis based on depth so that axis cycles through all valid values
        // use order x -> y -> z -> x ...
        axis = depth % 3;
        median = split(node, axis, list); // the list may be sorted in split(), 
                                          // but no element would be deleted
    }
    else // KdTreeSAH
    {
        median = splitSAH(node, list, axis, sah);

        // Automatic termination
        if (sah > 1.5f * list.size())
        {
            node->axis = NoAxis;
            node->splitPlane = 0.0f; // whatever
            node->left = NULL;
            node->right = NULL;
            node->list.assign(list.begin(), list.end());
            numLeaves += 1;
            leafElements += list.size();
            return;
        }
    }

    // Create node and construct subtrees
    node->axis = (Axes)axis;
    node->splitPlane = median;
    node->left = new KdNode();
    node->right = new KdNode();
    // node->list remains empty

    node->left->min = node->min;
    node->left->max = node->max;
    node->right->min = node->min;
    node->right->max = node->max;

    node->left->max[axis] = median;
    node->right->min[axis] = median;

#if DUMP_TREE
    Utils::SysDbgPrint("%02d ", depth);
    for (int i = 0; i < depth; i++)
    {
        Utils::SysDbgPrint("  ");
    }
    if (axis == 0)
        Utils::SysDbgPrint("X (%d) split_plane = %.2f\n", list.size(), median);
    else if (axis == 1)
        Utils::SysDbgPrint("Y (%d) split_plane = %.2f\n", list.size(), median);
    else // axis == 2
        Utils::SysDbgPrint("Z (%d) split_plane = %.2f\n", list.size(), median);
#endif

    std::vector<Geometry *> leftPart;
    std::vector<Geometry *> rightPart;

    for (unsigned int i = 0; i < list.size(); i++)
    {
        if (list[i]->type == TRIANGLE)
        {
            Triangle *t = (Triangle *)list[i];

            if (t->a[axis] < median ||
                t->b[axis] < median ||
                t->c[axis] < median)
            {
                leftPart.push_back(t);
            }

            if (t->a[axis] >= median ||
                t->b[axis] >= median ||
                t->c[axis] >= median)
            {
                rightPart.push_back(t);
            }
        }
        else if (list[i]->type == SPHERE)
        {
            Sphere *s = (Sphere *)list[i];

            if (s->center[axis] - s->radius < median)
            {
                leftPart.push_back(s);
            }

            if (s->center[axis] + s->radius >= median)
            {
                rightPart.push_back(s);
            }
        }
    }

    buildKdTree(node->left, leftPart, depth + 1, numLeaves, leafElements);
    buildKdTree(node->right, rightPart, depth + 1, numLeaves, leafElements);
}

void KdTreeAcc::deleteTree(KdNode *node)
{
    if (node->left != NULL)
        deleteTree(node->left);
    if (node->right != NULL)
        deleteTree(node->right);
    delete node;
}

double KdTreeAcc::split(KdNode *node, int axis, std::vector<Geometry *> &list)
{
    // simple split: sort the triangle by barycenter, and select the mid value in the sorted list
    if (axis == XAxis)
    {
        std::sort(list.begin(), list.end(), cmpGeometryXAxis);
    }
    else if (axis == YAxis)
    {
        std::sort(list.begin(), list.end(), cmpGeometryYAxis);
    }
    else // ZAxis
    {
        std::sort(list.begin(), list.end(), cmpGeometryZAxis);
    }

    // Select the median point
    return list[list.size() / 2]->getCenter()[axis];
}

double KdTreeAcc::splitSAH(KdNode *node, std::vector<Geometry *> &list, int &bestAxis, double &minSAH)
{
    minSAH = DBL_MAX;
    double minPosition;

    for (int axis = 0; axis < 3; axis++)
    {
        // Generate event list
        std::vector<KdEvent> events;

        for (unsigned int i = 0; i < list.size(); i++)
        {
            // Clip triangle to box
            Point min, max;
            list[i]->getBoundingBox(min, max);

            if (min[axis] == max[axis])
            {
                events.push_back(KdEvent(list[i], min[axis], Planar));
            }
            else
            {
                events.push_back(KdEvent(list[i], min[axis], Start));
                events.push_back(KdEvent(list[i], max[axis], End));
            }
        }

        // Sort event list
        std::sort(events.begin(), events.end(), cmpKdEvent);

        // Sweep all candidate split planes
        int NL = 0;
        int NP = 0;
        int NR = list.size();

        for (unsigned int i = 0; i < events.size(); )
        {
            double position = events[i].position;
            int PS = 0; // p(+) p_start
            int PE = 0; // p(-) p_end
            int PP = 0; // p(|) p_planar

            while (i < events.size() && 
                events[i].position == position && events[i].type == End)
            {
                PE += 1; i += 1;
            }

            while (i < events.size() && 
                events[i].position == position && events[i].type == Planar)
            {
                PP += 1; i += 1;
            }

            while (i < events.size() && 
                events[i].position == position && events[i].type == Start)
            {
                PS += 1; i += 1;
            }

            // Move plane onto p
            NP = PP; NR -= PP; NR -= PE;
            
            // Calculate SAH
            // KT: Traversal constant (1)
            // KI: Intersection constant (1.5)
            // SA: Surface area (total)
            // SAL: Surface area (left)
            // SAR: Surface area (right)
            // Cost = KT + KI * ((SAL / SA) * (NL + NP) + (SAR / SA) * NR)
            int nextAxis = (axis + 1) % 3; // x -> y -> z -> x ...
            int prevAxis = (axis + 2) % 3; // z -> y -> x -> z ...
            Vector boxSize = Vector(node->min, node->max);

            double width = node->max[axis] - node->min[axis];
            double leftWidth = position - node->min[axis];
            double rightWidth = node->max[axis] - position;
            double height = boxSize[nextAxis];
            double depth = boxSize[prevAxis];

            double SAL = leftWidth * height + leftWidth * depth + height * depth;
            double SAR = rightWidth * height + rightWidth * depth + height * depth;
            double SA = width * height + width * depth + height * depth;

            double SAH = 1 + 1.5f * ((SAL / SA) * NL + SAR / SA * (NR + NP));
            if (SAH < minSAH)
            {
                minSAH = SAH;
                minPosition = position;
                bestAxis = axis;
            }

            NL += PS; NL += PP; NP = 0;
        }
    }

    return minPosition;
}

void KdTreeAcc::init()
{
    Utils::PrintTime("Initialize k-d tree");

    root = new KdNode();

    // Copy list
    std::vector<Geometry *> list = *scene;

    // Init the boundry of the root node
    double min_x = DBL_MAX, min_y = DBL_MAX, min_z = DBL_MAX;
    double max_x = -DBL_MAX, max_y = -DBL_MAX, max_z = -DBL_MAX;

    for (unsigned int i = 0; i < list.size(); i++)
    {
        Point min, max;
        list[i]->getBoundingBox(min, max);

        min_x = std::min(min_x, min.x);
        min_y = std::min(min_y, min.y);
        min_z = std::min(min_z, min.z);

        max_x = std::max(max_x, max.x);
        max_y = std::max(max_y, max.y);
        max_z = std::max(max_z, max.z);
    }

    root->min = Point(min_x, min_y, min_z);
    root->max = Point(max_x, max_y, max_z);

    // Build the tree
    int leaves = 0;
    int leafElements = 0;
    buildKdTree(root, list, 0, leaves, leafElements);
    Utils::DbgPrint("Total leaves: %d\r\n", leaves);
    Utils::DbgPrint("Average Leaf Size: %d\r\n", leafElements / leaves);
}

// The recursive ray traversal algorithm TA_rec_B for the k-d tree
// "Heuristic Ray Shooting Algorithms" by Vlastimil Vavran (Appendix C)
IntersectResult KdTreeAcc::intersect(Ray &ray, std::vector<RxIntersection> &rxPoints)
{
    // Based on the C-pseudo code in page 157
    double a; // entry signed distance
    double b; // exit signed distance
    double t; // signed distance to the splitting plane

    // Intersect ray with sceneBox, find the entry and exit signed distance
    Grid sceneBox(root->min, root->max);
    if (!sceneBox.intersect(ray, a, b))
        return IntersectResult(false);

    // Stack required for traversal to store far children
    StackElem stack[50];

    // Pointers to the far child node and current node
    KdNode *farChild;
    KdNode *currNode;
    currNode = root;

    // Setup initial entry point
    int enPt = 0;
    stack[enPt].t = a; // Set the signed distance

    // Distinguish between internal and external origin
    if (a >= 0) // a ray with external origin
        stack[enPt].pb = ray.origin + ray.direction * a;
    else // a ray with internal origin
        stack[enPt].pb = ray.origin;

    // Setup initial exit point in the stack
    int exPt = 1;
    stack[exPt].t = b;
    stack[exPt].pb = ray.origin + ray.direction * b;
    stack[exPt].node = NULL; // Termination flag

    std::map<int, RxSphereInfo> rxIntersections;

    // Loop, traverse through the whole kd-tree
    while (currNode != NULL)
    {
        // Loop until a leaf is found
        while (currNode->axis != NoAxis)
        {
            // Retrive position of splitting plane
            double splitVal = currNode->splitPlane;

            // The current axis
            int axis = (int)currNode->axis;
            int nextAxis = (axis + 1) % 3; // x -> y -> z -> x ...
            int prevAxis = (axis + 2) % 3; // z -> y -> x -> z ...

            if (stack[enPt].pb[axis] <= splitVal)
            {
                // Case N1, N2, N3, P5, Z2 and Z3
                if (stack[exPt].pb[axis] <= splitVal)
                {
                    currNode = currNode->left;
                    continue;
                }

                // Case Z1
                if (stack[exPt].pb[axis] == splitVal)
                {
                    currNode = currNode->right;
                    continue;
                }

                // Case N4
                farChild = currNode->right;
                currNode = currNode->left;
            }
            else // stack[enPt].pb[axis] > splitVal
            {
                // Case P1, P2, P3 and N5
                if (splitVal < stack[exPt].pb[axis])
                {
                    currNode = currNode->right;
                    continue;
                }

                // Case P4
                farChild = currNode->left;
                currNode = currNode->right;
            }
            // Case P4 or N4 (traverse both children)

            // Signed distance to the splitting plane
            t = (splitVal - ray.origin[axis]) / ray.direction[axis];

            // Setup the new exit point
            int tmp = exPt++;
            if (exPt == enPt)
                exPt += 1;

            // Push values onto the stack
            stack[exPt].prev = tmp;
            stack[exPt].t = t;
            stack[exPt].node = farChild;
            stack[exPt].pb[axis] = splitVal;
            stack[exPt].pb[nextAxis] = ray.origin[nextAxis] + t * ray.direction[nextAxis];
            stack[exPt].pb[prevAxis] = ray.origin[prevAxis] + t * ray.direction[prevAxis];
        }

        // Current node is the leaf, empty or full
        double minDistance = DBL_MAX;
        IntersectResult minResult(false);

        for (unsigned int i = 0; i < currNode->list.size(); i++)
        {
            IntersectResult result = currNode->list[i]->intersect(ray);
            if (result.hit &&
                result.distance >= stack[enPt].t - 0.001f && 
                result.distance <= stack[exPt].t + 0.001f)
            {
                if (result.geometry->type == SPHERE && // rx sphere
                    result.distance < minDistance)
                {
                    RxSphere *s = (RxSphere *)result.geometry;
                    rxIntersections[s->index].distance = result.distance;
                    rxIntersections[s->index].offset = Vector(result.position, s->center).length();
                    rxIntersections[s->index].radius = s->radius;
                }
                else // triangle
                {
                    if (result.distance < minDistance) 
                    {
                        minDistance = result.distance;
                        minResult = result;
                    }
                }
            }
        }
        
        if (minResult.hit)
        {
            std::map<int, RxSphereInfo>::iterator it;
            for (it = rxIntersections.begin(); it != rxIntersections.end(); ++it)
            {
                if (it->second.distance < minDistance)
                {
                    rxPoints.push_back(
                        RxIntersection(it->first, it->second.distance, it->second.offset, it->second.radius));
                }
            }
        
            return minResult;
        }

        // Pop from stack
        enPt = exPt; // The signed distance intervals are adjacent

        // Retrieve the pointer to the next node,
        // it is possible that ray traversal terminates
        currNode = stack[exPt].node;

        exPt = stack[enPt].prev;
    }

    // Intersect with no triangles
    std::map<int, RxSphereInfo>::iterator it;
    for (it = rxIntersections.begin(); it != rxIntersections.end(); ++it)
    {
        rxPoints.push_back(
            RxIntersection(it->first, it->second.distance, it->second.offset, it->second.radius));
    }

    // currNode = NULL, ray leaves the scene
    return IntersectResult(false);
}
