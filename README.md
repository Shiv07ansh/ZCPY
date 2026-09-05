# ZCPY - Zero-Copy

### Universal Hardware-Agnostic Zero-Copy DMA Streaming Engine

**ESPRIT** (*Embedded Static-buffer Peripheral Ring-buffer Interface for Tensors*) is a high-performance, hardware-agnostic **zero-copy Direct Memory Access (DMA) streaming engine** written in bare-metal C.

It is designed to address a fundamental data-ingestion bottleneck in **TinyML and edge-AI systems**: eliminating CPU-driven memory copies when streaming high-bandwidth sensor data into neural-network workspaces.

> **Move the data, not the bytes.**

---

## Why ZCPY?

Embedded data pipelines commonly rely on CPU intervention between peripheral DMA buffers and application-level memory:

```text
Traditional Data Ingestion

┌──────────────────┐      ┌──────────────────┐      ┌────────────────────┐
│  Sensor / I2S    │ ───► │   DMA Buffer     │ ───► │  Model Input       │
│                  │      │                  │      │  Workspace         │
└──────────────────┘      └──────────────────┘      └────────────────────┘
                                  │
                                  │ CPU memcpy()
                                  ▼
                           Unnecessary Memory
                              Movement
```

For continuously running workloads such as **16 kHz audio** and multi-channel sensor streams, this introduces several costs:

* **CPU overhead** — processing cycles are spent moving data instead of performing inference.
* **Dynamic memory pressure** — conventional application pipelines may rely on runtime allocation.
* **Cache inefficiency** — poorly aligned or unnecessary memory movement can increase cache traffic.
* **Additional latency** — data must pass through intermediate software buffers before reaching the model.

ZCPY removes the copy stage by routing peripheral DMA transfers directly into **static, cache-aligned neural-network input buffers**.

```text
ZCPY

┌──────────────────┐
│  Sensor / I2S    │
└────────┬─────────┘
         │
         │ DMA
         ▼
┌──────────────────────────────┐
│       ZCPY Workspace         │
│                              │
│   ┌──────────┐ ┌──────────┐  │
│   │ Buffer A │ │ Buffer B │  │
│   │          │ │          │  │
│   │   DMA    │ │   CPU    │  │
│   │  FILLING │ │ PROCESS  │  │
│   └──────────┘ └──────────┘  │
│                              │
└──────────────┬───────────────┘
               │
               │ Pointer Swap
               ▼
        Neural Network
           Inference
```

No sensor bytes are copied by the CPU.

---

#  Architecture

ZCPY uses an **asynchronous double-buffered (ping-pong) ring-buffer architecture** coordinated at the hardware interrupt level.

```text
┌─────────────────────────────────────────────────────────────┐
│                        HARDWARE LAYER                       │
│                                                             │
│   Sensor / ADC / I2S                                        │
│          │                                                  │
│          │ DMA                                              │
│          ▼                                                  │
└──────────┼──────────────────────────────────────────────────┘
           │
           ▼
┌─────────────────────────────────────────────────────────────┐
│                    PHYSICAL SRAM WORKSPACE                  │
│                                                             │
│   ┌─────────────────────┐     ┌─────────────────────┐       │
│   │      Buffer A       │     │      Buffer B       │       │
│   │                     │     │                     │       │
│   │   DMA populating    │     │   CPU processing    │       │
│   │                     │     │                     │       │
│   └─────────────────────┘     └─────────────────────┘       │
│                                                             │
└───────────────────────────┬─────────────────────────────────┘
                            │
                            │ Zero-Copy Pointer Swap
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                       SOFTWARE LAYER                        │
│                                                             │
│       Buffer Ready → Queue → Inference(buffer_ptr)          │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

The hardware and software operate concurrently:

```text
Time ──────────────────────────────────────────────────────►

DMA:    [ Fill A ]──────[ Fill B ]──────[ Fill A ]──────
             │               │               │
CPU:         └─[ Process ]───┘─[ Process ]───┘─[ Process ]
                    A               B               A
```

While the CPU processes one buffer, DMA can populate the other.

---

##  Core Design Principles

### 1. Zero-Copy Pointer Swapping

The CPU never moves the sensor data itself.

When a DMA transfer completes, the ISR publishes **only the address of the completed buffer** through a thread-safe queue.

```text
DMA completes
     │
     ▼
   ISR
     │
     │  pointer only
     ▼
┌───────────────┐
│ FreeRTOS Queue│
└───────┬───────┘
        │
        ▼
run_inference(buffer_ptr)
```

The actual data remains exactly where DMA placed it.

---

### 2. Hardware-Yielded Execution

The processing task does not continuously poll for new data.

Instead, it yields while waiting for the next completed buffer:

```text
┌──────────────────────┐
│   Wait for Buffer    │
└──────────┬───────────┘
           │
           │ DMA complete
           ▼
