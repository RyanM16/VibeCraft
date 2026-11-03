#include "core/tasks.hpp"
#include "core/telemetry.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <numeric>
#include <string>
using namespace vc;
using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}
static double percentile(std::vector<double> v, double p) {
    std::sort(v.begin(), v.end());
    return v[size_t((v.size() - 1) * p)];
}
int main(int argc, char **argv) {
    try {
        int frames = 600, side = 16,
            workers = std::min(4u, std::max(1u, std::thread::hardware_concurrency()));
        size_t budget = 16 * 1024 * 1024;
        std::string scenario = "flight", output = "artifacts/bench";
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (i + 1 >= argc)
                throw std::runtime_error("missing argument");
            std::string v = argv[++i];
            if (a == "--frames")
                frames = std::stoi(v);
            else if (a == "--side")
                side = std::stoi(v);
            else if (a == "--workers")
                workers = std::stoi(v);
            else if (a == "--budget-mib")
                budget = std::stoull(v) * 1024 * 1024;
            else if (a == "--scenario")
                scenario = v;
            else if (a == "--output")
                output = v;
            else
                throw std::runtime_error("unknown option " + a);
        }
        if (frames < 1 || frames > 100000 || workers < 1 || workers > 64 || (side != 16 && side != 32) ||
            (scenario != "flight" && scenario != "teleport" && scenario != "edits" &&
             scenario != "stationary"))
            throw std::runtime_error("invalid benchmark parameters");
        World world;
        Streamer streamer(world, size_t(workers), budget, 32);
        auto path = std::filesystem::path(output);
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path());
        std::ofstream csv(output + ".csv");
        if (!csv)
            throw std::runtime_error("cannot write CSV");
        csv << "frame,main_thread_ms,resident_bytes,pending,completed,cancelled,evicted,rejected\n";
        std::vector<double> times, latencies;
        size_t quads = 0;
        int64_t camera = 0;
        auto start = Clock::now();
        for (int frame = 0; frame < frames; ++frame) {
            auto t = Clock::now();
            if (scenario == "teleport" && frame % 30 == 0) {
                camera += 100000;
                streamer.teleport();
            } else if (scenario != "stationary")
                camera += 2;
            if (scenario == "edits") {
                world.edit({camera, world.height(camera, 0), 0}, 0);
                if (frame % 10 == 0)
                    streamer.teleport();
            }
            for (int z = -1; z <= 1; ++z)
                for (int x = -1; x <= 1; ++x) {
                    int64_t wx = (floor_div(camera, side) + x) * side, wz = z * side;
                    streamer.request({wx, floor_div(world.height(wx, wz), side) * side, wz}, side,
                                     std::abs(x) + std::abs(z));
                }
            streamer.poll(2 * 1024 * 1024);
            auto elapsed = ms(t);
            times.push_back(elapsed);
            auto s = streamer.stats();
            csv << frame << ',' << elapsed << ',' << s.resident_bytes << ',' << s.pending << ','
                << s.completed << ',' << s.cancelled << ',' << s.evicted << ',' << s.rejected << '\n';
            if (s.resident_bytes > budget || s.pending > 32)
                throw std::runtime_error("streaming cap violated");
            // Yield a simulated frame interval; excluded from main-thread timings. This is not rendered FPS.
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        auto workload_peak = streamer.stats().peak_bytes;
        auto settle_start = Clock::now();
        auto deadline = Clock::now() + std::chrono::seconds(30);
        while (streamer.stats().pending) {
            streamer.poll(2 * 1024 * 1024);
            if (Clock::now() > deadline)
                throw std::runtime_error("streaming drain timeout");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        // After destructive motion/edits stop, require actual current geometry to become resident.
        // Cancelled futures alone are not proof that the streamer can recover.
        for (int z = -1; z <= 1; ++z)
            for (int x = -1; x <= 1; ++x) {
                int64_t wx = (floor_div(camera, side) + x) * side, wz = z * side;
                if (!streamer.request({wx, floor_div(world.height(wx, wz), side) * side, wz}, side))
                    throw std::runtime_error("settle request rejected");
            }
        while (streamer.stats().pending) {
            streamer.poll(2 * 1024 * 1024);
            if (Clock::now() > deadline)
                throw std::runtime_error("settle timeout");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        double settle_ms = ms(settle_start);
        if (streamer.stats().resident_bytes == 0)
            throw std::runtime_error("no current terrain resident after settling");
        for (int i = 0; i < 32; ++i) {
            auto t = Clock::now();
            auto c = generate(world, {i * side, floor_div(world.height(i * side, 0), side) * side, 0}, side);
            auto mesh = greedy_mesh(c, [&](Pos p) { return world.block(p); }, c.origin);
            latencies.push_back(ms(t));
            quads += mesh.indices.size() / 6;
        }
        auto s = streamer.stats();
        if (s.completed == 0 || s.completed != s.submitted)
            throw std::runtime_error("no completed work");
        std::ofstream json(output + ".json");
        json << "{\n  \"kind\": \"CPU-only streaming and meshing; not rendered FPS\",\n  \"scenario\": \""
             << scenario << "\",\n  \"seed\": 42, \"generation_version\": 1,\n  \"frames\": " << frames
             << ", \"chunk_side\": " << side << ", \"workers\": " << workers
             << ",\n  \"main_thread_ms\": {\"median\": " << percentile(times, .5)
             << ", \"p95\": " << percentile(times, .95) << ", \"p99\": " << percentile(times, .99)
             << "},\n  \"generation_mesh_ms\": {\"median\": " << percentile(latencies, .5)
             << ", \"p99\": " << percentile(latencies, .99) << "},\n  \"mesh_quads_32_samples\": " << quads
             << ", \"cache_budget_bytes\": " << budget << ", \"peak_cache_bytes\": " << s.peak_bytes
             << ",\n  \"workload_peak_cache_bytes\": " << workload_peak
             << ", \"settled_resident_bytes\": " << s.resident_bytes
             << ", \"post_workload_settle_ms\": " << settle_ms << ",\n  \"submitted\": " << s.submitted
             << ", \"completed\": " << s.completed << ", \"cancelled\": " << s.cancelled
             << ", \"evicted\": " << s.evicted << ", \"rejected\": " << s.rejected
             << ",\n  \"wall_ms\": " << ms(start) << ",\n  \"peak_rss_bytes\": " << peak_rss_bytes()
             << "\n}\n";
        json.flush();
        csv.flush();
        if (!json || !csv)
            throw std::runtime_error("benchmark output write failed");
        std::cout << "CPU benchmark " << scenario << ": " << frames << " frames; main p99 "
                  << percentile(times, .99) << " ms; gen+mesh p99 " << percentile(latencies, .99)
                  << " ms; completed " << s.completed << "; peak cache " << s.peak_bytes << " bytes\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
