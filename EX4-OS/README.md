# Virtual Memory Simulation in C++

This project simulates a virtual memory system, including hierarchical page tables, address translation, and page replacement with eviction and restoration logic. It mimics how an operating system handles virtual to physical memory mapping.

## Project Structure

- `VirtualMemory.cpp` – (User implementation) Contains the logic for virtual memory management, address translation, and memory access.
- `PhysicalMemory.cpp` – Simulates physical RAM and a swap file. Handles low-level read/write/evict/restore operations.
- `MemoryConstants.h` – Defines all memory-related constants like address widths, page size, frame count, and helper macros.

## Memory Model

- **Virtual Address Width**: 20 bits → 1M words
- **Physical Address Width**: 10 bits → 1K words (divided into 64 frames of 16 words each)
- **Page Size**: 16 words (OFFSET_WIDTH = 4)
- **Hierarchical Tables Depth**: `ceil((VIRTUAL_ADDRESS_WIDTH - OFFSET_WIDTH) / OFFSET_WIDTH)`
- **Page Replacement Strategy**: Custom weight-based eviction with `WEIGHT_ODD` and `WEIGHT_EVEN`.

## API (PhysicalMemory)

These functions simulate the physical memory and must be used in your implementation in `VirtualMemory.cpp`:

```cpp
void PMread(uint64_t physicalAddress, word_t* value);
void PMwrite(uint64_t physicalAddress, word_t value);
void PMevict(uint64_t frameIndex, uint64_t evictedPageIndex);
void PMrestore(uint64_t frameIndex, uint64_t restoredPageIndex);
```

## Compilation

To compile your virtual memory project:

```bash
g++ -std=c++11 -Wall -Wextra PhysicalMemory.cpp VirtualMemory.cpp -o vm_sim
```

Then link any test file that uses the virtual memory API.

## Constants and Configuration

All major constants are defined in `MemoryConstants.h`:

```cpp
#define OFFSET_WIDTH 4
#define PHYSICAL_ADDRESS_WIDTH 10
#define VIRTUAL_ADDRESS_WIDTH 20
#define RAM_SIZE (1 << PHYSICAL_ADDRESS_WIDTH)
#define VIRTUAL_MEMORY_SIZE (1 << VIRTUAL_ADDRESS_WIDTH)
#define PAGE_SIZE (1 << OFFSET_WIDTH)
#define NUM_FRAMES (RAM_SIZE / PAGE_SIZE)
#define NUM_PAGES (VIRTUAL_MEMORY_SIZE / PAGE_SIZE)
```

You can modify `OFFSET_WIDTH`, `VIRTUAL_ADDRESS_WIDTH`, or `PHYSICAL_ADDRESS_WIDTH` to simulate different systems.

## Utility Functions

For debugging, the simulation includes:

```cpp
void printRam();           // Print RAM content
void printEvictionCounter(); // Print number of evictions that occurred
```

## Eviction Policy

The eviction algorithm uses a custom weight function based on the parity of values in pages:

- Even values contribute `WEIGHT_EVEN`
- Odd values contribute `WEIGHT_ODD`
- The page with the lowest score is evicted

Weights can be configured via:

```cpp
#define WEIGHT_EVEN 4
#define WEIGHT_ODD 2
```

## Usage Example

You must implement a function like `VMread` or `VMwrite` in `VirtualMemory.cpp` that uses the physical memory API.

Example logic (pseudo):

```cpp
void VMread(uint64_t virtualAddress, word_t* value) {
    // Translate virtual → physical
    // Use PMread() on the result
}
```

## Notes

- The physical memory is simulated using `std::vector`, and swapped-out pages are stored in a `std::unordered_map`.
- Page restoration (`PMrestore`) does nothing if page is accessed for the first time.
- Asserts are used to detect illegal access.

## License

This simulation is part of the Hebrew University OS course. Provided for educational use only.
