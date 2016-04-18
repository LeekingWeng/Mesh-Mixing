#include <Eigen/Sparse>
#include "Mixer.h"
#include "Mapping.h"
#include "internal/SurfaceMeshVerticesKDTree.h"
#include "Smoother.h"
using namespace OpenGP;

/// Applies the high frequency details (ie texture) of meshFrom to the surface of meshTo
/// using laplacian coordinates and surface editing
SurfaceMesh Mixer::ApplyCoating(SurfaceMesh& meshFrom, SurfaceMesh& meshTo,
                                SurfaceMesh::Vertex_property<Vec2> meshFromMap,
                                SurfaceMesh::Vertex_property<Vec2> meshToMap)
{
    /* TODO:
     *  - use the uv coordinates to map vertices from one mesh to the other
     *  - set the result map to be equal to the meshTo mesh
     *  - capture the high freq details of meshFrom (S) by diffing it with a smoothed version of itself (S*)
     *  - encode the coating based on laplacian coordinates L (of S) and L* (of S*) then encoding of vertex i is L - L*
     *  - meshTo (U). Align S to U using surface normals of S* and U* (in our case U is a sphere and U = U*)
     *  - add L - L* to the laplacian of U, with respect to rotation alignment R for a vertex j where i maps to j
     *  - use the new laplacian to get the new vertex position
     *  - ???
     *  - profit!
     */

    // copy the mesh we're applying the coating to
    SurfaceMesh resultMesh(meshTo);

    //map from the vertex we're applying the coating to, to the mesh with the coating
    std::map<SurfaceMesh::Vertex, SurfaceMesh::Vertex> vertexMappingUtoS
            = MapUVs(meshTo, meshFrom, meshToMap, meshFromMap);

    SurfaceMesh::Vertex_property<Vec3> differentialsFrom = meshFrom.add_vertex_property("differentials", Vec3());
    ComputeDifferentials(meshFrom, differentialsFrom);

    //create smoothed copy of the coating mesh
    SurfaceMesh smoothFrom = SmoothCopy(meshFrom, 40);
    SurfaceMesh::Vertex_property<Vec3> differentialsSmoothFrom = smoothFrom.add_vertex_property("SmoothDifferentials", Vec3());
    ComputeDifferentials(smoothFrom, differentialsSmoothFrom);

    //calculate differences from original coating mesh to the smoothing mesh
    SurfaceMesh::Vertex_property<Vec3> diffDiff = smoothFrom.add_vertex_property("Chi", Vec3());

    for (auto v : smoothFrom.vertices())
    {
        diffDiff[v] = differentialsFrom[v] - differentialsSmoothFrom[v];
    }

    SurfaceMesh::Vertex_property<Vec3> differentialsSphere = meshTo.add_vertex_property("SphereDifferentials", Vec3());
    ComputeDifferentials(meshTo, differentialsSphere);

    //update vertex normals of meshes for orientation
    meshFrom.update_vertex_normals();
    resultMesh.update_vertex_normals();

    for (auto m : vertexMappingUtoS)
    {

    }

    Mixer::ApplyLaplacianShift(meshFrom, meshTo, vertexMappingUtoS);

    return resultMesh;
}

void Mixer::ApplyLaplacianShift(meshFrom, meshTo, vertexMappingUtoS)
{
    for (std::pair<SurfaceMesh::Vertex, SurfaceMesh::Vertex> v : vertexMappingUtoS)
    {

    }
}

/// Takes two meshes and their corresponding UV maps to return a vertex mapping from
/// meshFrom to meshTo. The mapping is not 1:1 nor onto.
std::map<SurfaceMesh::Vertex, SurfaceMesh::Vertex> Mixer::MapUVs(SurfaceMesh const& meshTo, SurfaceMesh const& meshFrom,
                                                                 SurfaceMesh::Vertex_property<Vec2> meshToMap,
                                                                 SurfaceMesh::Vertex_property<Vec2> meshFromMap)
{
    std::cout << "UVMapping begins:" << std::endl;

    std::map<SurfaceMesh::Vertex, SurfaceMesh::Vertex> vertexMap;

    std::vector<std::pair<SurfaceMesh::Vertex, SurfaceMesh::Vertex>> vertexMapping;

    // Construct 3D mesh representative of meshFrom's uv mapping
    SurfaceMesh flatU(meshFrom);
    for (auto vertU : meshTo.vertices())
    {
        Vec2 tempv2 = meshFromMap[vertU];
        flatU.position(vertU) = Vec3(tempv2[0], tempv2[1], 0);
    }

    SurfaceMeshVerticesKDTree meshFromTree(flatU);

    // Find closest vertex mapping via uv coordinates
    size_t vertexID = 0;
    for (auto vertS : meshTo.vertices())
    {
        Vec2 S = meshToMap[vertS];
        vertexMapping.push_back( std::make_pair(vertS, SurfaceMesh::Vertex( meshFromTree.closest_vertex( Vec3(S[0], S[1], 0)).idx() ) ) );

        // Progress statement
        vertexID++;
        PercentProgress(meshFrom.n_vertices(), vertexID);
    }

    vertexMap.insert(begin(vertexMapping), end(vertexMapping));

    std::cout << "UVMapping finished." << std::endl;

    return vertexMappingStoU;
}

/// Computes the laplacian coordinates using cotangent weights
void Mixer::ComputeDifferentials(SurfaceMesh const& mesh, SurfaceMesh::Vertex_property<Vec3>& differentials)
{
    std::cout << "Computing differentials:" << std::endl;

    // Compute the Laplacian Coordinates of a mesh using uniform weights
    for (auto v_i : mesh.vertices())
    {
        Vec3 p = mesh.position(v_i);
        int valence = 0;
        Vec3 sum(0.0f,0.0f,0.0f);
        for (auto v_j : mesh.vertices(v_i))
        {
            valence++;
            sum += mesh.position(v_j);
        }

        differentials[v_i] = p - sum * ( 1.0 / (float) valence);
    }
    /*
    Smoother s = Smoother(mesh);
    s.init();
    s.use_cotan_laplacian();
    */
    std::cout << "Differentials finished." << std::endl;
}

SurfaceMesh Mixer::SmoothCopy(SurfaceMesh const& mesh, int iterations)
{
    std::cout << "Creating smooth copy:" << std::endl;

    SurfaceMesh smoothMeshFrom(mesh);
    Smoother smoother = Smoother(smoothMeshFrom);

    smoothMeshFrom.update_face_normals();
    smoothMeshFrom.update_vertex_normals();

    smoother.init();
    smoother.use_graph_laplacian();

    for (int i = 0; i < iterations; i++)
    {
        smoother.smooth_explicit(0.1f);
        PercentProgress(iterations, i+1);
    }

    std::cout << "Smooth copy finished." << std::endl;

    return smoothMeshFrom;
}

Eigen::Matrix3f Mixer::ComputeRotationMatrix(Vec3 normA, Vec3 normB)
{
    Eigen::Matrix3f R;
    R = Eigen::Quaternionf().setFromTwoVectors(normA, normB);
    return R;
}

void Mixer::PercentProgress(int size, int iternum)
{
    if (iternum % (size/10) == 0)
    {
        std::cout << "progress: " << (iternum/(size/10))*10 << "%" << std::endl;
    }
}

