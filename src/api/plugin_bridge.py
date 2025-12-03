"""
VPIN C++ Processor Plugin Bridge

This module provides the integration layer between the crypto-ingestion-engine
and the external C++ VPIN processor. It registers the C++ processor as a
pluggable component and manages its lifecycle.

Architecture:
    1. Python plugin system discovers this module
    2. Plugin starts/stops the C++ processor as a subprocess
    3. C++ processor consumes from Kafka independently
    4. Metrics are published to Kafka output topic
    5. WebSocket bridge picks up metrics automatically

Usage:
    The plugin is automatically discovered and registered when placed in:
    crypto-ingestion-engine/src/microstructure/plugins/
"""

import subprocess
import os
import signal
import time
import logging
from typing import Optional, Dict, Any
from pathlib import Path

logger = logging.getLogger(__name__)


class VPINCppPlugin:
    """
    Plugin bridge for C++ VPIN processor
    
    This class manages the lifecycle of the external C++ VPIN processor,
    allowing it to run as an independent high-performance service while
    integrating with the Python-based crypto-ingestion-engine.
    """
    
    def __init__(
        self,
        executable_path: Optional[str] = None,
        bucket_size: float = 10000.0,
        num_buckets: int = 50,
        emit_interval: int = 100,
        auto_start: bool = True
    ):
        """
        Initialize the C++ VPIN processor plugin
        
        Args:
            executable_path: Path to vpin-processor binary (auto-detected if None)
            bucket_size: Volume bucket size in quote currency
            num_buckets: Number of buckets in rolling window
            emit_interval: Emit metric every N trades
            auto_start: Automatically start processor on initialization
        """
        self.executable_path = executable_path or self._find_executable()
        self.bucket_size = bucket_size
        self.num_buckets = num_buckets
        self.emit_interval = emit_interval
        self.process: Optional[subprocess.Popen] = None
        self.running = False
        
        if auto_start:
            self.start()
    
    def _find_executable(self) -> str:
        """Auto-detect the C++ processor executable"""
        # Check common locations
        search_paths = [
            Path(__file__).parent.parent.parent / "build" / "vpin-processor",
            Path("/usr/local/bin/vpin-processor"),
            Path.home() / "vpin-cpp-processor" / "build" / "vpin-processor",
        ]
        
        for path in search_paths:
            if path.exists() and path.is_file():
                logger.info(f"Found VPIN C++ processor at: {path}")
                return str(path)
        
        # Fallback to PATH
        logger.warning("VPIN C++ processor not found in common locations, using PATH")
        return "vpin-processor"
    
    def start(self) -> bool:
        """
        Start the C++ VPIN processor
        
        Returns:
            True if started successfully, False otherwise
        """
        if self.running:
            logger.warning("C++ VPIN processor already running")
            return True
        
        if not os.path.exists(self.executable_path):
            logger.error(f"Executable not found: {self.executable_path}")
            logger.info("Please build the C++ processor first:")
            logger.info("  cd vpin-cpp-processor && mkdir build && cd build")
            logger.info("  cmake .. && make")
            return False
        
        # Build command line arguments
        cmd = [
            self.executable_path,
            "--bucket-size", str(self.bucket_size),
            "--num-buckets", str(self.num_buckets),
            "--emit-interval", str(self.emit_interval)
        ]
        
        # Set environment variables from current environment
        env = os.environ.copy()
        
        try:
            logger.info(f"Starting C++ VPIN processor: {' '.join(cmd)}")
            self.process = subprocess.Popen(
                cmd,
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True
            )
            
            # Wait a bit to check if it started successfully
            time.sleep(1)
            
            if self.process.poll() is not None:
                # Process exited immediately
                stdout, stderr = self.process.communicate()
                logger.error(f"C++ processor failed to start:")
                logger.error(f"STDOUT: {stdout}")
                logger.error(f"STDERR: {stderr}")
                return False
            
            self.running = True
            logger.info(f"C++ VPIN processor started (PID: {self.process.pid})")
            return True
            
        except Exception as e:
            logger.error(f"Failed to start C++ processor: {e}")
            return False
    
    def stop(self) -> bool:
        """
        Stop the C++ VPIN processor
        
        Returns:
            True if stopped successfully, False otherwise
        """
        if not self.running or self.process is None:
            logger.warning("C++ VPIN processor not running")
            return True
        
        try:
            logger.info(f"Stopping C++ VPIN processor (PID: {self.process.pid})")
            
            # Send SIGTERM for graceful shutdown
            self.process.send_signal(signal.SIGTERM)
            
            # Wait up to 10 seconds for graceful shutdown
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                logger.warning("Graceful shutdown timed out, sending SIGKILL")
                self.process.kill()
                self.process.wait()
            
            self.running = False
            self.process = None
            logger.info("C++ VPIN processor stopped")
            return True
            
        except Exception as e:
            logger.error(f"Failed to stop C++ processor: {e}")
            return False
    
    def restart(self) -> bool:
        """Restart the C++ processor"""
        self.stop()
        time.sleep(1)
        return self.start()
    
    def is_running(self) -> bool:
        """Check if the C++ processor is running"""
        if not self.running or self.process is None:
            return False
        
        # Check if process is still alive
        if self.process.poll() is not None:
            self.running = False
            return False
        
        return True
    
    def get_status(self) -> Dict[str, Any]:
        """Get processor status"""
        return {
            "running": self.is_running(),
            "pid": self.process.pid if self.process else None,
            "executable": self.executable_path,
            "config": {
                "bucket_size": self.bucket_size,
                "num_buckets": self.num_buckets,
                "emit_interval": self.emit_interval
            }
        }
    
    def __enter__(self):
        """Context manager entry"""
        self.start()
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit"""
        self.stop()
    
    def __del__(self):
        """Cleanup on deletion"""
        if self.running:
            self.stop()


# ============================================================================
# Integration with crypto-ingestion-engine plugin system
# ============================================================================

# Global plugin instance
_cpp_plugin: Optional[VPINCppPlugin] = None


def register_plugin(config: Optional[Dict[str, Any]] = None) -> VPINCppPlugin:
    """
    Register the C++ VPIN processor plugin
    
    This function is called by the crypto-ingestion-engine plugin system
    to initialize and register the external C++ processor.
    
    Args:
        config: Optional configuration dictionary
        
    Returns:
        VPINCppPlugin instance
    """
    global _cpp_plugin
    
    if _cpp_plugin is not None:
        logger.warning("C++ VPIN plugin already registered")
        return _cpp_plugin
    
    config = config or {}
    
    _cpp_plugin = VPINCppPlugin(
        executable_path=config.get("executable_path"),
        bucket_size=config.get("bucket_size", 10000.0),
        num_buckets=config.get("num_buckets", 50),
        emit_interval=config.get("emit_interval", 100),
        auto_start=config.get("auto_start", True)
    )
    
    logger.info("C++ VPIN processor plugin registered")
    return _cpp_plugin


def unregister_plugin():
    """Unregister and stop the C++ processor"""
    global _cpp_plugin
    
    if _cpp_plugin is not None:
        _cpp_plugin.stop()
        _cpp_plugin = None
        logger.info("C++ VPIN processor plugin unregistered")


def get_plugin() -> Optional[VPINCppPlugin]:
    """Get the registered plugin instance"""
    return _cpp_plugin


# ============================================================================
# Standalone usage
# ============================================================================

if __name__ == "__main__":
    import sys
    
    logging.basicConfig(
        level=logging.INFO,
        format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
    )
    
    print("VPIN C++ Processor Plugin Bridge")
    print("=" * 50)
    
    if len(sys.argv) > 1:
        if sys.argv[1] == "start":
            plugin = register_plugin()
            print(f"Status: {plugin.get_status()}")
            print("\nPress Ctrl+C to stop...")
            try:
                while True:
                    time.sleep(1)
            except KeyboardInterrupt:
                print("\nStopping...")
                unregister_plugin()
        elif sys.argv[1] == "status":
            plugin = get_plugin()
            if plugin:
                print(f"Status: {plugin.get_status()}")
            else:
                print("Plugin not registered")
        else:
            print(f"Unknown command: {sys.argv[1]}")
            print("Usage: python plugin_bridge.py [start|status]")
    else:
        print("Usage: python plugin_bridge.py [start|status]")
