FROM ubuntu:22.04 AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    librdkafka-dev \
    nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Copy source code
COPY include/ ./include/
COPY src/ ./src/
COPY CMakeLists.txt ./

# Build
RUN mkdir build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j$(nproc) && \
    strip vpin-processor

# Runtime stage
FROM ubuntu:22.04

# Install runtime dependencies only
RUN apt-get update && apt-get install -y --no-install-recommends \
    librdkafka1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy binary from builder
COPY --from=builder /build/build/vpin-processor /app/

# Create non-root user
RUN useradd -m -u 1000 vpin && \
    chown -R vpin:vpin /app

USER vpin

# Environment variables (can be overridden)
ENV KAFKA_BOOTSTRAP_SERVERS=kafka:9092
ENV KAFKA_GROUP_ID=vpin-cpp-processor
ENV KAFKA_INPUT_TOPIC=crypto.processed.trades
ENV KAFKA_OUTPUT_TOPIC=crypto.microstructure.vpin

# Default configuration
ENV BUCKET_SIZE=10000
ENV NUM_BUCKETS=50
ENV EMIT_INTERVAL=100

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD pgrep -x vpin-processor || exit 1

# Run the processor
CMD ["/bin/sh", "-c", "/app/vpin-processor --bucket-size ${BUCKET_SIZE} --num-buckets ${NUM_BUCKETS} --emit-interval ${EMIT_INTERVAL}"]
