#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <shared_mutex>
#include <atomic>
#include <random>
#include <cmath>

struct LSHEntry {
    char id[37];
    float embedding[128];
};

class LSHIndex {
public:
    static constexpr int DIMENSIONS = 128;
    static constexpr int NUM_HYPERPLANES = 6;
    static constexpr int NUM_BANDS = 4;
    static constexpr int PLANES_PER_BAND = NUM_HYPERPLANES / NUM_BANDS;
    static constexpr int BUCKET_OVERFLOW_CAP = 30;
    static constexpr uint32_t RNG_SEED = 0x0D215481;

    LSHIndex();

    void insert(const char* id, const float* embedding);
    void remove(const char* id);
    std::vector<LSHEntry> query(const float* embedding, int maxCandidates) const;
    void build();
    void clear();

    bool ready() const;
    int size() const;

private:
    struct BandTable {
        std::unordered_map<uint32_t, std::vector<LSHEntry>> buckets;
    };

    float hyperplanes_[NUM_HYPERPLANES][DIMENSIONS];
    std::array<BandTable, NUM_BANDS> bandTables_;
    std::vector<LSHEntry> allEntries_;

    mutable std::shared_mutex mutex_;
    std::atomic<bool> isReady_;
    std::atomic<int> entryCount_;

    void generateHyperplanes();
    uint32_t computeBandHash(const float* embedding, int bandIndex) const;
    uint32_t computeFullHash(const float* embedding) const;
    void insertIntoBands(const LSHEntry& entry);
    void removeFromBands(const char* id);
    void rebuildBands();
};
