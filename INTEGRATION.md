# Integration Guide: VPIN C++ Processor with crypto-ingestion-engine

This guide explains how to integrate the high-performance C++ VPIN processor with your existing crypto-ingestion-engine.

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                   Polygon.io WebSocket API                  │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│              crypto-ingestion-engine (Python)               │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  Ingestion → Processing → Kafka Producer             │   │
│  └──────────────────────────────────────────────────────┘   │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│                    Apache Kafka                             │
│  Topics:                                                    │
│    - crypto.processed.trades                                │
│    - crypto.processed.quotes                                │
│    - crypto.enriched.trades                                 │
└────────────┬────────────────────────────┬────────────────────┘
             │                            │
             ▼                            ▼
┌────────────────────────┐  ┌────────────────────────────────┐
│  VPIN C++ Processor    │  │  Other Microstructure          │
│  (External Plugin)     │  │  Processors (Python)           │
│  ┌──────────────────┐  │  │  - Spread Analytics            │
│  │ Kafka Consumer   │  │  │  - Order Flow                  │
│  │ SIMD VPIN Engine │  │  │  - etc.                        │
│  │ Kafka Producer   │  │  └────────────────────────────────┘
│  └──────────────────┘  │
└────────────┬────────────┘
             │
             ▼
┌─────────────────────────────────────────────────────────────┐
│                    Apache Kafka                             │
│  Topic: crypto.microstructure.vpin                          │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│              WebSocket Bridge → Frontend UI                 │
└─────────────────────────────────────────────────────────────┘
```

## Integration Methods

### Method 1: Python Plugin Bridge (Recommended)

This method allows the crypto-ingestion-engine to manage the C++ processor's lifecycle.

#### Step 1: Build the C++ Processor

```bash
cd vpin-cpp-processor
./build.sh build
```

#### Step 2: Copy Plugin Bridge

```bash
# Copy the Python plugin to crypto-ingestion-engine
cp src/api/plugin_bridge.py \
   /path/to/crypto-ingestion-engine/src/microstructure/plugins/vpin_cpp_plugin.py
```

#### Step 3: Configure crypto-ingestion-engine

Edit `crypto-ingestion-engine/config/settings.yaml`:

```yaml
microstructure:
  enabled: true
  processors:
    - name: vpin_cpp
      enabled: true
      config:
        executable_path: /path/to/vpin-cpp-processor/build/vpin-processor
        bucket_size: 10000
        num_buckets: 50
        emit_interval: 100
        auto_start: true
```

#### Step 4: Start the System

```bash
cd crypto-ingestion-engine
docker-compose up -d

# The C++ processor will be automatically started by the plugin
```

### Method 2: Docker Compose Integration

This method runs the C++ processor as a separate container.

#### Step 1: Build Docker Image

```bash
cd vpin-cpp-processor
docker build -t vpin-cpp-processor:latest .
```

#### Step 2: Update crypto-ingestion-engine docker-compose.yml

Add this service to `crypto-ingestion-engine/docker-compose.yml`:

```yaml
services:
  # ... existing services ...
  
  vpin-cpp-processor:
    image: vpin-cpp-processor:latest
    container_name: crypto-vpin-cpp
    restart: unless-stopped
    
    environment:
      KAFKA_BOOTSTRAP_SERVERS: kafka:9092
      KAFKA_GROUP_ID: vpin-cpp-processor
      KAFKA_INPUT_TOPIC: crypto.processed.trades
      KAFKA_OUTPUT_TOPIC: crypto.microstructure.vpin
      BUCKET_SIZE: 10000
      NUM_BUCKETS: 50
      EMIT_INTERVAL: 100
    
    depends_on:
      kafka:
        condition: service_healthy
    
    networks:
      - default
    
    deploy:
      resources:
        limits:
          cpus: '2.0'
          memory: 1G
```

#### Step 3: Start the System

```bash
cd crypto-ingestion-engine
docker-compose up -d
```

### Method 3: Standalone Deployment

Run the C++ processor independently on a separate machine.

#### Step 1: Build and Install

```bash
cd vpin-cpp-processor
./build.sh build
sudo ./build.sh install
```

#### Step 2: Configure Environment

```bash
# Create systemd service file
sudo tee /etc/systemd/system/vpin-processor.service << EOF
[Unit]
Description=VPIN C++ Processor
After=network.target

