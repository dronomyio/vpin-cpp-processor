#include "kafka_consumer.hpp"
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <fstream>
#include <chrono>

namespace vpin {

// ============================================================================
// KafkaConfig Implementation
// ============================================================================

KafkaConfig KafkaConfig::from_env() {
    KafkaConfig config;
    
    const char* bootstrap = std::getenv("KAFKA_BOOTSTRAP_SERVERS");
    if (bootstrap) {
        config.bootstrap_servers = bootstrap;
    }
    
    const char* group = std::getenv("KAFKA_GROUP_ID");
    if (group) {
        config.group_id = group;
    }
    
    const char* input_topic = std::getenv("KAFKA_INPUT_TOPIC");
    if (input_topic) {
        config.topics = {input_topic};
    }
    
    const char* output_topic = std::getenv("KAFKA_OUTPUT_TOPIC");
    if (output_topic) {
        config.output_topic = output_topic;
    }
    
    return config;
}

KafkaConfig KafkaConfig::from_file(const std::string& config_path) {
    KafkaConfig config;
    
    std::ifstream file(config_path);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file: " << config_path << std::endl;
        return config;
    }
    
    json j;
    file >> j;
    
    if (j.contains("bootstrap_servers")) {
        config.bootstrap_servers = j["bootstrap_servers"];
    }
    if (j.contains("group_id")) {
        config.group_id = j["group_id"];
    }
    if (j.contains("topics")) {
        config.topics = j["topics"].get<std::vector<std::string>>();
    }
    if (j.contains("output_topic")) {
        config.output_topic = j["output_topic"];
    }
    
    return config;
}

// ============================================================================
// MessageParser Implementation
// ============================================================================

CryptoTrade MessageParser::parse_trade(const std::string& json_str) {
    json j = json::parse(json_str);
    
    CryptoTrade trade;
    trade.pair = j.value("pair", "");
    trade.timestamp = j.value("timestamp", 0ULL);
    trade.price = j.value("price", 0.0);
    trade.size = j.value("size", 0.0);
    trade.notional_value = j.value("notional_value", trade.price * trade.size);
    trade.trade_direction = parse_trade_direction(j);
    trade.exchange_id = j.value("exchange_id", 0U);
    
    return trade;
}

CryptoQuote MessageParser::parse_quote(const std::string& json_str) {
    json j = json::parse(json_str);
    
    CryptoQuote quote;
    quote.pair = j.value("pair", "");
    quote.timestamp = j.value("timestamp", 0ULL);
    quote.bid_price = j.value("bid_price", 0.0);
    quote.ask_price = j.value("ask_price", 0.0);
    quote.bid_size = j.value("bid_size", 0.0);
    quote.ask_size = j.value("ask_size", 0.0);
    quote.mid_price = j.value("mid_price", (quote.bid_price + quote.ask_price) / 2.0);
    quote.spread = j.value("spread", quote.ask_price - quote.bid_price);
    quote.spread_bps = j.value("spread_bps", 0.0);
    
    return quote;
}

std::string MessageParser::serialize_vpin_metric(const VPINMetric& metric) {
    json j;
    j["pair"] = metric.pair;
    j["timestamp"] = metric.timestamp;
    j["vpin"] = metric.vpin;
    j["mean_vpin"] = metric.mean_vpin;
    j["std_vpin"] = metric.std_vpin;
    j["max_vpin"] = metric.max_vpin;
    j["min_vpin"] = metric.min_vpin;
    j["bucket_count"] = metric.bucket_count;
    j["total_volume"] = metric.total_volume;
    j["processor"] = "vpin-cpp";
    j["_topic"] = "crypto.microstructure.vpin";
    
    return j.dump();
}

int8_t MessageParser::parse_trade_direction(const json& j) {
    if (j.contains("trade_direction")) {
        return j["trade_direction"].get<int8_t>();
    }
    
    // Fallback: try to infer from other fields
    if (j.contains("conditions")) {
        // Polygon.io trade conditions
        // This is simplified - real implementation would parse conditions
        return 0;
    }
    
    return 0; // Unknown
}

