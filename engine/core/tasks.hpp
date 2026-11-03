#pragma once
#include "core/world.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <map>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_set>
namespace vc {
class Scheduler {
    struct Task {
        int priority;
        uint64_t sequence;
        std::function<void()> run;
    };
    struct Compare {
        bool operator()(const Task &a, const Task &b) const {
            return a.priority == b.priority ? a.sequence > b.sequence : a.priority > b.priority;
        }
    };
    std::priority_queue<Task, std::vector<Task>, Compare> queue_;
    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable ready_;
    bool stopping_ = false;
    size_t limit_;
    uint64_t sequence_ = 0;

  public:
    Scheduler(size_t workers, size_t limit);
    ~Scheduler();
    Scheduler(const Scheduler &) = delete;
    Scheduler &operator=(const Scheduler &) = delete;
    bool submit(int priority, std::function<void()> job);
    size_t queued();
};
struct StreamStats {
    size_t resident_bytes{}, peak_bytes{}, pending{}, resident{}, submitted{}, completed{}, cancelled{},
        evicted{}, rejected{};
};
class Streamer {
    struct Resident {
        Mesh mesh;
        uint64_t used;
    };
    struct Result {
        Pos key;
        uint64_t epoch;
        Mesh mesh;
        bool cancelled = false;
    };
    const World &world_;
    std::atomic<uint64_t> epoch_{0};
    size_t budget_, max_pending_, max_resident_entries_;
    uint64_t world_revision_;
    int chunk_side_ = 0;
    uint64_t clock_ = 0;
    std::unordered_map<Pos, Resident, Hash> cache_;
    std::vector<std::future<Result>> pending_;
    std::unordered_set<Pos, Hash> in_flight_;
    StreamStats stats_;
    // Declared last so workers join before result state and world references are destroyed.
    Scheduler scheduler_;

  public:
    Streamer(const World &w, size_t workers, size_t budget, size_t max_pending);
    void teleport();
    void synchronize_edits();
    bool request(Pos chunk, int side = 16, int priority = 0);
    size_t poll(size_t max_upload_bytes);
    bool contains(Pos key) const {
        return cache_.contains(key);
    }
    StreamStats stats() const;
};
} // namespace vc
