# On-Chain Integration Guide
## Ethereum Mempool + MEV Detection + Risk Scoring

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│  Ethereum Ecosystem                                          │
├─────────────────────────────────────────────────────────────┤
│  • Infura/Alchemy (RPC Provider)                            │
│  • Flashbots Protect RPC                                    │
│  • EigenPhi API (MEV Analytics)                             │
│  • Etherscan API (Transaction data)                         │
└─────────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────────┐
│  On-Chain Monitor Service (New Component)                   │
│  Location: crypto-ingestion-engine/src/onchain/             │
├─────────────────────────────────────────────────────────────┤
│  1. eth_monitor.py - Main monitoring service                │
│  2. mev_detector.py - MEV pattern detection                 │
│  3. risk_scorer.py - Risk calculation engine                │
│  4. gas_oracle.py - Gas price tracking                      │
└─────────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────────┐
│  Kafka Topic: crypto.onchain.metrics                        │
└─────────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────────┐
│  WebSocket Bridge (Existing)                                │
│  - Add subscription to onchain topic                        │
└─────────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────────┐
│  UI (New Right Panel)                                       │
│  - On-chain metrics display                                 │
│  - Risk dashboard                                           │
└─────────────────────────────────────────────────────────────┘
```

---

## 1. On-Chain Monitor Service

### **File Structure**:

```
crypto-ingestion-engine/
└── src/
    └── onchain/
        ├── __init__.py
        ├── eth_monitor.py          # Main service
        ├── mev_detector.py         # MEV pattern detection
        ├── risk_scorer.py          # Risk calculation
        ├── gas_oracle.py           # Gas price tracking
        └── config.py               # Configuration
```

### **eth_monitor.py**:

```python
"""
Ethereum On-Chain Monitor

Monitors Ethereum mempool, blocks, and MEV activity.
Publishes metrics to Kafka for real-time risk assessment.
"""

import asyncio
import time
import logging
from web3 import Web3
from web3.middleware import geth_poa_middleware
from kafka import KafkaProducer
import json
import os
from typing import Dict, List, Optional

from .mev_detector import MEVDetector
from .gas_oracle import GasOracle

logger = logging.getLogger(__name__)


