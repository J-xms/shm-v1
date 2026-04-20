# shm-v1

POSIX Shared Memory Read/Write Test Tool

## Overview

This project provides a C++ program that performs continuous read/write operations on POSIX shared memory objects, monitoring virtual and physical memory addresses.

## Features

- Uses `shm_open()` POSIX API for shared memory creation/access
- Random string generation for write verification
- Real-time display of virtual and physical memory addresses
- Configurable duration and operation frequency

## Build

```bash
g++ -o shm_rw_test shm_rw_test.cpp -std=c++11 -pthread -lrt
```

## Usage

```bash
./shm_rw_test <shared_memory_name>
```

Example:
```bash
./shm_rw_test /test_shm
```

## Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| Duration | 120 seconds | Test running time |
| Operations | 10/second | Read/write frequency |
| Memory Size | 1 MB | Shared memory segment size |
| String Size | 256 bytes | Random string length |

## Output

Each operation displays:
- Elapsed time (seconds)
- Virtual memory address
- Physical memory address (requires pagemap access)
- Write content (first 32 chars)
- Read content (first 32 chars)
- Verification result (OK/FAIL)

## Requirements

- Linux with POSIX shared memory support
- `librt` library for `shm_open()`
- `/proc/self/pagemap` access for physical address (optional)

## License

MIT