┌──────────────────────┐
│   Process Buffer     │
└──────────┬───────────┘
           │
           ▼
      Wait Again
```

This allows the processor to perform other work while hardware handles data acquisition.

---

### 3. Ping-Pong Double Buffering

Two static buffers alternate between DMA and CPU ownership:

```text
             ┌───────────────┐
             │    Buffer A   │
             └───────┬───────┘
                     │
             DMA ────┤
                     │
                     ▼
                ┌─────────┐
                │  Ready  │
                └────┬────┘
                     │
                     ▼
                   CPU

             ┌───────────────┐
             │    Buffer B   │
             └───────┬───────┘
                     │
                   CPU
                     │
                     ▼
                  Process
```

At each frame boundary, the roles swap.

This enables **data acquisition and inference to overlap** rather than executing as strictly sequential stages.

---

### 4. Cache-Aligned Static Buffers

Buffers are allocated in internal SRAM and aligned to **32-byte boundaries**:

```c
__attribute__((aligned(32)))
```

ESP-IDF DMA-specific allocation attributes are used where required:

```c
DMA_ATTR
```

The goal is to provide predictable physical memory placement and appropriate alignment for DMA and CPU cache behavior.

---

#  Modular Hardware Abstraction

ZCPY separates the **generic ring-buffer implementation** from the hardware-specific DMA backend.

```text
                         ┌────────────────────────┐
                         │     dma_ringbuf.c      │
                         │                        │
                         │    Universal Core      │
                         │       HAL              │
                         └───────────┬────────────┘
                                     │
                    ┌────────────────┴────────────────┐
                    │                                 │
                    ▼                                 ▼
       ┌────────────────────────┐       ┌────────────────────────┐
       │  target_esp32s3.c      │       │   target_posix.c       │
       │                        │       │                        │
       │  ESP32-S3 Hardware     │       │   Host PC Simulator    │
       │  I2S / DMA / ISR       │       │   Linux / macOS        │
       └────────────────────────┘       └────────────────────────┘
```

### Target-Agnostic Core

`src/dma_ringbuf.c`

Provides the portable interface for:

* Buffer acquisition
* Buffer release
* Ring-buffer management
* Pointer handshaking
* Lifecycle management

### ESP32-S3 Backend

`src/targets/target_esp32s3.c`

Implements the hardware-specific components:

* I2S configuration
* DMA descriptors
* DMA callbacks
* Interrupt handling
* ESP32-S3 peripheral integration

### POSIX Simulator

`src/targets/target_posix.c`

Provides a host-side simulation of the DMA pipeline for:

* Linux
* macOS
* Automated testing
* Off-target development
* Debugging the ring-buffer logic without physical hardware

---

#  Project Structure

The repository follows an **ESP-IDF component architecture** while also supporting native host builds through CMake.

```text
MicroP/
│
├── CMakeLists.txt
│
├── components/
│   └── ZCPY_driver/
│       ├── CMakeLists.txt
│       │
│       ├── include/
│       │   └── dma_ringbuf.h
│       │       # Hardware-agnostic public C API
│       │
│       └── src/
│           ├── dma_ringbuf.c
│           │   # Core ring-buffer / pointer-swapping logic
│           │
│           └── targets/
│               ├── target_esp32s3.c
│               │   # ESP32-S3 I2S / DMA backend
│               │
│               └── target_posix.c
│                   # Host simulation backend
│
└── main/
    ├── CMakeLists.txt
    └── main.c
        # Test application
```

---

## Why This Structure?

### Strict Encapsulation

Only the public API is exposed:

```text
components/ZCPY_driver/include/dma_ringbuf.h
```

Implementation details such as DMA configuration and ISR handling remain inside the component.

---

###  Reusability

The driver is structured as a standalone ESP-IDF component that can be:

* Copied into another project
* Included as a Git submodule
* Integrated into an existing ESP-IDF application

---

###  Dual Build Targets

The project is designed to support both:

```text
                 ZCPY
                   │
          ┌────────┴────────┐
          │                 │
          ▼                 ▼
      ESP-IDF             POSIX
          │                 │
          ▼                 ▼
     ESP32-S3          Linux / macOS
     Real DMA          Simulation
```

This allows core logic to be developed and tested without requiring physical embedded hardware for every iteration.

---

# 🧪 Host Simulation

The POSIX backend allows the zero-copy buffer logic to be tested on a desktop system.

### Build

```bash
# Clone the repository
git clone https://github.com/your-username/MicroP.git
cd MicroP