class EthereumMonitor:
    """
    Main on-chain monitoring service
    """
    
    def __init__(
        self,
        rpc_url: str,
        kafka_bootstrap: str,
        output_topic: str = "crypto.onchain.metrics"
    ):
        # Web3 connection
        self.w3 = Web3(Web3.HTTPProvider(rpc_url))
        self.w3.middleware_onion.inject(geth_poa_middleware, layer=0)
        
        if not self.w3.is_connected():
            raise ConnectionError(f"Failed to connect to Ethereum node: {rpc_url}")
        
        logger.info(f"Connected to Ethereum node: {rpc_url}")
        logger.info(f"Chain ID: {self.w3.eth.chain_id}")
        
        # Kafka producer
        self.producer = KafkaProducer(
            bootstrap_servers=kafka_bootstrap,
            value_serializer=lambda v: json.dumps(v).encode('utf-8'),
            compression_type='lz4'
        )
        self.output_topic = output_topic
        
        # Components
        self.mev_detector = MEVDetector(self.w3)
        self.gas_oracle = GasOracle(self.w3)
        
        # State
        self.last_block = 0
        self.metrics_buffer = []
        
    async def start(self):
        """Start monitoring"""
        logger.info("Starting Ethereum on-chain monitor...")
        
        # Run monitoring tasks concurrently
        await asyncio.gather(
            self.monitor_blocks(),
            self.monitor_mempool(),
            self.monitor_gas_prices(),
            self.publish_metrics()
        )
    
    async def monitor_blocks(self):
        """Monitor new blocks"""
        logger.info("Starting block monitor...")
        
        while True:
            try:
                current_block = self.w3.eth.block_number
                
                if current_block > self.last_block:
                    # Process new block
                    block = self.w3.eth.get_block(current_block, full_transactions=True)
                    
                    # Analyze block for MEV
                    mev_activity = self.mev_detector.analyze_block(block)
                    
                    # Publish metrics
                    self.publish({
                        'type': 'block',
                        'block_number': current_block,
                        'timestamp': int(time.time() * 1000),
                        'tx_count': len(block.transactions),
                        'gas_used': block.gasUsed,
                        'gas_limit': block.gasLimit,
                        'mev_activity': mev_activity
                    })
                    
                    self.last_block = current_block
                    logger.debug(f"Processed block {current_block}")
                
                await asyncio.sleep(1)  # Check every second
                
            except Exception as e:
                logger.error(f"Error monitoring blocks: {e}")
                await asyncio.sleep(5)
    
    async def monitor_mempool(self):
        """Monitor pending transactions"""
        logger.info("Starting mempool monitor...")
        
        while True:
            try:
                # Get pending transactions
                pending_filter = self.w3.eth.filter('pending')
                pending_txs = pending_filter.get_new_entries()
                
                if pending_txs:
                    # Analyze for MEV patterns
                    mev_patterns = self.mev_detector.detect_mev_in_mempool(pending_txs)
                    
                    # Publish metrics
                    self.publish({
                        'type': 'mempool',
                        'timestamp': int(time.time() * 1000),
                        'pending_count': len(pending_txs),
                        'mev_patterns': mev_patterns
                    })
                
                await asyncio.sleep(2)  # Check every 2 seconds
                
            except Exception as e:
                logger.error(f"Error monitoring mempool: {e}")
                await asyncio.sleep(5)
    
    async def monitor_gas_prices(self):
        """Monitor gas prices"""
        logger.info("Starting gas price monitor...")
        
        while True:
            try:
                gas_prices = self.gas_oracle.get_gas_prices()
                
                self.publish({
                    'type': 'gas',
                    'timestamp': int(time.time() * 1000),
                    'gas_prices': gas_prices
                })
                
                await asyncio.sleep(10)  # Update every 10 seconds
                
            except Exception as e:
                logger.error(f"Error monitoring gas prices: {e}")
                await asyncio.sleep(10)
    
    async def publish_metrics(self):
        """Publish aggregated metrics periodically"""
        while True:
            try:
                if self.metrics_buffer:
                    # Aggregate metrics
                    aggregated = self.aggregate_metrics(self.metrics_buffer)
                    
                    # Publish
                    self.publish({
                        'type': 'aggregated',
                        'timestamp': int(time.time() * 1000),
                        'metrics': aggregated
                    })
                    
                    # Clear buffer
                    self.metrics_buffer = []
                
                await asyncio.sleep(30)  # Publish every 30 seconds
                
            except Exception as e:
                logger.error(f"Error publishing metrics: {e}")
                await asyncio.sleep(30)
    
    def publish(self, message: Dict):
        """Publish message to Kafka"""
        try:
            self.producer.send(self.output_topic, message)
            self.metrics_buffer.append(message)
        except Exception as e:
            logger.error(f"Error publishing to Kafka: {e}")
    
    def aggregate_metrics(self, metrics: List[Dict]) -> Dict:
        """Aggregate metrics from buffer"""
        # Count by type
        counts = {}
        for m in metrics:
            mtype = m.get('type')
            counts[mtype] = counts.get(mtype, 0) + 1
        
        return {
            'counts': counts,
            'total': len(metrics)
        }
    
    def shutdown(self):
        """Cleanup"""
        logger.info("Shutting down Ethereum monitor...")
        self.producer.flush()
        self.producer.close()


# Main entry point
async def main():
    """Run the monitor"""
    import sys
    
    logging.basicConfig(
        level=logging.INFO,
        format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
    )
    
    # Configuration from environment
    rpc_url = os.getenv('ETH_RPC_URL', 'https://mainnet.infura.io/v3/YOUR_KEY')
    kafka_bootstrap = os.getenv('KAFKA_BOOTSTRAP_SERVERS', 'localhost:9092')
    
    monitor = EthereumMonitor(rpc_url, kafka_bootstrap)
    
    try:
        await monitor.start()
    except KeyboardInterrupt:
        logger.info("Received shutdown signal")
    finally:
        monitor.shutdown()


