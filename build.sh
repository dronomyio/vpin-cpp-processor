#!/bin/bash
set -e

# VPIN C++ Processor - Build and Deployment Script

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
BUILD_TYPE="${BUILD_TYPE:-Release}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== VPIN C++ Processor Build Script ===${NC}"
echo ""

# Function to print colored messages
info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1"
    exit 1
}

# Check dependencies
check_dependencies() {
    info "Checking dependencies..."
    
    # Check for required commands
    for cmd in cmake make g++; do
        if ! command -v $cmd &> /dev/null; then
            error "$cmd not found. Please install build-essential and cmake"
        fi
    done
    
    # Check for librdkafka
    if ! pkg-config --exists rdkafka 2>/dev/null; then
        warn "librdkafka not found via pkg-config"
        warn "Attempting to continue, but build may fail"
        warn "Install with: sudo apt-get install librdkafka-dev"
    else
        info "librdkafka found: $(pkg-config --modversion rdkafka)"
    fi
    
    # Check for nlohmann/json
    if [ ! -f "/usr/include/nlohmann/json.hpp" ] && [ ! -f "/usr/local/include/nlohmann/json.hpp" ]; then
        warn "nlohmann/json not found"
        warn "Install with: sudo apt-get install nlohmann-json3-dev"
    else
        info "nlohmann/json found"
    fi
    
    # Check CPU features
    if grep -q avx2 /proc/cpuinfo; then
        info "AVX2 support detected"
    else
        warn "AVX2 not detected - SIMD optimizations will be limited"
    fi
    
    if grep -q avx512 /proc/cpuinfo; then
        info "AVX512 support detected"
    fi
}

# Clean build directory
clean() {
    info "Cleaning build directory..."
    rm -rf "${BUILD_DIR}"
    info "Clean complete"
}

# Build the project
build() {
    info "Building VPIN C++ Processor (${BUILD_TYPE})..."
    
    # Create build directory
    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"
    
    # Configure with CMake
    info "Running CMake..."
    cmake -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" ..
    
    # Build
    info "Compiling..."
    make -j$(nproc)
    
    # Check if binary was created
    if [ -f "vpin-processor" ]; then
        info "Build successful!"
        info "Binary location: ${BUILD_DIR}/vpin-processor"
        
        # Print binary info
        ls -lh vpin-processor
        file vpin-processor
    else
        error "Build failed - binary not found"
    fi
}

# Install the binary
install() {
    if [ ! -f "${BUILD_DIR}/vpin-processor" ]; then
        error "Binary not found. Please build first."
    fi
    
    info "Installing VPIN processor..."
    sudo make -C "${BUILD_DIR}" install
    info "Installation complete"
    info "Binary installed to: /usr/local/bin/vpin-processor"
}

# Run tests
test() {
    if [ ! -d "${BUILD_DIR}" ]; then
        error "Build directory not found. Please build first."
    fi
    
    info "Running tests..."
    cd "${BUILD_DIR}"
    ctest --output-on-failure
}

# Build Docker image
docker_build() {
    info "Building Docker image..."
    cd "${SCRIPT_DIR}"
    docker build -t vpin-cpp-processor:latest .
    info "Docker image built: vpin-cpp-processor:latest"
}

# Show usage
usage() {
    echo "Usage: $0 [COMMAND]"
    echo ""
    echo "Commands:"
    echo "  check       Check dependencies"
    echo "  clean       Clean build directory"
    echo "  build       Build the project (default)"
    echo "  rebuild     Clean and build"
    echo "  install     Install binary system-wide"
    echo "  test        Run tests"
    echo "  docker      Build Docker image"
    echo "  all         Check, build, and test"
    echo ""
    echo "Environment Variables:"
    echo "  BUILD_TYPE  Build type (Debug|Release) [default: Release]"
    echo ""
    echo "Examples:"
    echo "  $0 build"
    echo "  BUILD_TYPE=Debug $0 rebuild"
    echo "  $0 docker"
}

# Main script
main() {
    case "${1:-build}" in
        check)
            check_dependencies
            ;;
        clean)
            clean
            ;;
        build)
            check_dependencies
            build
            ;;
        rebuild)
            check_dependencies
            clean
            build
            ;;
        install)
            install
            ;;
        test)
            test
            ;;
        docker)
            docker_build
            ;;
        all)
            check_dependencies
            clean
            build
            test
            ;;
        help|--help|-h)
            usage
            ;;
        *)
            error "Unknown command: $1"
            usage
            ;;
    esac
}

main "$@"