# Configure the native build
mkdir build
cd build
cmake ..

# Compile
make

# Run
./app
```

### Expected Output

```text
[POSIX_SIM] Simulated DMA Driver Initialized.
Zero-Copy DMA Streaming Pipeline Active.

[HOST] Processed Frame 1 at Address: 0x1005a3080 (Sample Byte: 0x01)
[HOST] Processed Frame 2 at Address: 0x1005a3280 (Sample Byte: 0x02)
[HOST] Processed Frame 3 at Address: 0x1005a3080 (Sample Byte: 0x01)
[HOST] Processed Frame 4 at Address: 0x1005a3280 (Sample Byte: 0x02)

...

[POSIX_SIM] Simulated DMA Driver Stopped.
```

Notice that the address alternates between the two buffers:

```text
Buffer A → Buffer B → Buffer A → Buffer B
```

The data is transferred by reference rather than copied between memory regions.

---

#  ESP32-S3 Deployment

The hardware backend targets the **ESP32-S3** using ESP-IDF.

### Prerequisites

* ESP-IDF v5.0+
* ESP32-S3 development board
* Compatible I2S peripheral / sensor

### Configure the Target

```bash
idf.py set-target esp32s3
```

### Build

```bash
idf.py build
```

### Flash & Monitor

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

### Expected Runtime Behavior

```text
I (320) DMA_TARGET_ESP32S3:
ESP32-S3 Zero-Copy DMA Driver initialized.

Zero-Copy DMA Streaming Pipeline Active.

I (325) MAIN_APP:
Frame 1 @ 0x3fc9a200

I (330) MAIN_APP:
Frame 2 @ 0x3fc9a400

I (335) MAIN_APP:
Frame 3 @ 0x3fc9a200
```

The alternating addresses demonstrate the ping-pong buffer mechanism.

---

#  Roadmap

ZCPY is currently focused on establishing a clean, portable zero-copy DMA architecture. Future development will extend the system toward broader peripheral support and deeper TinyML integration.

### Phase 1 — Core DMA Infrastructure

* [x] Static double-buffer architecture
* [x] Zero-copy pointer handoff
* [x] Interrupt-driven buffer completion
* [x] Cache-aligned buffers
* [x] ESP32-S3 backend
* [x] POSIX simulation backend
* [x] ESP-IDF component structure
* [x] Native host build support

### Phase 2 — Peripheral Expansion

* [ ] SPI DMA support
* [ ] ADC DMA support
* [ ] Additional I2S configurations
* [ ] Multi-channel streaming
* [ ] Configurable buffer sizes
* [ ] Multiple simultaneous DMA streams

### Phase 3 — TinyML Integration

* [ ] Direct integration with neural-network input tensors
* [ ] Tensor-aware buffer management
* [ ] Streaming inference pipeline
* [ ] Integration with AOT inference runtimes
* [ ] End-to-end sensor → DMA → inference examples

### Phase 4 — Performance Evaluation

* [ ] Cycle-level profiling
* [ ] CPU utilization measurements
* [ ] Memory-bandwidth analysis
* [ ] Copy-vs-zero-copy comparison
* [ ] Latency and jitter characterization
* [ ] Sustained streaming stress tests

> **Performance benchmarks will be added once the measurement methodology and test workloads are finalized.**

---

#  Design Philosophy

ZCPY is built around three principles:

```text
             ┌──────────────────────┐
             │    HARDWARE DMA      │
             └──────────┬───────────┘
                        │
                        ▼
              ┌──────────────────┐
              │   STATIC SRAM    │
              │                  │
              │  Buffer A / B    │
              └────────┬─────────┘
                       │
                 Pointer Only
                       │
                       ▼
              ┌──────────────────┐
              │  TINYML / DSP    │
              │    WORKLOAD      │
              └──────────────────┘
```

### **No unnecessary copies.**

### **No unnecessary polling.**

### **No unnecessary coupling to hardware.**

The long-term goal is a reusable streaming layer that allows embedded ML applications to treat **DMA-backed sensor buffers as direct inference inputs**, minimizing the software overhead between physical data acquisition and computation.

---

# Project Status

**Status:**  Active Development

**Current Target:** ESP32-S3
**Language:** C
**Build Systems:** ESP-IDF / CMake
**Host Support:** POSIX
**Architecture:** Zero-Copy DMA + Static Ping-Pong Buffers

The current implementation establishes the core **DMA → static buffer → pointer handoff → processing** pipeline with both a real ESP32-S3 backend and a host-side simulation environment.

Performance benchmarking is intentionally left as a future phase rather than reporting unverified measurements.

---

##  License

This project is licensed under the **MIT License**. See [`LICENSE`](LICENSE) for details.