if __name__ == "__main__":
    asyncio.run(main())
```

### **mev_detector.py**:

```python
"""
MEV Pattern Detection

Detects sandwich attacks, frontrunning, backrunning, and other MEV patterns.
"""

import logging
from typing import Dict, List, Optional
from web3 import Web3
from collections import defaultdict

logger = logging.getLogger(__name__)


class MEVDetector:
    """
    Detect MEV patterns in blocks and mempool
    """
    
    def __init__(self, w3: Web3):
        self.w3 = w3
        
        # DEX router addresses (Uniswap, Sushiswap, etc.)
        self.dex_routers = {
            '0x7a250d5630B4cF539739dF2C5dAcb4c659F2488D': 'Uniswap V2',
            '0xE592427A0AEce92De3Edee1F18E0157C05861564': 'Uniswap V3',
            '0xd9e1cE17f2641f24aE83637ab66a2cca9C378B9F': 'Sushiswap',
        }
        
        # MEV bot addresses (known)
        self.known_mev_bots = set([
            '0x00000000003b3cc22aF3aE1EAc0440BcEe416B40',  # Flashbots
            # Add more known MEV bot addresses
        ])
    
    def analyze_block(self, block) -> Dict:
        """Analyze a block for MEV activity"""
        sandwich_attacks = []
        frontrun_attempts = []
        backrun_attempts = []
        
        txs = block.transactions
        
        # Group transactions by target
        tx_groups = self.group_by_target(txs)
        
        # Detect sandwich attacks (buy-victim-sell pattern)
        for target, group in tx_groups.items():
            if len(group) >= 3:
                for i in range(len(group) - 2):
                    if self.is_sandwich(group[i], group[i+1], group[i+2]):
                        sandwich_attacks.append({
                            'attacker': group[i]['from'],
                            'victim': group[i+1]['from'],
                            'profit_estimate': self.estimate_sandwich_profit(group[i:i+3])
                        })
        
        # Detect frontrunning
        for i in range(len(txs) - 1):
            if self.is_frontrun(txs[i], txs[i+1]):
                frontrun_attempts.append({
                    'attacker': txs[i]['from'],
                    'victim': txs[i+1]['from']
                })
        
        # Detect backrunning
        for i in range(len(txs) - 1):
            if self.is_backrun(txs[i], txs[i+1]):
                backrun_attempts.append({
                    'attacker': txs[i+1]['from'],
                    'target': txs[i]['from']
                })
        
        return {
            'sandwich_attacks': len(sandwich_attacks),
            'frontrun_attempts': len(frontrun_attempts),
            'backrun_attempts': len(backrun_attempts),
            'total_mev_txs': len(sandwich_attacks) + len(frontrun_attempts) + len(backrun_attempts),
            'details': {
                'sandwiches': sandwich_attacks[:5],  # Top 5
                'frontruns': frontrun_attempts[:5],
                'backruns': backrun_attempts[:5]
            }
        }
    
    def detect_mev_in_mempool(self, pending_txs: List) -> Dict:
        """Detect MEV patterns in pending transactions"""
        potential_victims = []
        potential_attackers = []
        
        for tx_hash in pending_txs:
            try:
                tx = self.w3.eth.get_transaction(tx_hash)
                
                # Check if targeting DEX
                if tx['to'] in self.dex_routers:
                    # High gas price = potential attacker
                    if tx['gasPrice'] > self.w3.eth.gas_price * 1.5:
                        potential_attackers.append(tx)
                    else:
                        potential_victims.append(tx)
                        
            except Exception as e:
                logger.debug(f"Error fetching tx {tx_hash}: {e}")
        
        return {
            'potential_victims': len(potential_victims),
            'potential_attackers': len(potential_attackers),
            'risk_level': 'high' if len(potential_attackers) > 10 else 'normal'
        }
    
    def group_by_target(self, txs: List) -> Dict:
        """Group transactions by target contract"""
        groups = defaultdict(list)
        for tx in txs:
            if tx['to']:
                groups[tx['to']].append(tx)
        return groups
    
    def is_sandwich(self, tx1, tx2, tx3) -> bool:
        """Check if three transactions form a sandwich attack"""
        # Same attacker for tx1 and tx3
        if tx1['from'] != tx3['from']:
            return False
        
        # Different victim
        if tx2['from'] == tx1['from']:
            return False
        
        # Same target (DEX router)
        if tx1['to'] != tx2['to'] or tx2['to'] != tx3['to']:
            return False
        
        # tx1 and tx3 have higher gas than tx2
        if tx1['gasPrice'] <= tx2['gasPrice'] or tx3['gasPrice'] <= tx2['gasPrice']:
            return False
        
        return True
    
    def is_frontrun(self, tx1, tx2) -> bool:
        """Check if tx1 frontruns tx2"""
        # Same target
        if tx1['to'] != tx2['to']:
            return False
        
        # tx1 has higher gas
        if tx1['gasPrice'] <= tx2['gasPrice']:
            return False
        
        # Different senders
        if tx1['from'] == tx2['from']:
            return False
        
        return True
    
    def is_backrun(self, tx1, tx2) -> bool:
        """Check if tx2 backruns tx1"""
        # tx2 immediately follows tx1
        # Same target or related
        if tx1['to'] == tx2['to']:
            return True
        
        return False
    
    def estimate_sandwich_profit(self, txs: List) -> float:
        """Estimate profit from sandwich attack"""
        # Simplified - would need to decode transaction data
        # and calculate actual token amounts
        return 0.0  # Placeholder
