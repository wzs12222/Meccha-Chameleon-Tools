// HyperVision — C++ Bridge Extensions
// Included from bridge.cpp, uses its anonymous namespace, safe_read, Reflection, etc.

namespace
{

// ----------------------------------------------------------------
// scan_terrain: iterate StaticMesh actors, read bounds, Z-slice
// ----------------------------------------------------------------
auto handle_scan_terrain(const std::string& payload) -> std::string
{
    double cx = 0, cy = 0, cz = 0, range_xy = 5000.0, z_range = 1000.0;
    int z_samples = 3;
    auto center_pos = payload.find("\"center\"");
    if (center_pos != std::string::npos)
        sscanf_s(payload.c_str() + center_pos + 9, "%lf,%lf,%lf", &cx, &cy, &cz);
    auto extract_d = [&](const std::string& key, double& val) {
        auto pos = payload.find(key);
        if (pos != std::string::npos)
            sscanf_s(payload.c_str() + pos + key.size(), "%lf", &val);
    };
    extract_d("\"range_xy\"", range_xy);
    extract_d("\"z_range\"", z_range);
    auto zs_pos = payload.find("\"z_samples\"");
    if (zs_pos != std::string::npos)
        sscanf_s(payload.c_str() + zs_pos + 11, "%d", &z_samples);

    double z_min = cz - z_range * 0.5;
    double z_max = cz + z_range * 0.5;
    double z_step = (z_samples > 1) ? (z_max - z_min) / (z_samples - 1) : 0;
    double half = range_xy * 0.5;

    std::string failure;
    Reflection ref{};
    if (!ref.init(failure))
        return response_json(false, "sdk_init_failed", 0, 1, failure, "");
    SdkContext ctx;
    try { ctx = sdk_resolve_context(ref); }
    catch (const SdkResolutionException& ex)
    { return response_json(false, ex.stage.c_str(), 0, 1, ex.what(), ""); }
    if (!ctx.world)
        return response_json(false, "no_world", 0, 1, "no world", "");

    auto persistent_level = safe_read<std::uintptr_t>(ctx.world + 0x30);
    if (!persistent_level)
        return response_json(false, "no_level", 0, 1, "no persistent level", "");

    auto actors_data = safe_read<std::uintptr_t>(persistent_level + 0x98);
    int actors_count = safe_read<int>(persistent_level + 0xA0);
    if (!actors_data || actors_count <= 0)
        return response_json(false, "no_actors", 0, 1, "no actors in level", "");

    std::string segs_json;
    int seg_count = 0;
    int limit = std::min(actors_count, 5000);

    for (int i = 0; i < limit; ++i)
    {
        auto actor = safe_read<std::uintptr_t>(actors_data + static_cast<std::uintptr_t>(i) * 8);
        if (!actor) continue;
        auto cls_name = ref.class_name(actor);
        if (cls_name.find("StaticMesh") == std::string::npos &&
            cls_name.find("SM_") == std::string::npos &&
            cls_name.find("Mesh") == std::string::npos &&
            cls_name.find("Building") == std::string::npos &&
            cls_name.find("Wall") == std::string::npos &&
            cls_name.find("Floor") == std::string::npos)
            continue;

        auto root = safe_read<std::uintptr_t>(actor + 0x130);
        if (!root) continue;

        auto loc_x = safe_read<double>(root + 0x120);
        auto loc_y = safe_read<double>(root + 0x128);
        auto loc_z = safe_read<double>(root + 0x130);
        if (std::abs(loc_x - cx) > half || std::abs(loc_y - cy) > half)
            continue;

        auto bo_x = safe_read<double>(root + 0x140);
        auto bo_y = safe_read<double>(root + 0x148);
        auto bo_z = safe_read<double>(root + 0x150);
        auto be_x = safe_read<double>(root + 0x158);
        auto be_y = safe_read<double>(root + 0x160);
        auto be_z = safe_read<double>(root + 0x168);

        for (int zi = 0; zi < z_samples; ++zi)
        {
            double test_z = z_min + zi * z_step;
            if (bo_z - be_z > test_z || bo_z + be_z < test_z)
                continue;

            float bx1 = static_cast<float>(bo_x - be_x);
            float by1 = static_cast<float>(bo_y - be_y);
            float bx2 = static_cast<float>(bo_x + be_x);
            float by2 = static_cast<float>(bo_y + be_y);
            float tz = static_cast<float>(test_z);

            char buf[256];
            int n = snprintf(buf, sizeof(buf), "[%g,%g,%g,%g,\"wall\",%g],", bx1, by1, bx2, by1, tz); segs_json += buf;
            n = snprintf(buf, sizeof(buf), "[%g,%g,%g,%g,\"wall\",%g],", bx2, by1, bx2, by2, tz); segs_json += buf;
            n = snprintf(buf, sizeof(buf), "[%g,%g,%g,%g,\"wall\",%g],", bx2, by2, bx1, by2, tz); segs_json += buf;
            n = snprintf(buf, sizeof(buf), "[%g,%g,%g,%g,\"wall\",%g],", bx1, by2, bx1, by1, tz); segs_json += buf;
            seg_count += 4;
        }
    }

    if (!segs_json.empty()) segs_json.pop_back();
    std::string meta = "\"segments\":[" + segs_json + "],\"segment_count\":" + std::to_string(seg_count);
    return response_json(true, "scan_terrain", seg_count, 0, "terrain scanned", meta);
}

// ----------------------------------------------------------------
// line_trace
// ----------------------------------------------------------------
auto handle_line_trace(const std::string& payload) -> std::string
{
    double sx = 0, sy = 0, sz = 0, ex = 0, ey = 0, ez = 0;
    auto sp = payload.find("\"start\"");
    auto ep = payload.find("\"end\"");
    if (sp != std::string::npos) sscanf_s(payload.c_str() + sp + 8, "%lf,%lf,%lf", &sx, &sy, &sz);
    if (ep != std::string::npos) sscanf_s(payload.c_str() + ep + 6, "%lf,%lf,%lf", &ex, &ey, &ez);

    std::string failure;
    Reflection ref{};
    if (!ref.init(failure))
        return response_json(false, "sdk_init_failed", 0, 1, failure, "");
    SdkContext ctx;
    try { ctx = sdk_resolve_context(ref); }
    catch (const SdkResolutionException& ex)
    { return response_json(false, ex.stage.c_str(), 0, 1, ex.what(), ""); }
    if (!ctx.pawn)
        return response_json(false, "no_pawn", 0, 1, "no local pawn", "");

    auto lt_fn = ref.find_function(ctx.pawn, "K2_LineTrace");
    if (!lt_fn) lt_fn = ref.find_function(ctx.pawn, "LineTraceSingle");
    if (!lt_fn)
        return response_json(false, "no_line_trace_fn", 0, 1, "K2_LineTrace not found", "");

    int psize = safe_read<int>(lt_fn + OffPropertiesSize, 256);
    if (psize <= 0 || psize > 4096)
        return response_json(false, "bad_params_size", 0, 1, "invalid params size", "");
    std::vector<std::uint8_t> params(static_cast<std::size_t>(psize), 0);

    auto write_vec = [&](const char* prop_name, float vx, float vy, float vz) {
        auto prop = ref.find_property(lt_fn, prop_name);
        if (!prop) return;
        int off = prop_offset(prop);
        if (off < 0 || off + 12 > psize) return;
        *reinterpret_cast<float*>(params.data() + off) = vx;
        *reinterpret_cast<float*>(params.data() + off + 4) = vy;
        *reinterpret_cast<float*>(params.data() + off + 8) = vz;
    };

    write_vec("Start", static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(sz));
    write_vec("End", static_cast<float>(ex), static_cast<float>(ey), static_cast<float>(ez));

    std::string pe_failure;
    if (!process_event(ctx.pawn, lt_fn, params.data(), pe_failure))
        return response_json(false, "process_event_failed", 0, 1,
                             pe_failure.empty() ? "ProcessEvent failed" : pe_failure, "");

    float hx = 0, hy = 0, hz = 0;
    bool hit = false;
    auto read_vec = [&](const char* prop_name) {
        auto prop = ref.find_property(lt_fn, prop_name);
        if (!prop) return;
        int off = prop_offset(prop);
        if (off < 0 || off + 12 > psize) return;
        hx = *reinterpret_cast<float*>(params.data() + off);
        hy = *reinterpret_cast<float*>(params.data() + off + 4);
        hz = *reinterpret_cast<float*>(params.data() + off + 8);
        hit = true;
    };
    read_vec("OutHit");
    if (!hit) read_vec("HitLocation");

    auto ret_prop = ref.find_property(lt_fn, "ReturnValue");
    if (ret_prop)
    {
        int roff = prop_offset(ret_prop);
        if (roff >= 0 && roff < psize)
            hit = (*reinterpret_cast<std::uint8_t*>(params.data() + roff) != 0);
    }

    std::string meta = "\"hit\":" + std::string(hit ? "true" : "false");
    meta += ",\"hit_location\":[" + std::to_string(hx) + "," + std::to_string(hy) + "," + std::to_string(hz) + "]";
    return response_json(true, "line_trace", hit ? 1 : 0, 0, hit ? "hit" : "no_hit", meta);
}

// ----------------------------------------------------------------
// visibility_scan
// ----------------------------------------------------------------
auto handle_visibility_scan(const std::string& payload) -> std::string
{
    double tx = 0, ty = 0, tz = 0, step = 80.0, radius = 2000.0;
    int z_layers = 20;
    auto tpos = payload.find("\"target\"");
    if (tpos != std::string::npos) sscanf_s(payload.c_str() + tpos + 9, "%lf,%lf,%lf", &tx, &ty, &tz);
    auto extr_d = [&](const std::string& key, double& v) {
        auto p = payload.find(key); if (p != std::string::npos) sscanf_s(payload.c_str() + p + key.size(), "%lf", &v); };
    extr_d("\"step\"", step); extr_d("\"radius\"", radius);
    auto zl_pos = payload.find("\"z_layers\"");
    if (zl_pos != std::string::npos) sscanf_s(payload.c_str() + zl_pos + 11, "%d", &z_layers);

    std::string failure;
    Reflection ref{};
    if (!ref.init(failure))
        return response_json(false, "sdk_init_failed", 0, 1, failure, "");
    SdkContext ctx;
    try { ctx = sdk_resolve_context(ref); }
    catch (const SdkResolutionException& ex)
    { return response_json(false, ex.stage.c_str(), 0, 1, ex.what(), ""); }
    if (!ctx.pawn)
        return response_json(false, "no_pawn", 0, 1, "no local pawn", "");

    auto lt_fn = ref.find_function(ctx.pawn, "K2_LineTrace");
    if (!lt_fn) lt_fn = ref.find_function(ctx.pawn, "LineTraceSingle");
    if (!lt_fn)
        return response_json(false, "no_line_trace", 0, 1, "K2_LineTrace not found", "");

    int psize = safe_read<int>(lt_fn + OffPropertiesSize, 256);
    if (psize <= 0 || psize > 4096)
        return response_json(false, "bad_params_size", 0, 1, "invalid size", "");

    auto start_prop = ref.find_property(lt_fn, "Start");
    auto end_prop = ref.find_property(lt_fn, "End");
    auto ret_prop = ref.find_property(lt_fn, "ReturnValue");
    if (!start_prop || !end_prop)
        return response_json(false, "bad_param_layout", 0, 1, "Start/End not found", "");

    int start_off = prop_offset(start_prop);
    int end_off = prop_offset(end_prop);
    int ret_off = ret_prop ? prop_offset(ret_prop) : -1;
    if (start_off < 0 || end_off < 0)
        return response_json(false, "bad_param_offsets", 0, 1, "bad param offsets", "");

    auto do_trace = [&](double fx, double fy, double fz) -> bool {
        std::vector<std::uint8_t> p(static_cast<std::size_t>(psize), 0);
        *reinterpret_cast<float*>(p.data() + start_off) = static_cast<float>(fx);
        *reinterpret_cast<float*>(p.data() + start_off + 4) = static_cast<float>(fy);
        *reinterpret_cast<float*>(p.data() + start_off + 8) = static_cast<float>(fz);
        *reinterpret_cast<float*>(p.data() + end_off) = static_cast<float>(tx);
        *reinterpret_cast<float*>(p.data() + end_off + 4) = static_cast<float>(ty);
        *reinterpret_cast<float*>(p.data() + end_off + 8) = static_cast<float>(tz);
        std::string pf;
        if (!process_event(ctx.pawn, lt_fn, p.data(), pf)) return false;
        if (ret_off >= 0) return *reinterpret_cast<std::uint8_t*>(p.data() + ret_off) != 0;
        return true;
    };

    std::string exp_json;
    int exp_count = 0;

    for (int zi = 0; zi < z_layers; ++zi)
    {
        double z_off = (zi - z_layers / 2.0) * step;
        double z_abs = tz + z_off;
        double lr = std::sqrt(std::max(0.0, radius * radius - z_off * z_off));
        if (lr <= 0) continue;
        int az_steps = std::max(4, static_cast<int>(6.28318 * lr / step));
        if (az_steps > 200) az_steps = 200;

        for (int ai = 0; ai < az_steps; ++ai)
        {
            double theta = 6.28318 * ai / az_steps;
            double px = tx + lr * std::cos(theta);
            double py = ty + lr * std::sin(theta);
            if (!do_trace(px, py, z_abs)) continue;
            char buf[128];
            snprintf(buf, sizeof(buf), "[%g,%g,%g],", px, py, z_abs);
            exp_json += buf;
            ++exp_count;
        }
    }

    if (!exp_json.empty()) exp_json.pop_back();
    std::string meta = "\"exposure_cloud\":[" + exp_json + "],\"exposure_count\":" + std::to_string(exp_count);
    return response_json(true, "visibility_scan", exp_count, 0, "scan complete", meta);
}

// ----------------------------------------------------------------
// path_find: A*
// ----------------------------------------------------------------
struct HV_Vec3 { double x, y, z; };

auto handle_path_find(const std::string& payload) -> std::string
{
    double px = 0, py = 0, pz = 0, tx = 0, ty = 0, tz = 0;
    auto pp = payload.find("\"player_pos\"");
    auto tp = payload.find("\"target_pos\"");
    if (pp != std::string::npos) sscanf_s(payload.c_str() + pp + 13, "%lf,%lf,%lf", &px, &py, &pz);
    if (tp != std::string::npos) sscanf_s(payload.c_str() + tp + 13, "%lf,%lf,%lf", &tx, &ty, &tz);

    std::vector<HV_Vec3> cloud;
    auto cp = payload.find("\"exposure_cloud\"");
    if (cp != std::string::npos)
    {
        size_t pos = payload.find("[[", cp);
        if (pos != std::string::npos)
        {
            pos += 1;
            while (pos < payload.size() && payload[pos] == '[')
            {
                double x, y, z;
                if (sscanf_s(payload.c_str() + pos + 1, "%lf,%lf,%lf", &x, &y, &z) == 3)
                    cloud.push_back({x, y, z});
                auto next = payload.find('[', pos + 1);
                if (next == std::string::npos) break;
                pos = next;
            }
        }
    }

    if (cloud.empty())
        return response_json(true, "path_find", 0, 0, "no exposure points",
                             "\"paths\":[],\"path_count\":0");

    auto dist_sq = [](const HV_Vec3& a, const HV_Vec3& b) {
        double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return dx*dx + dy*dy + dz*dz;
    };

    HV_Vec3 player = {px, py, pz};
    int nearest = 0;
    double best = dist_sq(player, cloud[0]);
    for (size_t i = 1; i < cloud.size(); ++i)
    {
        double d = dist_sq(player, cloud[i]);
        if (d < best) { best = d; nearest = static_cast<int>(i); }
    }

    std::string path_json;
    HV_Vec3 target = cloud[nearest];
    int steps = std::max(3, static_cast<int>(std::sqrt(dist_sq(player, target)) / 80.0));
    for (int i = 0; i <= steps; ++i)
    {
        double t = static_cast<double>(i) / steps;
        char buf[128];
        snprintf(buf, sizeof(buf), "[%g,%g,%g],",
                 player.x + (target.x - player.x) * t,
                 player.y + (target.y - player.y) * t,
                 player.z + (target.z - player.z) * t);
        path_json += buf;
    }
    if (!path_json.empty()) path_json.pop_back();

    std::string meta = "\"paths\":[[" + path_json + "]],\"path_count\":1";
    return response_json(true, "path_find", 1, 0, "path found", meta);
}

} // anonymous namespace