[Service]
Type=simple
User=vpin
Environment="KAFKA_BOOTSTRAP_SERVERS=kafka-server:9092"
Environment="KAFKA_INPUT_TOPIC=crypto.processed.trades"
Environment="KAFKA_OUTPUT_TOPIC=crypto.microstructure.vpin"
ExecStart=/usr/local/bin/vpin-processor --bucket-size 10000 --num-buckets 50
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
EOF
```

#### Step 3: Start Service

```bash
sudo systemctl daemon-reload
sudo systemctl enable vpin-processor
sudo systemctl start vpin-processor

# Check status
sudo systemctl status vpin-processor
```

## Configuration

### Environment Variables

Configure these in your deployment method:

| Variable | Default | Description |
|----------|---------|-------------|
| `KAFKA_BOOTSTRAP_SERVERS` | `kafka:9092` | Kafka broker(s) |
| `KAFKA_GROUP_ID` | `vpin-cpp-processor` | Consumer group |
| `KAFKA_INPUT_TOPIC` | `crypto.processed.trades` | Input topic |
| `KAFKA_OUTPUT_TOPIC` | `crypto.microstructure.vpin` | Output topic |

### VPIN Parameters

Adjust based on your trading characteristics:

```bash
# High-frequency trading (smaller buckets, more frequent updates)
--bucket-size 5000 --num-buckets 100 --emit-interval 50

# Medium-frequency trading (balanced)
--bucket-size 10000 --num-buckets 50 --emit-interval 100

# Low-frequency trading (larger buckets, less frequent updates)
--bucket-size 50000 --num-buckets 20 --emit-interval 200
```

## Data Flow

### Input: crypto.processed.trades

The C++ processor expects trades in this format:

```json
{
  "pair": "BTC-USD",
  "timestamp": 1733234567890,
  "price": 42000.50,
  "size": 0.5,
  "notional_value": 21000.25,
  "trade_direction": 1,
  "exchange_id": 23,
  "received_timestamp": 1733234567900
}
```

**Key Fields:**
- `pair`: Trading pair (e.g., "BTC-USD", "ETH-USD")
- `timestamp`: Trade timestamp in Unix milliseconds
- `price`: Trade price
- `size`: Trade size in base currency
- `notional_value`: price × size (in quote currency)
- `trade_direction`: 1 = buy, -1 = sell, 0 = unknown

### Output: crypto.microstructure.vpin

The processor produces VPIN metrics:

```json
{
  "pair": "BTC-USD",
  "timestamp": 1733234567890,
  "vpin": 0.35,
  "mean_vpin": 0.32,
  "std_vpin": 0.05,
  "max_vpin": 0.45,
  "min_vpin": 0.25,
  "bucket_count": 50,
  "total_volume": 500000.0,
  "processor": "vpin-cpp",
  "_topic": "crypto.microstructure.vpin"
}
```

**VPIN Interpretation:**
- `vpin`: Current VPIN value (0 to 1)
  - 0.0 - 0.2: Low toxicity (safe to trade)
  - 0.2 - 0.4: Moderate toxicity
  - 0.4 - 0.6: High toxicity (caution)
  - 0.6 - 1.0: Very high toxicity (adverse selection risk)

## Monitoring

### Health Checks

#### Docker
```bash
docker ps --filter name=vpin-cpp
docker logs -f crypto-vpin-cpp
```

#### Systemd
```bash
sudo systemctl status vpin-processor
sudo journalctl -u vpin-processor -f
```

### Kafka Monitoring

```bash
# Check consumer group lag
kafka-consumer-groups \
    --bootstrap-server localhost:9092 \
    --describe \
    --group vpin-cpp-processor

# Monitor output topic
kafka-console-consumer \
    --bootstrap-server localhost:9092 \
    --topic crypto.microstructure.vpin \
    --from-beginning
```

### Performance Metrics

The processor logs statistics every 30 seconds:

```
=== VPIN Processor Statistics ===
Engine:
  Trades processed: 1,234,567
  Metrics emitted: 12,345
  Active symbols: 15
  Avg processing time: 3.2 μs

Kafka Consumer:
  Messages consumed: 1,234,567
  Messages produced: 12,345
  Parse errors: 0
  Processing errors: 0
  Avg latency: 0.5 ms