```

### **gas_oracle.py**:

```python
"""
Gas Price Oracle

Tracks gas prices and provides recommendations.
"""

import logging
from typing import Dict
from web3 import Web3

logger = logging.getLogger(__name__)


class GasOracle:
    """
    Gas price tracking and prediction
    """
    
    def __init__(self, w3: Web3):
        self.w3 = w3
        self.history = []
        self.max_history = 100
    
    def get_gas_prices(self) -> Dict:
        """Get current gas prices"""
        try:
            # Current gas price
            current = self.w3.eth.gas_price
            
            # Convert to gwei
            current_gwei = self.w3.from_wei(current, 'gwei')
            
            # Estimate fast/safe prices
            fast_gwei = current_gwei * 1.2
            safe_gwei = current_gwei * 0.8
            
            # Add to history
            self.history.append(current_gwei)
            if len(self.history) > self.max_history:
                self.history.pop(0)
            
            # Calculate trend
            trend = self.calculate_trend()
            
            return {
                'current': float(current_gwei),
                'fast': float(fast_gwei),
                'safe': float(safe_gwei),
                'trend': trend,
                'avg_1h': self.calculate_average()
            }
            
        except Exception as e:
            logger.error(f"Error getting gas prices: {e}")
            return {
                'current': 0,
                'fast': 0,
                'safe': 0,
                'trend': 'unknown',
                'avg_1h': 0
            }
    
    def calculate_trend(self) -> str:
        """Calculate gas price trend"""
        if len(self.history) < 10:
            return 'stable'
        
        recent = self.history[-10:]
        older = self.history[-20:-10] if len(self.history) >= 20 else recent
        
        recent_avg = sum(recent) / len(recent)
        older_avg = sum(older) / len(older)
        
        if recent_avg > older_avg * 1.1:
            return 'rising'
        elif recent_avg < older_avg * 0.9:
            return 'falling'
        else:
            return 'stable'
    
    def calculate_average(self) -> float:
        """Calculate average gas price"""
        if not self.history:
            return 0
        return sum(self.history) / len(self.history)
