#include "LSHIndex.h"
#include <algorithm>

LSHIndex::LSHIndex() : isReady_(false), entryCount_(0) {
    std::memset(hyperplanes_, 0, sizeof(hyperplanes_));
    generateHyperplanes();
}

void LSHIndex::generateHyperplanes() {
    std::mt19937 rng(RNG_SEED);
    std::normal_distribution<float> dist(0.0f, 1.0f);

    for (int h = 0; h < NUM_HYPERPLANES; h++) {
        float magnitude = 0.0f;
        for (int d = 0; d < DIMENSIONS; d++) {
            hyperplanes_[h][d] = dist(rng);
            magnitude += hyperplanes_[h][d] * hyperplanes_[h][d];
        }

        magnitude = std::sqrt(magnitude);

        if (magnitude > 1e-8f) {
            for (int d = 0; d < DIMENSIONS; d++) {
                hyperplanes_[h][d] /= magnitude;
            }
        }
    }
}

uint32_t LSHIndex::computeBandHash(const float* embedding, int bandIndex) const {
    int startPlane = bandIndex * PLANES_PER_BAND;
    int endPlane = startPlane + PLANES_PER_BAND;

    if (endPlane > NUM_HYPERPLANES) {
        endPlane = NUM_HYPERPLANES;
    }

    uint32_t hash = 0;

    for (int p = startPlane; p < endPlane; p++) {
        float dot = 0.0f;
        for (int d = 0; d < DIMENSIONS; d++) {
            dot += embedding[d] * hyperplanes_[p][d];
        }

        if (dot >= 0.0f) {
            hash |= (1u << (p - startPlane));
        }
    }

    return hash;
}

uint32_t LSHIndex::computeFullHash(const float* embedding) const {
    uint32_t hash = 0;

    for (int p = 0; p < NUM_HYPERPLANES; p++) {
        float dot = 0.0f;
        for (int d = 0; d < DIMENSIONS; d++) {
            dot += embedding[d] * hyperplanes_[p][d];
        }

        if (dot >= 0.0f) {
            hash |= (1u << p);
        }
    }

    return hash;
}

void LSHIndex::insertIntoBands(const LSHEntry& entry) {
    for (int b = 0; b < NUM_BANDS; b++) {
        uint32_t bandHash = computeBandHash(entry.embedding, b);
        auto& bucket = bandTables_[b].buckets[bandHash];

        if (static_cast<int>(bucket.size()) >= BUCKET_OVERFLOW_CAP) {
            continue;
        }

        bucket.push_back(entry);
    }
}

void LSHIndex::removeFromBands(const char* id) {
    for (int b = 0; b < NUM_BANDS; b++) {
        for (auto& pair : bandTables_[b].buckets) {
            auto& bucket = pair.second;
            auto it = bucket.begin();
            while (it != bucket.end()) {
                if (std::strncmp(it->id, id, 36) == 0) {
                    it = bucket.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }
}

void LSHIndex::rebuildBands() {
    for (int b = 0; b < NUM_BANDS; b++) {
        bandTables_[b].buckets.clear();
    }

    for (const auto& entry : allEntries_) {
        insertIntoBands(entry);
    }
}

void LSHIndex::insert(const char* id, const float* embedding) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    for (const auto& existing : allEntries_) {
        if (std::strncmp(existing.id, id, 36) == 0) {
            return;
        }
    }

    LSHEntry entry;
    std::memset(entry.id, 0, sizeof(entry.id));
    std::strncpy(entry.id, id, 36);
    entry.id[36] = '\0';
    std::memcpy(entry.embedding, embedding, DIMENSIONS * sizeof(float));

    allEntries_.push_back(entry);
    insertIntoBands(entry);
    entryCount_.store(static_cast<int>(allEntries_.size()), std::memory_order_release);
}

void LSHIndex::remove(const char* id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    removeFromBands(id);

    auto it = allEntries_.begin();
    while (it != allEntries_.end()) {
        if (std::strncmp(it->id, id, 36) == 0) {
            it = allEntries_.erase(it);
        } else {
            ++it;
        }
    }

    entryCount_.store(static_cast<int>(allEntries_.size()), std::memory_order_release);
}

std::vector<LSHEntry> LSHIndex::query(const float* embedding, int maxCandidates) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    if (!isReady_.load(std::memory_order_acquire)) {
        return {};
    }

    std::vector<LSHEntry> candidates;
    candidates.reserve(maxCandidates * NUM_BANDS);

    for (int b = 0; b < NUM_BANDS; b++) {
        uint32_t bandHash = computeBandHash(embedding, b);
        auto it = bandTables_[b].buckets.find(bandHash);

        if (it == bandTables_[b].buckets.end()) {
            continue;
        }

        const auto& bucket = it->second;
        for (const auto& entry : bucket) {
            bool duplicate = false;
            for (const auto& existing : candidates) {
                if (std::strncmp(existing.id, entry.id, 36) == 0) {
                    duplicate = true;
                    break;
                }
            }

            if (!duplicate) {
                candidates.push_back(entry);

                if (static_cast<int>(candidates.size()) >= maxCandidates) {
                    return candidates;
                }
            }
        }
    }

    return candidates;
}

void LSHIndex::build() {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    isReady_.store(false, std::memory_order_release);

    rebuildBands();

    entryCount_.store(static_cast<int>(allEntries_.size()), std::memory_order_release);
    isReady_.store(true, std::memory_order_release);
}

void LSHIndex::clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    allEntries_.clear();

    for (int b = 0; b < NUM_BANDS; b++) {
        bandTables_[b].buckets.clear();
    }

    entryCount_.store(0, std::memory_order_release);
    isReady_.store(false, std::memory_order_release);
}

bool LSHIndex::ready() const {
    return isReady_.load(std::memory_order_acquire);
}

int LSHIndex::size() const {
    return entryCount_.load(std::memory_order_acquire);
}