// ============================================================================
// VPINKafkaConsumer Implementation
// ============================================================================

VPINKafkaConsumer::VPINKafkaConsumer(const KafkaConfig& config, VPINEngine& engine)
    : config_(config),
      engine_(engine),
      running_(false),
      stats_{0, 0, 0, 0, 0.0} {
}

VPINKafkaConsumer::~VPINKafkaConsumer() {
    stop();
}

void VPINKafkaConsumer::start() {
    if (running_.load()) {
        std::cerr << "Consumer already running" << std::endl;
        return;
    }
    
    if (!init_consumer()) {
        std::cerr << "Failed to initialize consumer" << std::endl;
        return;
    }
    
    if (!init_producer()) {
        std::cerr << "Failed to initialize producer" << std::endl;
        return;
    }
    
    running_.store(true);
    consumer_thread_ = std::thread(&VPINKafkaConsumer::consume_loop, this);
    
    std::cout << "VPIN Kafka Consumer started" << std::endl;
    std::cout << "  Bootstrap servers: " << config_.bootstrap_servers << std::endl;
    std::cout << "  Group ID: " << config_.group_id << std::endl;
    std::cout << "  Input topics: ";
    for (const auto& topic : config_.topics) {
        std::cout << topic << " ";
    }
    std::cout << std::endl;
    std::cout << "  Output topic: " << config_.output_topic << std::endl;
}

void VPINKafkaConsumer::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    if (consumer_thread_.joinable()) {
        consumer_thread_.join();
    }
    
    if (consumer_) {
        consumer_->close();
    }
    
    std::cout << "VPIN Kafka Consumer stopped" << std::endl;
}

VPINKafkaConsumer::ConsumerStats VPINKafkaConsumer::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

bool VPINKafkaConsumer::init_consumer() {
    std::string errstr;
    RdKafka::Conf* conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
    
    conf->set("bootstrap.servers", config_.bootstrap_servers, errstr);
    conf->set("group.id", config_.group_id, errstr);
    conf->set("session.timeout.ms", std::to_string(config_.session_timeout_ms), errstr);
    conf->set("max.poll.interval.ms", std::to_string(config_.max_poll_interval_ms), errstr);
    conf->set("auto.offset.reset", config_.auto_offset_reset, errstr);
    conf->set("enable.auto.commit", config_.enable_auto_commit ? "true" : "false", errstr);
    
    // Set callbacks
    EventCallback* event_cb = new EventCallback();
    conf->set("event_cb", event_cb, errstr);
    
    RebalanceCallback* rebalance_cb = new RebalanceCallback();
    conf->set("rebalance_cb", rebalance_cb, errstr);
    
    consumer_.reset(RdKafka::KafkaConsumer::create(conf, errstr));
    delete conf;
    
    if (!consumer_) {
        std::cerr << "Failed to create consumer: " << errstr << std::endl;
        return false;
    }
    
    // Subscribe to topics
    RdKafka::ErrorCode err = consumer_->subscribe(config_.topics);
    if (err != RdKafka::ERR_NO_ERROR) {
        std::cerr << "Failed to subscribe: " << RdKafka::err2str(err) << std::endl;
        return false;
    }
    
    return true;
}

bool VPINKafkaConsumer::init_producer() {
    std::string errstr;
    RdKafka::Conf* conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
    
    conf->set("bootstrap.servers", config_.bootstrap_servers, errstr);
    conf->set("compression.codec", "lz4", errstr);
    conf->set("linger.ms", "10", errstr);
    conf->set("batch.size", "16384", errstr);
    
    producer_.reset(RdKafka::Producer::create(conf, errstr));
    delete conf;
    
    if (!producer_) {
        std::cerr << "Failed to create producer: " << errstr << std::endl;
        return false;
    }
    
    return true;
}