```

---

## 2. Docker Integration

### **Add to docker-compose.yml**:

```yaml
  onchain-monitor:
    build:
      context: .
      dockerfile: Dockerfile
    container_name: crypto-onchain-monitor
    restart: unless-stopped
    
    environment:
      ETH_RPC_URL: ${ETH_RPC_URL:-https://mainnet.infura.io/v3/YOUR_KEY}
      KAFKA_BOOTSTRAP_SERVERS: kafka:9092
      OUTPUT_TOPIC: crypto.onchain.metrics
    
    depends_on:
      kafka:
        condition: service_healthy
    
    networks:
      - crypto-network
    
    command: python -m src.onchain.eth_monitor
```

### **.env file**:

```bash
# Ethereum RPC
ETH_RPC_URL=https://mainnet.infura.io/v3/YOUR_INFURA_KEY

# Or use Alchemy
# ETH_RPC_URL=https://eth-mainnet.g.alchemy.com/v2/YOUR_ALCHEMY_KEY
```

---

## 3. WebSocket Bridge Update

### **Update ws_bridge.py** to subscribe to onchain topic:

```python
# In ws_bridge.py, add to KAFKA_TOPICS
KAFKA_TOPICS = [
    "crypto.processed.quotes",
    "crypto.processed.trades",
    "crypto.enriched.trades",
    "crypto.microstructure.vpin",
    "crypto.onchain.metrics"  # ← Add this
]
```

---

## 4. UI Integration

### **Update CryptoChartWithBands.jsx**:

```javascript
// Add on-chain state
const [onChainMetrics, setOnChainMetrics] = useState({
  gas_price: 0,
  mempool_size: 0,
  mev_activity: {},
  last_update: 0
});

// Handle on-chain messages
useEffect(() => {
  if (!ws) return;
  
  ws.onmessage = (event) => {
    const message = JSON.parse(event.data);
    const topic = message.topic || '';
    
    // ... existing handlers ...
    
    // On-chain metrics handler
    if (topic.includes('onchain')) {
      setOnChainMetrics(prev => ({
        ...prev,
        ...message.data,
        last_update: Date.now()
      }));
    }
  };
}, [ws]);

// Render right panel
<div className="w-1/3 p-4">
  <OnChainPanel metrics={onChainMetrics} />
  <RiskDashboard 
    microstructure={data[data.length - 1]} 
    onchain={onChainMetrics} 
  />
</div>
```

---

## 5. Testing

### **Test On-Chain Monitor**:

```bash
# Start the monitor standalone
cd crypto-ingestion-engine
python -m src.onchain.eth_monitor

# Check Kafka for messages
docker exec -it crypto-kafka kafka-console-consumer \
    --bootstrap-server localhost:9092 \
    --topic crypto.onchain.metrics \
    --from-beginning
```

---

## 6. API Keys Required

1. **Infura** or **Alchemy**: Ethereum RPC access
   - Sign up: https://infura.io or https://alchemy.com
   - Free tier: 100k requests/day

2. **EigenPhi** (Optional): MEV analytics API
   - Sign up: https://eigenphi.io
   - Provides detailed MEV data

3. **Flashbots** (Optional): MEV protection
   - No API key needed for RPC
   - Use: https://rpc.flashbots.net

---

## 7. Cost Considerations

| Service | Free Tier | Paid Tier | Recommended |
|---------|-----------|-----------|-------------|
| Infura | 100k req/day | $50/mo for 10M | Free tier OK |
| Alchemy | 300M compute units/mo | $49/mo | Free tier OK |
| EigenPhi | Limited | $99/mo | Optional |
| QuickNode | 10M credits | $9/mo | Good alternative |

**Recommendation**: Start with Infura/Alchemy free tier, upgrade if needed.

---

## 8. Next Steps

1. ✅ Create `src/onchain/` directory
2. ✅ Implement `eth_monitor.py`
3. ✅ Implement `mev_detector.py`
4. ✅ Implement `gas_oracle.py`
5. ✅ Update `docker-compose.yml`
6. ✅ Get Infura/Alchemy API key
7. ✅ Test on-chain monitor
8. ✅ Update WebSocket bridge
9. ✅ Create UI right panel
10. ✅ Integrate risk dashboard

This gives you **real-time on-chain intelligence** combined with market microstructure! 🚀
