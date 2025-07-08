# Audio Circular Buffer for BeagleY-AI C7x DSP

This project implements a circular buffer audio processing system that uses the C7x DSP cores on the BeagleY-AI (AM67A) for real-time audio processing. The system generates a 440Hz test tone, divides it into 8 chunks, and sends each chunk to the DSP for processing via shared memory and RPMsg communication.

## Architecture

```
┌─────────────────┐    RPMsg     ┌─────────────────┐
│  Linux App      │◄────────────►│  C7x DSP Core   │
│  (ARM Cortex-A) │              │                 │
└─────────────────┘              └─────────────────┘
         │                                │
         └────────── Shared Memory ───────┘
              (C7x DMA Region)
```

## Memory Layout

Based on the BeagleY-AI device tree:

- **C7x_0 DMA Memory**: `0xa3000000` (1MB)
  - Audio Buffer: `0xa3080000` (Upper 512KB)
- **C7x_1 DMA Memory**: `0xa4000000` (1MB) 
- **RTOS IPC Memory**: `0xa5000000` (28MB)

## Audio Processing Pipeline

1. **Linux Application**: Generates 440Hz sine wave
2. **Chunking**: Divides audio into 8 chunks of 6000 samples each
3. **Shared Memory**: Stores audio data in C7x DMA region
4. **RPMsg**: Sends processing commands to DSP
5. **DSP Processing**: 
   - Low-pass filtering (IIR filter)
   - Amplification (1.2x gain)
   - 16-bit ↔ float conversion
6. **Response**: DSP sends completion status via RPMsg

## File Structure

```
audio_circular_buffer/
├── audio_circular_buffer.c     # Linux userspace application
├── shared_memory.h/.c          # Shared memory management
├── c7x_audio_processor.c       # C7x DSP firmware
├── rsc_table.h/.c              # Remoteproc resource table
├── Makefile                    # Linux app build system
├── c7x_makefile               # DSP firmware build system
└── README.md                   # This file
```

## Prerequisites

### Hardware
- BeagleY-AI board with AM67A processor
- MicroSD card with BeagleY-AI Linux image

### Software
- Linux 6.1.73+ with remoteproc support
- GCC compiler for Linux application
- Optional: TI C7000 Code Generation Tools for optimized DSP code

### System Requirements
- Root access for `/dev/mem` and firmware loading
- RPMsg kernel modules loaded
- Remoteproc support enabled

## Building

### 1. Linux Application

```bash
# Build the Linux application
make

# Or with debug symbols
make debug

# Check environment
make setup
```

### 2. C7x DSP Firmware

```bash
# Build DSP firmware
make -f c7x_makefile

# Check development environment  
make -f c7x_makefile setup

# Quick build and deploy
make -f c7x_makefile deploy
```

## Installation

### 1. Install DSP Firmware

```bash
# Build and install firmware to both C7x cores
sudo make -f c7x_makefile install

# Check DSP status
make -f c7x_makefile status
```

### 2. Install Linux Application

```bash
# Install to system
sudo make install

# Or run directly
sudo ./audio_circular_buffer
```

## Usage

### Basic Usage

```bash
# Run with default settings
sudo ./audio_circular_buffer

# Show statistics every 5 seconds
sudo ./audio_circular_buffer --stats

# Verbose output
sudo ./audio_circular_buffer --verbose

# Help
./audio_circular_buffer --help
```

### Monitoring

```bash
# Check DSP status
cat /sys/class/remoteproc/remoteproc*/state

# Monitor RPMsg devices
ls -la /dev/rpmsg*

# View kernel messages
dmesg | grep -E "(dsp|rpmsg|remoteproc)"

# Check shared memory
sudo xxd -l 256 /dev/mem -s 0xa3080000
```

## System Configuration

### Enable Required Modules

```bash
# Load RPMsg modules if not auto-loaded
sudo modprobe rpmsg_char
sudo modprobe rpmsg_ns
sudo modprobe ti_k3_dsp_remoteproc
```

### Memory Permissions

```bash
# Allow access to /dev/mem (be careful!)
sudo chmod 666 /dev/mem

# Or add user to appropriate group
sudo usermod -a -G kmem $USER
```

## Troubleshooting

### DSP Not Starting

1. Check firmware file:
   ```bash
   ls -la /lib/firmware/j722s-c71_*-fw
   ```

2. Check remoteproc state:
   ```bash
   cat /sys/class/remoteproc/remoteproc*/state
   ```

3. Manual DSP start:
   ```bash
   sudo sh -c 'echo start > /sys/class/remoteproc/remoteproc0/state'
   ```

### RPMsg Communication Issues

1. Check for RPMsg devices:
   ```bash
   ls /dev/rpmsg*
   ```

2. Verify kernel modules:
   ```bash
   lsmod | grep rpmsg
   ```

3. Check kernel messages:
   ```bash
   dmesg | tail -20
   ```

### Memory Access Problems

1. Verify you're running as root
2. Check `/dev/mem` permissions
3. Confirm memory regions in device tree:
   ```bash
   cat /proc/device-tree/reserved-memory/*/reg | xxd
   ```

## Performance

### Expected Performance
- **Chunk Size**: 6000 samples (125ms at 48kHz)
- **Processing Latency**: < 10ms per chunk
- **Throughput**: ~48,000 samples/second
- **Memory Usage**: 512KB shared buffer

### Optimization Tips
1. Use TI C7000 compiler for DSP code
2. Enable C7x SIMD instructions
3. Optimize memory alignment
4. Minimize RPMsg message frequency

## Development

### Adding New Audio Processing

1. Modify `process_audio_chunk()` in `c7x_audio_processor.c`
2. Add new algorithm functions
3. Update shared memory structure if needed
4. Rebuild and deploy firmware

### Extending Message Protocol

1. Add new message types to both files
2. Update message structures
3. Implement handlers in both Linux app and DSP

### Testing

```bash
# Build and test basic functionality
make test

# Run with statistics for monitoring
sudo ./audio_circular_buffer --stats
```

## Known Limitations

1. **Single DSP Core**: Currently uses only one C7x core
2. **Simple Processing**: Basic filtering and amplification only
3. **Fixed Format**: 16-bit signed audio only
4. **Test Tone Only**: No real audio input/output

## Future Enhancements

- [ ] Dual C7x core processing
- [ ] Real audio I/O integration
- [ ] Advanced DSP algorithms (FFT, etc.)
- [ ] Multiple audio formats
- [ ] Dynamic chunk size configuration
- [ ] Performance profiling tools

## License

This project is provided as example code for BeagleY-AI development.

## Support

For issues related to:
- **BeagleY-AI Hardware**: [BeagleBoard Forum](https://forum.beagleboard.org/)
- **TI Processors**: [TI E2E Forums](https://e2e.ti.com/)
- **This Project**: Create an issue in the repository

## References

- [BeagleY-AI Documentation](https://docs.beagle.cc/boards/beagley/ai/)
- [TI AM67A Technical Reference](https://www.ti.com/product/AM67A)
- [Linux RemoteProc Framework](https://www.kernel.org/doc/Documentation/remoteproc.txt)
- [RPMsg Protocol](https://www.kernel.org/doc/Documentation/rpmsg.txt)
