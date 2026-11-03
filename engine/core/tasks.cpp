#include "core/tasks.hpp"
#include <algorithm>
#include <chrono>
#include <stdexcept>
namespace vc {
Scheduler::Scheduler(size_t count, size_t limit) : limit_(limit) {
    if (!count || !limit)
        throw std::invalid_argument("worker/queue limits must be positive");
    for (size_t i = 0; i < count; ++i)
        workers_.emplace_back([this] {
            for (;;) {
                Task task;
                {
                    std::unique_lock lock(mutex_);
                    ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                    if (stopping_ && queue_.empty())
                        return;
                    task = queue_.top();
                    queue_.pop();
                }
                task.run();
            }
        });
}
Scheduler::~Scheduler() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    ready_.notify_all();
    for (auto &t : workers_)
        t.join();
}
bool Scheduler::submit(int p, std::function<void()> fn) {
    {
        std::lock_guard lock(mutex_);
        if (stopping_ || queue_.size() >= limit_)
            return false;
        queue_.push({p, sequence_++, std::move(fn)});
    }
    ready_.notify_one();
    return true;
}
size_t Scheduler::queued() {
    std::lock_guard lock(mutex_);
    return queue_.size();
}
Streamer::Streamer(const World &w, size_t workers, size_t budget, size_t max_pending)
    : world_(w), budget_(budget), max_pending_(max_pending),
      max_resident_entries_(std::clamp<size_t>(budget / 1024, 1, 4096)), world_revision_(w.revision()),
      scheduler_(workers, max_pending) {
    if (!budget)
        throw std::invalid_argument("budget must be positive");
}
void Streamer::teleport() {
    ++epoch_;
    in_flight_.clear();
    cache_.clear();
    stats_.resident_bytes = 0;
}
void Streamer::synchronize_edits() {
    if (world_revision_ != world_.revision()) {
        teleport();
        world_revision_ = world_.revision();
    }
}
bool Streamer::request(Pos key, int side, int priority) {
    synchronize_edits();
    if (side != 16 && side != 32)
        throw std::invalid_argument("invalid chunk side");
    if (chunk_side_ && chunk_side_ != side)
        teleport();
    chunk_side_ = side;
    if (auto it = cache_.find(key); it != cache_.end()) {
        it->second.used = ++clock_;
        return true;
    }
    if (in_flight_.contains(key))
        return true;
    if (pending_.size() >= max_pending_) {
        ++stats_.rejected;
        return false;
    }
    auto task = std::make_shared<std::packaged_task<Result()>>([this, key, side, e = epoch_.load()] {
        Result result{key, e, {}};
        if (e != epoch_) {
            result.cancelled = true;
            return result;
        }
        auto chunk = generate(world_, key, side);
        if (e != epoch_) {
            result.cancelled = true;
            return result;
        }
        result.mesh = greedy_mesh(chunk, [this](Pos p) { return world_.block(p); }, key);
        result.cancelled = e != epoch_;
        return result;
    });
    auto future = task->get_future();
    if (!scheduler_.submit(priority, [task] { (*task)(); })) {
        ++stats_.rejected;
        return false;
    }
    in_flight_.insert(key);
    pending_.push_back(std::move(future));
    ++stats_.submitted;
    return true;
}
size_t Streamer::poll(size_t upload) {
    synchronize_edits();
    size_t consumed = 0;
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }
        // The result queue is bounded by max_pending. Reject over-budget uploads; callers can retry.
        auto result = it->get();
        it = pending_.erase(it);
        ++stats_.completed;
        if (result.epoch == epoch_)
            in_flight_.erase(result.key);
        if (result.cancelled || result.epoch != epoch_) {
            ++stats_.cancelled;
            continue;
        }
        auto bytes = result.mesh.bytes();
        if (bytes > budget_ || bytes > upload - consumed) {
            ++stats_.rejected;
            continue;
        }
        if (auto existing = cache_.find(result.key); existing != cache_.end()) {
            stats_.resident_bytes -= existing->second.mesh.bytes();
            cache_.erase(existing);
        }
        while (!cache_.empty() &&
               (stats_.resident_bytes + bytes > budget_ || cache_.size() >= max_resident_entries_)) {
            auto oldest = std::min_element(cache_.begin(), cache_.end(),
                                           [](auto &a, auto &b) { return a.second.used < b.second.used; });
            stats_.resident_bytes -= oldest->second.mesh.bytes();
            cache_.erase(oldest);
            ++stats_.evicted;
        }
        stats_.resident_bytes += bytes;
        stats_.peak_bytes = std::max(stats_.peak_bytes, stats_.resident_bytes);
        cache_.emplace(result.key, Resident{std::move(result.mesh), ++clock_});
        consumed += bytes;
    }
    return consumed;
}
StreamStats Streamer::stats() const {
    auto s = stats_;
    s.pending = pending_.size();
    s.resident = cache_.size();
    return s;
}
} // namespace vc
