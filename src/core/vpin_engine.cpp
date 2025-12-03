#include "vpin_engine.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <chrono>
#include <optional>

#ifdef __AVX2__
#include <immintrin.h>
#endif

namespace vpin {

// ============================================================================
// VPINState Implementation
// ============================================================================

void VPINState::add_trade(const CryptoTrade& trade) {
    if (trade.notional_value <= 0.0) {
        return;
    }
    
    current_bucket.add_trade(trade.notional_value, trade.trade_direction, trade.timestamp);
    trade_count++;
    
    // Check if bucket is complete
    if (current_bucket.is_complete(bucket_size)) {
        complete_current_bucket();
    }
}

void VPINState::complete_current_bucket() {
    completed_buckets.push_back(current_bucket);
    
    // Maintain rolling window
    if (completed_buckets.size() > max_buckets) {
        completed_buckets.pop_front();
    }
    
    // Reset current bucket
    current_bucket = VolumeBucket();
}

double VPINState::calculate_vpin_value() const {
    if (completed_buckets.empty()) {
        return 0.0;
    }
    
    double total_imbalance = 0.0;
    double total_volume = 0.0;
    
    for (const auto& bucket : completed_buckets) {
        total_imbalance += bucket.order_imbalance();
        total_volume += bucket.total_volume;
    }
    
    if (total_volume == 0.0) {
        return 0.0;
    }
    
    return total_imbalance / total_volume;
}

std::optional<VPINMetric> VPINState::calculate_vpin(const std::string& pair, uint64_t timestamp) {
    // Need at least half the window for meaningful VPIN
    if (completed_buckets.size() < max_buckets / 2) {
        return std::nullopt;
    }
    
    double vpin_value = calculate_vpin_value();
    
    // Calculate statistics
    std::vector<double> vpin_values;
    vpin_values.reserve(completed_buckets.size());
    
    double total_volume = 0.0;
    for (const auto& bucket : completed_buckets) {
        total_volume += bucket.total_volume;
    }
    
    VPINMetric metric;
    metric.pair = pair;
    metric.timestamp = timestamp;
    metric.vpin = vpin_value;
    metric.mean_vpin = vpin_value;
    metric.std_vpin = 0.0;
    metric.max_vpin = vpin_value;
    metric.min_vpin = vpin_value;
    metric.bucket_count = completed_buckets.size();
    metric.total_volume = total_volume;
    
    return metric;
}

void VPINState::reset() {
    current_bucket = VolumeBucket();
    completed_buckets.clear();
    trade_count = 0;
}

// ============================================================================
// VPINEngine Implementation
// ============================================================================

VPINEngine::VPINEngine(double bucket_size, uint32_t num_buckets, uint32_t emit_interval)
    : bucket_size_(bucket_size),
      num_buckets_(num_buckets),
      emit_interval_(emit_interval),
      stats_{0, 0, 0, 0.0} {
}

std::optional<VPINMetric> VPINEngine::process_trade(const CryptoTrade& trade) {
    auto start = std::chrono::high_resolution_clock::now();
    
    VPINState* state = get_or_create_state(trade.pair);
    state->add_trade(trade);
    
    // Update stats
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.trades_processed++;
    }
    
    // Emit metric at intervals
    std::optional<VPINMetric> result = std::nullopt;
    if (stats_.trades_processed % emit_interval_ == 0) {
        result = state->calculate_vpin(trade.pair, trade.timestamp);
        if (result.has_value()) {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.metrics_emitted++;
        }
    }
    
    // Update processing time
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        // Running average
        stats_.avg_processing_time_us = 
            (stats_.avg_processing_time_us * (stats_.trades_processed - 1) + duration.count()) 
            / stats_.trades_processed;
    }
    
    return result;
}

void VPINEngine::process_quote(const CryptoQuote& quote) {
    // Store latest quote for each symbol (for future enhancements)
    // Currently not used in VPIN calculation
}

