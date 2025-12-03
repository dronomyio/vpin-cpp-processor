#include "vpin_engine.hpp"
#include "kafka_consumer.hpp"
#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

std::atomic<bool> shutdown_requested(false);

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    shutdown_requested.store(true);
}

void print_stats(const vpin::VPINEngine& engine, 
                const vpin::VPINKafkaConsumer& consumer) {
    auto engine_stats = engine.get_stats();
    auto consumer_stats = consumer.get_stats();
    
    std::cout << "\n=== VPIN Processor Statistics ===" << std::endl;
    std::cout << "Engine:" << std::endl;
    std::cout << "  Trades processed: " << engine_stats.trades_processed << std::endl;
    std::cout << "  Metrics emitted: " << engine_stats.metrics_emitted << std::endl;
    std::cout << "  Active symbols: " << engine_stats.active_symbols << std::endl;
    std::cout << "  Avg processing time: " << engine_stats.avg_processing_time_us 
              << " μs" << std::endl;
    
    std::cout << "\nKafka Consumer:" << std::endl;
    std::cout << "  Messages consumed: " << consumer_stats.messages_consumed << std::endl;
    std::cout << "  Messages produced: " << consumer_stats.messages_produced << std::endl;
    std::cout << "  Parse errors: " << consumer_stats.parse_errors << std::endl;
    std::cout << "  Processing errors: " << consumer_stats.processing_errors << std::endl;
    std::cout << "  Avg latency: " << consumer_stats.avg_latency_ms << " ms" << std::endl;
    
    std::cout << "\nSIMD Support:" << std::endl;
    std::cout << "  AVX2: " << (vpin::simd::has_avx2() ? "Yes" : "No") << std::endl;
    std::cout << "  AVX512: " << (vpin::simd::has_avx512() ? "Yes" : "No") << std::endl;
    std::cout << "==================================\n" << std::endl;
}

int main(int argc, char** argv) {
    std::cout << "VPIN C++ Processor - High-Performance Microstructure Analytics" << std::endl;
    std::cout << "Version: 1.0.0" << std::endl;
    std::cout << "Architecture: CPU-optimized with SIMD (AVX2/AVX512)" << std::endl;
    std::cout << std::endl;
    
    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    // Load configuration
    vpin::KafkaConfig kafka_config = vpin::KafkaConfig::from_env();
    
    // Parse command line arguments
    double bucket_size = 10000.0;  // $10K
    uint32_t num_buckets = 50;
    uint32_t emit_interval = 100;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--bucket-size" && i + 1 < argc) {
            bucket_size = std::stod(argv[++i]);
        } else if (arg == "--num-buckets" && i + 1 < argc) {
            num_buckets = std::stoul(argv[++i]);
        } else if (arg == "--emit-interval" && i + 1 < argc) {
            emit_interval = std::stoul(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [OPTIONS]" << std::endl;
            std::cout << "\nOptions:" << std::endl;
            std::cout << "  --bucket-size SIZE      Volume bucket size (default: 10000)" << std::endl;
            std::cout << "  --num-buckets COUNT     Number of buckets in window (default: 50)" << std::endl;
            std::cout << "  --emit-interval N       Emit metric every N trades (default: 100)" << std::endl;
            std::cout << "  --help, -h              Show this help message" << std::endl;
            std::cout << "\nEnvironment Variables:" << std::endl;
            std::cout << "  KAFKA_BOOTSTRAP_SERVERS  Kafka broker addresses (default: localhost:9092)" << std::endl;
            std::cout << "  KAFKA_GROUP_ID           Consumer group ID (default: vpin-cpp-processor)" << std::endl;
            std::cout << "  KAFKA_INPUT_TOPIC        Input topic (default: crypto.processed.trades)" << std::endl;
            std::cout << "  KAFKA_OUTPUT_TOPIC       Output topic (default: crypto.microstructure.vpin)" << std::endl;
            return 0;
        }
    }
    
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Bucket size: $" << bucket_size << std::endl;
    std::cout << "  Number of buckets: " << num_buckets << std::endl;
    std::cout << "  Emit interval: " << emit_interval << " trades" << std::endl;
    std::cout << std::endl;
    
    // Initialize VPIN engine
    vpin::VPINEngine engine(bucket_size, num_buckets, emit_interval);
    
    // Initialize Kafka consumer
    vpin::VPINKafkaConsumer consumer(kafka_config, engine);
    
    // Start consuming
    consumer.start();
    
    // Statistics reporting thread
    std::thread stats_thread([&]() {
        while (!shutdown_requested.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            if (!shutdown_requested.load()) {
                print_stats(engine, consumer);
            }
        }
    });
    
    // Wait for shutdown signal
    while (!shutdown_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Cleanup
    std::cout << "Shutting down..." << std::endl;
    consumer.stop();
    
    if (stats_thread.joinable()) {
        stats_thread.join();
    }
    
    // Print final stats
    print_stats(engine, consumer);
    
    std::cout << "Shutdown complete" << std::endl;
    return 0;
}
