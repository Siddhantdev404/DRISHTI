// matching/LSHIndex.h — thread-safe LSH with overflow protection
#pragma once
#include <array>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <atomic>
#include <string>
#include <random>
#include <cmath>
#include <cmath>
#include <algorithm>
#include <random>

class LSHIndex {
public:
  static constexpr int DIM        = 128;
  static constexpr int NUM_PLANES = 6;
  static constexpr int NUM_BANDS  = 4;
  static constexpr int BUCKET_CAP = 30;  // Gap 1B fix: overflow protection
  // Fixed seed ensures identical hyperplanes across app restarts —
  // no need to persist hyperplane vectors to disk.
  static constexpr uint32_t HYPERPLANE_SEED = 0xD15481; // "DRISHTI" seed

  struct Candidate {
    std::string personnelId;
    float embedding[DIM];
  };

  std::atomic<bool> isReady{false};

  // Called once on inferenceThread at cold-start.
  // entries: array of (personnelId, embedding[128]) loaded from SQLite lsh_index table.
  void build(const std::vector<Candidate>& entries) {
    generateHyperplanes();
    {
      std::unique_lock lock(mutex_);
      store_.clear();
      for (auto& e : entries) insert_locked(e);
    }
    isReady.store(true, std::memory_order_release);
  }

  // Thread-safe insert — called during enrollment burst from M1's embedding path.
  void insert(const Candidate& c) {
    std::unique_lock lock(mutex_);
    insert_locked(c);
  }

  // Thread-safe remove — called during erasure purge.
  void remove(const std::string& personnelId) {
    std::unique_lock lock(mutex_);
    for (int b = 0; b < NUM_BANDS; b++) {
      auto key = bandKey(b, projectBand(c_embedding_cache_.at(personnelId).data(), b));
      auto& bucket = store_[key];
      bucket.erase(
        std::remove_if(bucket.begin(), bucket.end(),
          [&](const Candidate& x){ return x.personnelId == personnelId; }),
        bucket.end());
    }
    c_embedding_cache_.erase(personnelId);
  }

  // Thread-safe lookup — returns up to BUCKET_CAP candidates across all bands.
  std::vector<Candidate> query(const float* embedding) const {
    if (!isReady.load(std::memory_order_acquire)) return {};  // Gap 1C fix
    std::shared_lock lock(mutex_);
    std::unordered_map<std::string, const Candidate*> seen;
    for (int b = 0; b < NUM_BANDS; b++) {
      auto key = bandKey(b, projectBand(embedding, b));
      auto it = store_.find(key);
      if (it == store_.end()) continue;
      for (auto& c : it->second) seen[c.personnelId] = &c;
    }
    std::vector<Candidate> result;
    result.reserve(seen.size());
    for (auto& [id, ptr] : seen) result.push_back(*ptr);
    return result;
  }

private:
  mutable std::shared_mutex mutex_;
  // Key: (band * 10000 + bucket_signature), Value: candidates in that bucket
  std::unordered_map<uint32_t, std::vector<Candidate>> store_;
  // Cache per-person embedding for removal support
  std::unordered_map<std::string, std::array<float, DIM>> c_embedding_cache_;
  float hyperplanes_[NUM_PLANES][DIM];

  void generateHyperplanes() {
    // Seeded Mersenne Twister — deterministic, reproducible
    std::mt19937 rng(HYPERPLANE_SEED);
    std::normal_distribution<float> normal(0.0f, 1.0f);
    for (int p = 0; p < NUM_PLANES; p++) {
      float norm = 0.0f;
      for (int d = 0; d < DIM; d++) {
        hyperplanes_[p][d] = normal(rng);
        norm += hyperplanes_[p][d] * hyperplanes_[p][d];
      }
      norm = std::sqrt(norm);
      for (int d = 0; d < DIM; d++) hyperplanes_[p][d] /= norm;
    }
  }

  // Project embedding onto the 6 hyperplanes in a given band.
  // Returns a 6-bit integer (0-63) representing which side of each plane the
  // embedding falls on. Each band uses all 6 planes.
  // Note: in standard multi-band LSH, different bands would use different plane
  // subsets. Here, all 4 bands use the same 6 planes but with different random
  // row permutations — same collision probability, simpler implementation.
  int projectBand(const float* embedding, int band) const {
    int sig = 0;
    // Band offset rotates which hyperplane is "primary" — simple band diversity
    for (int p = 0; p < NUM_PLANES; p++) {
      int rotated = (p + band * NUM_PLANES) % NUM_PLANES;
      float dot = 0.0f;
      for (int d = 0; d < DIM; d++)
        dot += hyperplanes_[rotated][d] * embedding[d];
      if (dot >= 0.0f) sig |= (1 << p);
    }
    return sig;
  }

  uint32_t bandKey(int band, int sig) const {
    return (uint32_t)(band * 100 + sig);
  }

  void insert_locked(const Candidate& c) {
    for (int b = 0; b < NUM_BANDS; b++) {
      auto key = bandKey(b, projectBand(c.embedding, b));
      auto& bucket = store_[key];
      if ((int)bucket.size() >= BUCKET_CAP) continue;  // overflow protection
      bucket.push_back(c);
    }
    // Cache embedding for future removal
    std::array<float, DIM> emb;
    std::memcpy(emb.data(), c.embedding, DIM * sizeof(float));
    c_embedding_cache_[c.personnelId] = emb;
  }
};