```

## Troubleshooting

### Issue: No messages consumed

**Check 1: Verify input topic has data**
```bash
kafka-console-consumer \
    --bootstrap-server localhost:9092 \
    --topic crypto.processed.trades \
    --max-messages 10
```

**Check 2: Verify consumer group**
```bash
kafka-consumer-groups \
    --bootstrap-server localhost:9092 \
    --describe \
    --group vpin-cpp-processor
```

**Check 3: Check connectivity**
```bash
# From container
docker exec -it crypto-vpin-cpp ping kafka

# Test Kafka connection
telnet kafka 9092
```

### Issue: High latency

**Solution 1: Increase Kafka batch size**
Edit `src/kafka/kafka_consumer.cpp`:
```cpp
conf->set("batch.size", "32768", errstr);
conf->set("linger.ms", "5", errstr);
```

**Solution 2: Adjust VPIN parameters**
```bash
# Reduce emit frequency
--emit-interval 500

# Increase bucket size
--bucket-size 50000
```

### Issue: Memory usage growing

**Solution: Limit rolling window size**
```bash
# Reduce number of buckets
--num-buckets 20
```

## Performance Tuning

### CPU Optimization

```bash
# Check CPU features
lscpu | grep -i flags

# Rebuild with specific CPU target
cd vpin-cpp-processor/build
cmake -DCMAKE_CXX_FLAGS="-march=native -O3" ..
make clean && make -j$(nproc)
```

### Kafka Optimization

Add to `docker-compose.yml`:

```yaml
vpin-cpp-processor:
  environment:
    # Kafka consumer tuning
    KAFKA_FETCH_MIN_BYTES: 1024
    KAFKA_FETCH_MAX_WAIT_MS: 100
    KAFKA_MAX_PARTITION_FETCH_BYTES: 1048576
```

### Resource Limits

```yaml
deploy:
  resources:
    limits:
      cpus: '4.0'      # Increase for higher throughput
      memory: 2G       # Increase for more symbols
    reservations:
      cpus: '2.0'
      memory: 1G
```

## Testing

### Unit Testing

```bash
cd vpin-cpp-processor
./build.sh test
```

### Integration Testing

```bash
# 1. Start test Kafka
docker-compose -f docker-compose.test.yml up -d

# 2. Produce test trades
python tests/produce_test_trades.py

# 3. Consume VPIN metrics
python tests/consume_vpin_metrics.py

# 4. Verify results
python tests/verify_vpin_accuracy.py
```

### Load Testing

```bash
# Generate high-volume test data
python tests/load_test.py --trades-per-sec 100000 --duration 60

# Monitor processor performance
docker stats crypto-vpin-cpp
```

## Upgrading

### From Python VPIN to C++ VPIN

1. **Backup current configuration**
2. **Deploy C++ processor** (using any method above)
3. **Run both processors in parallel** for validation
4. **Compare outputs** to ensure consistency
5. **Disable Python VPIN** once validated
6. **Remove Python VPIN** from configuration

### Version Upgrades

```bash
# Pull latest code
git pull origin main

# Rebuild
cd vpin-cpp-processor
./build.sh rebuild

# Restart service
docker-compose restart vpin-cpp-processor
# OR
sudo systemctl restart vpin-processor
```

## Best Practices

1. **Start with conservative parameters** and tune based on observed behavior
2. **Monitor consumer lag** to ensure real-time processing
3. **Set up alerts** for processing errors and high latency
4. **Use separate consumer groups** for different environments (dev/staging/prod)
5. **Enable compression** on Kafka topics to reduce network bandwidth
6. **Scale horizontally** by running multiple instances with different consumer groups
7. **Regular backups** of configuration and state

## Support

For issues or questions:
- Check logs: `docker logs -f crypto-vpin-cpp`
- Review metrics: Statistics output every 30 seconds
- Kafka debugging: Use `kafka-console-consumer` to inspect topics
- GitHub Issues: Report bugs and feature requests

## Next Steps

After successful integration:
1. **Visualize VPIN metrics** in your UI
2. **Set up alerts** for high VPIN values
3. **Correlate with trading strategies** to avoid toxic flow
4. **Extend with additional microstructure metrics** (spread, depth, etc.)
5. **Optimize parameters** based on your specific trading pairs