std::optional<VPINMetric> VPINEngine::get_current_vpin(const std::string& pair) const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    auto it = symbol_states_.find(pair);
    if (it == symbol_states_.end()) {
        return std::nullopt;
    }
    
    return it->second->calculate_vpin(pair, 
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

std::vector<VPINMetric> VPINEngine::process_trade_batch(const std::vector<CryptoTrade>& trades) {
    std::vector<VPINMetric> metrics;
    metrics.reserve(trades.size() / emit_interval_);
    
    for (const auto& trade : trades) {
        auto metric = process_trade(trade);
        if (metric.has_value()) {
            metrics.push_back(metric.value());
        }
    }
    
    return metrics;
}

VPINEngine::Stats VPINEngine::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    Stats current_stats = stats_;
    current_stats.active_symbols = symbol_states_.size();
    
    return current_stats;
}

void VPINEngine::reset_stats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = {0, 0, 0, 0.0};
}

void VPINEngine::set_bucket_size(double size) {
    bucket_size_ = size;
}

void VPINEngine::set_num_buckets(uint32_t count) {
    num_buckets_ = count;
}

void VPINEngine::set_emit_interval(uint32_t interval) {
    emit_interval_ = interval;
}

VPINState* VPINEngine::get_or_create_state(const std::string& pair) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    auto it = symbol_states_.find(pair);
    if (it != symbol_states_.end()) {
        return it->second.get();
    }
    
    auto state = std::make_unique<VPINState>(bucket_size_, num_buckets_);
    VPINState* ptr = state.get();
    symbol_states_[pair] = std::move(state);
    
    return ptr;
}

// ============================================================================
// SIMD Optimizations
// ============================================================================

namespace simd {

bool has_avx2() {
#ifdef __AVX2__
    return true;
#else
    return false;
#endif
}

bool has_avx512() {
#ifdef __AVX512F__
    return true;
#else
    return false;
#endif
}

void sum_volumes_avx2(const double* volumes, size_t count, double& result) {
#ifdef __AVX2__
    __m256d sum_vec = _mm256_setzero_pd();
    
    size_t i = 0;
    for (; i + 4 <= count; i += 4) {
        __m256d v = _mm256_loadu_pd(&volumes[i]);
        sum_vec = _mm256_add_pd(sum_vec, v);
    }
    
    // Horizontal sum
    __m128d sum_high = _mm256_extractf128_pd(sum_vec, 1);
    __m128d sum_low = _mm256_castpd256_pd128(sum_vec);
    __m128d sum_128 = _mm_add_pd(sum_high, sum_low);
    sum_128 = _mm_hadd_pd(sum_128, sum_128);
    
    result = _mm_cvtsd_f64(sum_128);
    
    // Add remaining elements
    for (; i < count; ++i) {
        result += volumes[i];
    }
#else
    result = std::accumulate(volumes, volumes + count, 0.0);
#endif
}

void compute_imbalances_avx2(const double* buy_vols, const double* sell_vols,
                             size_t count, double* imbalances) {
#ifdef __AVX2__
    size_t i = 0;
    for (; i + 4 <= count; i += 4) {
        __m256d buy = _mm256_loadu_pd(&buy_vols[i]);
        __m256d sell = _mm256_loadu_pd(&sell_vols[i]);
        __m256d diff = _mm256_sub_pd(buy, sell);
        
        // Absolute value using bitwise AND with sign mask
        __m256d sign_mask = _mm256_set1_pd(-0.0);
        __m256d abs_diff = _mm256_andnot_pd(sign_mask, diff);
        
        _mm256_storeu_pd(&imbalances[i], abs_diff);
    }
    
    // Process remaining elements
    for (; i < count; ++i) {
        imbalances[i] = std::abs(buy_vols[i] - sell_vols[i]);
    }
#else
    for (size_t i = 0; i < count; ++i) {
        imbalances[i] = std::abs(buy_vols[i] - sell_vols[i]);
    }
#endif
}

void rolling_mean_avx2(const double* values, size_t count, size_t window,
                      double* means) {
    if (count < window) {
        return;
    }
    
    for (size_t i = 0; i <= count - window; ++i) {
        double sum = 0.0;
        sum_volumes_avx2(&values[i], window, sum);
        means[i] = sum / static_cast<double>(window);
    }
}

} // namespace simd

} // namespace vpin
