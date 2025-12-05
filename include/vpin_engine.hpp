#ifndef VPIN_ENGINE_HPP
#define VPIN_ENGINE_HPP

#include <vector>
#include <deque>
#include <unordered_map>
#include <string>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

namespace vpin {

// Trade structure matching Polygon.io crypto trades
struct CryptoTrade {
    std::string pair;           // e.g., "BTC-USD"
    uint64_t timestamp;         // Unix timestamp in milliseconds
    double price;               // Trade price
    double size;                // Trade size (in base currency)
    double notional_value;      // price * size
    int8_t trade_direction;     // 1=buy, -1=sell, 0=unknown
    uint32_t exchange_id;       // Exchange identifier
    
    CryptoTrade() = default;
    CryptoTrade(const std::string& p, uint64_t ts, double pr, double sz, 
                double notional, int8_t dir, uint32_t exch)
        : pair(p), timestamp(ts), price(pr), size(sz), 
          notional_value(notional), trade_direction(dir), exchange_id(exch) {}
};

// Quote structure matching Polygon.io crypto quotes
struct CryptoQuote {
    std::string pair;
    uint64_t timestamp;
    double bid_price;
    double ask_price;
    double bid_size;
    double ask_size;
    double mid_price;
    double spread;
    double spread_bps;
    
    CryptoQuote() = default;
    CryptoQuote(const std::string& p, uint64_t ts, double bid_p, double ask_p,
                double bid_s, double ask_s, double mid, double spr, double spr_bps)
        : pair(p), timestamp(ts), bid_price(bid_p), ask_price(ask_p),
          bid_size(bid_s), ask_size(ask_s), mid_price(mid), 
          spread(spr), spread_bps(spr_bps) {}
};

// Volume bucket for VPIN calculation
struct VolumeBucket {
    double buy_volume;
    double sell_volume;
    double total_volume;
    uint64_t start_time;
    uint64_t end_time;
    uint32_t trade_count;
    
    VolumeBucket() 
        : buy_volume(0.0), sell_volume(0.0), total_volume(0.0),
          start_time(0), end_time(0), trade_count(0) {}
    
    double order_imbalance() const {
        return std::abs(buy_volume - sell_volume);
    }
    
    bool is_complete(double target_volume) const {
        return total_volume >= target_volume;
    }
    
    void add_trade(double volume, int8_t direction, uint64_t timestamp) {
        if (trade_count == 0) {
            start_time = timestamp;
        }
        end_time = timestamp;
        total_volume += volume;
        trade_count++;
        
        if (direction > 0) {
            buy_volume += volume;
        } else if (direction < 0) {
            sell_volume += volume;
        } else {
            // Unknown direction: split 50/50
            buy_volume += volume * 0.5;
            sell_volume += volume * 0.5;
        }
    }
};

// VPIN metric output
struct VPINMetric {
    std::string pair;
    uint64_t timestamp;
    double vpin;                // Current VPIN value [0, 1]
    double mean_vpin;           // Rolling mean
    double std_vpin;            // Rolling standard deviation
    double max_vpin;            // Max in window
    double min_vpin;            // Min in window
    uint32_t bucket_count;      // Number of buckets used
    double total_volume;        // Total volume in window
    
    VPINMetric() = default;
    VPINMetric(const std::string& p, uint64_t ts, double v)
        : pair(p), timestamp(ts), vpin(v), mean_vpin(v), std_vpin(0.0),
          max_vpin(v), min_vpin(v), bucket_count(0), total_volume(0.0) {}
};

// Per-symbol VPIN state
class VPINState {
public:
    VPINState(double bucket_sz, uint32_t num_buckets)
        : bucket_size(bucket_sz), max_buckets(num_buckets),
          current_bucket(), trade_count(0) {
        //completed_buckets.reserve(num_buckets); //// Note: deque doesn't have reserve(), but pre-allocates efficiently
    }
    
    void add_trade(const CryptoTrade& trade);
    std::optional<VPINMetric> calculate_vpin(const std::string& pair, uint64_t timestamp);
    void reset();
    
    double get_bucket_size() const { return bucket_size; }
    uint32_t get_bucket_count() const { return completed_buckets.size(); }
    
private:
    double bucket_size;
    uint32_t max_buckets;
    VolumeBucket current_bucket;
    std::deque<VolumeBucket> completed_buckets;
    uint64_t trade_count;
    
    double calculate_vpin_value() const;
    void complete_current_bucket();
};

// Main VPIN Engine with SIMD optimizations
class VPINEngine {
public:
    VPINEngine(double bucket_size = 10000.0,  // $10K per bucket
               uint32_t num_buckets = 50,      // Rolling window
               uint32_t emit_interval = 100);  // Emit every N trades
    
    ~VPINEngine() = default;
    
    // Process incoming trade
    std::optional<VPINMetric> process_trade(const CryptoTrade& trade);
    
    // Process incoming quote (for future enhancements)
    void process_quote(const CryptoQuote& quote);
    
    // Get current VPIN for a symbol
    std::optional<VPINMetric> get_current_vpin(const std::string& pair) const;
    
    // Batch processing with SIMD optimization
    std::vector<VPINMetric> process_trade_batch(const std::vector<CryptoTrade>& trades);
    
    // Statistics
    struct Stats {
        uint64_t trades_processed;
        uint64_t metrics_emitted;
        uint64_t active_symbols;
        double avg_processing_time_us;
    };
    
    Stats get_stats() const;
    void reset_stats();
    
    // Configuration
    void set_bucket_size(double size);
    void set_num_buckets(uint32_t count);
    void set_emit_interval(uint32_t interval);
    
private:
    double bucket_size_;
    uint32_t num_buckets_;
    uint32_t emit_interval_;
    
    // Per-symbol state (thread-safe)
    mutable std::mutex state_mutex_;
    std::unordered_map<std::string, std::unique_ptr<VPINState>> symbol_states_;
    
    // Statistics
    mutable std::mutex stats_mutex_;
    Stats stats_;
    
    VPINState* get_or_create_state(const std::string& pair);
    
    // SIMD-optimized batch processing
    void classify_trades_simd(const std::vector<CryptoTrade>& trades,
                             std::vector<int8_t>& classifications);
    
    void compute_buckets_simd(const std::vector<CryptoTrade>& trades,
                             const std::vector<int8_t>& classifications,
                             std::vector<VolumeBucket>& buckets);
    
    std::vector<double> compute_vpin_values_simd(const std::vector<VolumeBucket>& buckets);
};

// SIMD-optimized helper functions
namespace simd {
    // Check CPU capabilities
    bool has_avx2();
    bool has_avx512();
    
    // SIMD-optimized operations
    void sum_volumes_avx2(const double* volumes, size_t count, double& result);
    void compute_imbalances_avx2(const double* buy_vols, const double* sell_vols,
                                 size_t count, double* imbalances);
    void rolling_mean_avx2(const double* values, size_t count, size_t window,
                          double* means);
}

} // namespace vpin

#endif // VPIN_ENGINE_HPP
