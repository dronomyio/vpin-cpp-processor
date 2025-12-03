# Quick Start Guide

Get the VPIN C++ Processor running in 5 minutes!

## Prerequisites

- Linux system (Ubuntu 20.04+ recommended)
- Docker and Docker Compose installed
- Running crypto-ingestion-engine instance

## Option 1: Docker (Fastest)

### Step 1: Build Docker Image

```bash
cd vpin-cpp-processor
docker build -t vpin-cpp-processor:latest .
```

### Step 2: Add to crypto-ingestion-engine

Edit `crypto-ingestion-engine/docker-compose.yml` and add:

```yaml
  vpin-cpp-processor:
    image: vpin-cpp-processor:latest
    container_name: crypto-vpin-cpp
    environment:
      KAFKA_BOOTSTRAP_SERVERS: kafka:9092
      KAFKA_INPUT_TOPIC: crypto.processed.trades
      KAFKA_OUTPUT_TOPIC: crypto.microstructure.vpin
    depends_on:
      - kafka
```

### Step 3: Start Everything

```bash
cd crypto-ingestion-engine
docker-compose up -d
```

### Step 4: Verify It's Working

```bash
# Check logs
docker logs -f crypto-vpin-cpp

# You should see:
# VPIN C++ Processor - High-Performance Microstructure Analytics
# VPIN Kafka Consumer started
# Trades processed: XXX
```

### Step 5: View VPIN Metrics

```bash
# Monitor output topic
docker exec -it crypto-kafka kafka-console-consumer \
    --bootstrap-server localhost:9092 \
    --topic crypto.microstructure.vpin \
    --from-beginning
```

Done! VPIN metrics are now flowing to your WebSocket bridge and UI.

## Option 2: Native Build

### Step 1: Install Dependencies

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    librdkafka-dev \
    nlohmann-json3-dev
```

### Step 2: Build

```bash
cd vpin-cpp-processor
./build.sh build
```

### Step 3: Run

```bash
# Set Kafka connection
export KAFKA_BOOTSTRAP_SERVERS=localhost:29092

# Start processor
./build/vpin-processor
```

## Verification

### Check Processing Statistics

The processor prints stats every 30 seconds:

```
=== VPIN Processor Statistics ===
Engine:
  Trades processed: 12,345
  Metrics emitted: 123
  Active symbols: 5
  Avg processing time: 3.2 μs
```

### Test VPIN Output

```bash
# View latest VPIN metrics
kafka-console-consumer \
    --bootstrap-server localhost:29092 \
    --topic crypto.microstructure.vpin \
    --max-messages 1
```

Expected output:
```json
{
  "pair": "BTC-USD",
  "timestamp": 1733234567890,
  "vpin": 0.35,
  "mean_vpin": 0.32,
  "bucket_count": 50,
  "total_volume": 500000.0,
  "processor": "vpin-cpp"
}
```

## Troubleshooting

### "No messages consumed"

**Check 1:** Verify crypto-ingestion-engine is running
```bash
docker-compose ps
```

**Check 2:** Verify trades are flowing
```bash
kafka-console-consumer \
    --bootstrap-server localhost:29092 \
    --topic crypto.processed.trades \
    --max-messages 1
```

**Check 3:** Check Kafka connection
```bash
docker logs crypto-vpin-cpp | grep -i error
```

### "Cannot connect to Kafka"

Update `KAFKA_BOOTSTRAP_SERVERS`:
- Docker: `kafka:9092`
- Native: `localhost:29092` or `localhost:9092`

### "librdkafka not found"

```bash
sudo apt-get install librdkafka-dev
./build.sh rebuild
```

## Next Steps

1. **View in UI**: Open `http://localhost:3000` to see VPIN metrics
2. **Tune Parameters**: Adjust bucket size and window in docker-compose.yml
3. **Monitor Performance**: Check statistics output every 30 seconds
4. **Read Full Docs**: See README.md and INTEGRATION.md

## Configuration Examples

### High-Frequency Trading
```yaml
environment:
  BUCKET_SIZE: 5000
  NUM_BUCKETS: 100
  EMIT_INTERVAL: 50
```

### Low-Frequency Trading
```yaml
environment:
  BUCKET_SIZE: 50000
  NUM_BUCKETS: 20
  EMIT_INTERVAL: 200
```

## Support

- **Documentation**: README.md, INTEGRATION.md
- **Logs**: `docker logs -f crypto-vpin-cpp`
- **Kafka Tools**: Use kafka-console-consumer to debug

Happy trading! 🚀
