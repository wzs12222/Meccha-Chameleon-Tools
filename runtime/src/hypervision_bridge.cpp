// HyperVision — C++ Bridge Extensions
// Included from bridge.cpp, uses its anonymous namespace and response_json.

namespace
{

// -----------------------------------------------------------------------
// Line Trace
// -----------------------------------------------------------------------

auto handle_line_trace(const std::string& payload) -> std::string
{
    // Parse: {"start":[x,y,z],"end":[x,y,z]}
    auto start_pos = payload.find("\"start\"");
    auto end_pos = payload.find("\"end\"");
    if (start_pos == std::string::npos || end_pos == std::string::npos)
        return response_json(false, "line_trace", 0, 1, "missing start/end");

    // Read coordinates from JSON (simple parsing)
    auto read_vec3 = [&](size_t pos) -> std::tuple<double, double, double> {
        pos = payload.find('[', pos);
        if (pos == std::string::npos) return {0,0,0};
        double x, y, z;
        sscanf_s(payload.c_str() + pos + 1, "%lf,%lf,%lf", &x, &y, &z);
        return {x, y, z};
    };

    auto [sx, sy, sz] = read_vec3(start_pos);
    auto [ex, ey, ez] = read_vec3(end_pos);

    // TODO: Call UWorld->LineTraceSingleByChannel
    // This requires a pointer to the world and proper SDK types.
    // For now return a stub that validates the bridge is reachable.
    std::string meta = "\"start\":[" + std::to_string(sx) + "," + std::to_string(sy) + "," + std::to_string(sz) + "]";
    meta += ",\"end\":[" + std::to_string(ex) + "," + std::to_string(ey) + "," + std::to_string(ez) + "]";
    meta += ",\"status\":\"sdk_line_trace_pending_implementation\"";
    return response_json(true, "line_trace", 1, 0, "line trace stub", meta);
}

// -----------------------------------------------------------------------
// Scan Terrain
// -----------------------------------------------------------------------
// Iterates world StaticMesh actors, reads bounds, slices at Z levels.
// Returns segments as JSON array.

auto handle_scan_terrain(const std::string& payload) -> std::string
{
    // Parse input
    auto center_pos = payload.find("\"center\"");
    auto range_pos = payload.find("\"range_xy\"");
    auto zsamp_pos = payload.find("\"z_samples\"");
    auto zrange_pos = payload.find("\"z_range\"");

    double cx = 0, cy = 0, cz = 0;
    double range_xy = 5000.0;
    int z_samples = 3;
    double z_range = 1000.0;

    if (center_pos != std::string::npos)
        sscanf_s(payload.c_str() + center_pos + 9, "%lf,%lf,%lf", &cx, &cy, &cz);
    if (range_pos != std::string::npos)
        sscanf_s(payload.c_str() + range_pos + 10, "%lf", &range_xy);
    if (zsamp_pos != std::string::npos)
        sscanf_s(payload.c_str() + zsamp_pos + 11, "%d", &z_samples);
    if (zrange_pos != std::string::npos)
        sscanf_s(payload.c_str() + zrange_pos + 9, "%lf", &z_range);

    double z_min = cz - z_range * 0.5;
    double z_max = cz + z_range * 0.5;
    double z_step = (z_max - z_min) / std::max(1, z_samples);

    // TODO: Iterate UWorld->PersistentLevel->Actors
    // For each UStaticMeshComponent, read Bounds (origin + extent)
    // Compute AABB intersection with each Z slice plane
    // Emit wall segments for radar

    // Stub: return empty segments list
    return response_json(true, "scan_terrain", 0, 0,
                         "terrain scan stub — implement world actor iteration",
                         "\"segments\":[],\"z_min\":" + std::to_string(z_min) +
                         ",\"z_max\":" + std::to_string(z_max) +
                         ",\"z_samples\":" + std::to_string(z_samples));
}

// -----------------------------------------------------------------------
// Visibility Scan
// -----------------------------------------------------------------------
// Spherical LineTrace sampling around target. Returns exposure point cloud.

auto handle_visibility_scan(const std::string& payload) -> std::string
{
    // Parse: {"target":[x,y,z],"step":80,"z_layers":20,"radius":2000}
    auto target_pos = payload.find("\"target\"");
    double tx = 0, ty = 0, tz = 0;
    double step = 80.0;
    int z_layers = 20;
    double radius = 2000.0;

    if (target_pos != std::string::npos)
        sscanf_s(payload.c_str() + target_pos + 9, "%lf,%lf,%lf", &tx, &ty, &tz);
    auto step_pos = payload.find("\"step\"");
    if (step_pos != std::string::npos) sscanf_s(payload.c_str() + step_pos + 7, "%lf", &step);
    auto zl_pos = payload.find("\"z_layers\"");
    if (zl_pos != std::string::npos) sscanf_s(payload.c_str() + zl_pos + 11, "%d", &z_layers);
    auto rad_pos = payload.find("\"radius\"");
    if (rad_pos != std::string::npos) sscanf_s(payload.c_str() + rad_pos + 9, "%lf", &radius);

    // TODO: For each point in spherical grid around (tx,ty,tz):
    //   LineTrace from sample point to target
    //   If no hit → mark as exposure point
    //   Collect into exposure_cloud

    std::string meta = "\"target\":[" + std::to_string(tx) + "," + std::to_string(ty) + "," + std::to_string(tz) + "]";
    meta += ",\"step\":" + std::to_string(step);
    meta += ",\"z_layers\":" + std::to_string(z_layers);
    meta += ",\"radius\":" + std::to_string(radius);
    meta += ",\"exposure_count\":0";
    return response_json(true, "visibility_scan", 0, 0,
                         "visibility scan stub — implement spherical LineTrace sampling", meta);
}

// -----------------------------------------------------------------------
// Path Find (A* on exposure graph)
// -----------------------------------------------------------------------

auto handle_path_find(const std::string& payload) -> std::string
{
    // Parse: {"player_pos":[...],"target_pos":[...],"exposure_cloud":[[...],...]}
    // TODO: Build graph from exposure cloud, run A*, return paths
    return response_json(true, "path_find", 0, 0,
                         "path find stub — implement A* on exposure graph",
                         "\"paths\":[],\"path_count\":0");
}

} // anonymous namespace