void VPINKafkaConsumer::consume_loop() {
    while (running_.load()) {
        RdKafka::Message* msg = consumer_->consume(1000); // 1 second timeout
        
        if (msg->err() == RdKafka::ERR_NO_ERROR) {
            process_message(msg);
        } else if (msg->err() == RdKafka::ERR__TIMED_OUT) {
            // Timeout, continue
        } else if (msg->err() == RdKafka::ERR__PARTITION_EOF) {
            // End of partition
        } else {
            std::cerr << "Consumer error: " << msg->errstr() << std::endl;
        }
        
        delete msg;
        
        // Poll producer for delivery reports
        if (producer_) {
            producer_->poll(0);
        }
    }
}

void VPINKafkaConsumer::process_message(RdKafka::Message* message) {
    auto start = std::chrono::high_resolution_clock::now();
    
    try {
        std::string payload(static_cast<const char*>(message->payload()), 
                          message->len());
        
        // Parse trade
        CryptoTrade trade = MessageParser::parse_trade(payload);
        
        // Process through VPIN engine
        auto metric = engine_.process_trade(trade);
        
        // Produce metric if available
        if (metric.has_value()) {
            produce_metric(metric.value());
        }
        
        // Update stats
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.messages_consumed++;
            
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            double latency_ms = duration.count() / 1000.0;
            
            stats_.avg_latency_ms = 
                (stats_.avg_latency_ms * (stats_.messages_consumed - 1) + latency_ms) 
                / stats_.messages_consumed;
        }
        
    } catch (const json::exception& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.parse_errors++;
    } catch (const std::exception& e) {
        std::cerr << "Processing error: " << e.what() << std::endl;
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.processing_errors++;
    }
}

void VPINKafkaConsumer::produce_metric(const VPINMetric& metric) {
    std::string payload = MessageParser::serialize_vpin_metric(metric);
    
    RdKafka::ErrorCode err = producer_->produce(
        config_.output_topic,
        RdKafka::Topic::PARTITION_UA,
        RdKafka::Producer::RK_MSG_COPY,
        const_cast<char*>(payload.c_str()),
        payload.size(),
        nullptr,
        0,
        0,
        nullptr
    );
    
    if (err != RdKafka::ERR_NO_ERROR) {
        std::cerr << "Failed to produce message: " << RdKafka::err2str(err) << std::endl;
    } else {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.messages_produced++;
    }
}

// ============================================================================
// Callbacks Implementation
// ============================================================================

void EventCallback::event_cb(RdKafka::Event& event) {
    switch (event.type()) {
        case RdKafka::Event::EVENT_ERROR:
            std::cerr << "Kafka error: " << RdKafka::err2str(event.err()) 
                     << " - " << event.str() << std::endl;
            break;
        case RdKafka::Event::EVENT_STATS:
            // std::cout << "Kafka stats: " << event.str() << std::endl;
            break;
        case RdKafka::Event::EVENT_LOG:
            std::cout << "Kafka log: " << event.str() << std::endl;
            break;
        default:
            break;
    }
}

void RebalanceCallback::rebalance_cb(RdKafka::KafkaConsumer* consumer,
                                    RdKafka::ErrorCode err,
                                    std::vector<RdKafka::TopicPartition*>& partitions) {
    if (err == RdKafka::ERR__ASSIGN_PARTITIONS) {
        std::cout << "Rebalance: Assigning " << partitions.size() << " partitions" << std::endl;
        consumer->assign(partitions);
    } else if (err == RdKafka::ERR__REVOKE_PARTITIONS) {
        std::cout << "Rebalance: Revoking " << partitions.size() << " partitions" << std::endl;
        consumer->unassign();
    } else {
        std::cerr << "Rebalance error: " << RdKafka::err2str(err) << std::endl;
    }
}

} // namespace vpin
