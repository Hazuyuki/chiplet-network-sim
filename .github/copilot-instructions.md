# CNSim (Chiplet Network Simulator) AI Coding Assistant Instructions

## Project Overview
CNSim is a cycle-accurate, packet-parallel simulator for chiplet-based network topologies. It supports multi-threading, configurable routing algorithms, and various topologies like NVSwitch, Mesh, Torus, Dragonfly, and FatTree. The simulator evaluates performance of inter-chiplet/inter-GPU networks.

## Architecture
- **Layered Design**: `System` → `Group` → `Node` → `Buffer`
- **Factory Pattern**: `System::New()` creates topology-specific subclasses
- **Multi-threading**: Packet-parallel updates using atomic indices and condition variables
- **Router Pipeline**: Configurable 1/2/3-stage (routing → VC allocation → switch allocation)

## Key Components
- `src/system.*`: Base system class with router pipeline
- `src/topologies/`: Topology implementations (e.g., `nvswitch.*`, `fat_tree.*`)
- `src/traffic/`: Traffic pattern generators (uniform, hotspot, collectives)
- `src/traffic_manager.*`: Simulation orchestration and statistics
- `src/buffer.*`: Virtual channel management
- `src/packet.*`: Packet state tracking with flit traces

## Development Workflow
### Building
- Use CMake presets: `cmake --preset Release`, `cd builds/Release`, `cmake --build .`
- Main executable: `ChipletNetworkSim` (runs full simulations)
- Test executables: Built separately (e.g., `test_nvswitch_topology`) - each `src/topologies/test_*.cpp` becomes an exe

### Adding Tests
- Create `src/topologies/test_new_feature.cpp`
- Follow pattern: Include test cpp + all src except main.cpp in CMakeLists.txt
- Run: `./build/test_new_feature input/config.ini`

### Configuration
- INI format: `[Network]`, `[Workload]`, `[Simulation]`, `[Files]`
- Example: `topology = NVSwitch`, `traffic = uniform`, `threads = 8`

### Analysis
- Simulations output CSV files to `output/`
- Plot with Python scripts (e.g., `plot_credit_delay.py`) using matplotlib
- Common workflow: Run experiment script → Generate CSV → Plot results

## Coding Patterns
### Topology Implementation
- Inherit from `System`, implement `routing_algorithm()` and `read_config()`
- Use `Group` subclasses for logical groupings (e.g., servers, chiplets)
- Nodes represent routers/switches, buffers handle VC allocation

### Packet Updates
- Multi-threaded: Workers fetch packets via `pkt_i` atomic, update in parallel
- Single-threaded phases: Credit processing, link releases, packet deletion

### Naming Conventions
- Member variables: `num_cores_`, `vc_num_`
- Functions: `routing_algorithm()`, `vc_allocate()`
- Classes: `NVSwitchSystem`, `FatTreeGroup`

### Style
- Google C++ style with 100-column limit (enforced by `.clang-format`)
- Use `boost::mt19937` for randomness
- Extensive use of vectors and pointers for dynamic structures

## Common Tasks
- **New Topology**: Add class in `src/topologies/`, register in `System::New()`, update CMake
- **Traffic Pattern**: Add to `src/traffic/`, integrate in `TrafficManager`
- **Debugging**: Use test executables with specific configs, check CSV outputs
- **Performance**: Adjust `threads`, `issue_width` for parallelism tuning

## Key Files
- `src/main.cpp`: Threading logic and simulation loop
- `src/config.*`: INI parsing
- `CMakeLists.txt`: Build configuration with separate test targets
- `input/*.ini`: Example configurations
- `docs/`: Chinese documentation for interfaces and analysis</content>
<parameter name="filePath">/share_data/zhuyu/chiplet-network-sim/.github/copilot-instructions.md