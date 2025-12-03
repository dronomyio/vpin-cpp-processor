#ifndef KAFKA_CONSUMER_HPP
#define KAFKA_CONSUMER_HPP

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <librdkafka/rdkafkacpp.h>
#include <nlohmann/json.hpp>
#include "vpin_engine.hpp"

namespace vpin {

using json = nlohmann::json;

// Kafka configuration
struct KafkaConfig {
    std::string bootstrap_servers;
    std::string group_id;
    std::vector<std::string> topics;
    std::string output_topic;
    int32_t session_timeout_ms;
    int32_t max_poll_interval_ms;
    std::string auto_offset_reset;
    bool enable_auto_commit;
    
    KafkaConfig()
        : bootstrap_servers("localhost:9092"),
          group_id("vpin-cpp-processor"),
          topics({"crypto.processed.trades"}),
          output_topic("crypto.microstructure.vpin"),
          session_timeout_ms(30000),
          max_poll_interval_ms(300000),
          auto_offset_reset("latest"),
          enable_auto_commit(true) {}
    
    static KafkaConfig from_env();
    static KafkaConfig from_file(const std::string& config_path);
};

// Message parser for Polygon.io format
class MessageParser {
public:
    static CryptoTrade parse_trade(const std::string& json_str);
    static CryptoQuote parse_quote(const std::string& json_str);
    static std::string serialize_vpin_metric(const VPINMetric& metric);
    
private:
    static int8_t parse_trade_direction(const json& j);
};

// Kafka consumer with VPIN processing
class VPINKafkaConsumer {
public:
    VPINKafkaConsumer(const KafkaConfig& config, VPINEngine& engine);
    ~VPINKafkaConsumer();
    
    // Start consuming messages
    void start();
    
    // Stop consuming
    void stop();
    
    // Check if running
    bool is_running() const { return running_.load(); }
    
    // Get statistics
    struct ConsumerStats {
        uint64_t messages_consumed;
        uint64_t messages_produced;
        uint64_t parse_errors;
        uint64_t processing_errors;
        double avg_latency_ms;
    };
    
    ConsumerStats get_stats() const;
    
private:
    KafkaConfig config_;
    VPINEngine& engine_;
    
    std::unique_ptr<RdKafka::KafkaConsumer> consumer_;
    std::unique_ptr<RdKafka::Producer> producer_;
    
    std::atomic<bool> running_;
    std::thread consumer_thread_;
    
    mutable std::mutex stats_mutex_;
    ConsumerStats stats_;
    
    void consume_loop();
    void process_message(RdKafka::Message* message);
    void produce_metric(const VPINMetric& metric);
    
    bool init_consumer();
    bool init_producer();
};

// Event callback for Kafka consumer
class EventCallback : public RdKafka::EventCb {
public:
    void event_cb(RdKafka::Event& event) override;
};

// Rebalance callback for Kafka consumer
class RebalanceCallback : public RdKafka::RebalanceCb {
public:
    void rebalance_cb(RdKafka::KafkaConsumer* consumer,
                     RdKafka::ErrorCode err,
                     std::vector<RdKafka::TopicPartition*>& partitions) override;
};

} // namespace vpin

#endif // KAFKA_CONSUMER_HPP
