# MapReduce Framework in C++

This project implements a multi-threaded MapReduce framework in C++, similar to the Hadoop model, using POSIX threads and custom synchronization primitives.

## Overview

The framework enables clients to define custom `map` and `reduce` logic and apply it over a collection of key-value pairs in parallel using multiple threads. It is composed of three phases:

1. **Map** – Processes input pairs and emits intermediate key-value pairs.
2. **Shuffle** – Groups all intermediate pairs by key.
3. **Reduce** – Processes grouped pairs and emits output pairs.

## API

To use the framework, implement the `MapReduceClient` interface:

```cpp
class MapReduceClient {
public:
    virtual void map(const K1* key, const V1* value, void* context) const = 0;
    virtual void reduce(const IntermediateVec* pairs, void* context) const = 0;
};
```

And use the following API from `MapReduceFramework.h`:

```cpp
JobHandle startMapReduceJob(const MapReduceClient& client,
                            const InputVec& inputVec,
                            OutputVec& outputVec,
                            int multiThreadLevel);

void waitForJob(JobHandle job);

void getJobState(JobHandle job, JobState* state);

void closeJobHandle(JobHandle job);

void emit2(K2* key, V2* value, void* context);

void emit3(K3* key, V3* value, void* context);
```

## Build Instructions

Compile all sources together:

```bash
g++ -std=c++11 -pthread MapReduceFramework.cpp -o mapreduce
```

Then, link your implementation file that includes your `MapReduceClient` subclass.

## Example Usage

```cpp
class MyClient : public MapReduceClient {
    void map(const K1* key, const V1* value, void* context) const override {
        // your map logic
        emit2(...);
    }

    void reduce(const IntermediateVec* pairs, void* context) const override {
        // your reduce logic
        emit3(...);
    }
};

int main() {
    InputVec input;   // Fill with (K1*, V1*) pairs
    OutputVec output;

    MyClient client;
    JobHandle job = startMapReduceJob(client, input, output, 4);
    waitForJob(job);
    closeJobHandle(job);
}
```

## Design Highlights

- **Threading:** Uses `pthread_create` to spawn worker threads.
- **Barrier:** Custom reusable barrier to synchronize at shuffle start.
- **Atomic Counter:** Tracks progress across threads using bit fields for stage + counters.
- **Mutex & Condition Variable:** Used to ensure thread-safe operations on shared vectors and state.
- **Shuffle Phase:** Merges all intermediate vectors into a single grouped structure by key.
- **Sorting:** Each thread sorts its intermediate pairs before the shuffle.

## Job State Tracking

You can retrieve job progress at any time via:

```cpp
JobState state;
getJobState(job, &state);
```

The `stage` will be one of:
- `UNDEFINED_STAGE`
- `MAP_STAGE`
- `SHUFFLE_STAGE`
- `REDUCE_STAGE`

And `percentage` will reflect completion percentage of the current stage.

## Limitations

- Memory allocations are handled manually — no smart pointers used.
- Assumes all `K1/K2/K3` implement `<` operator properly.
- No fault tolerance or recovery mechanisms.
- Not designed for distributed environments.

## License

This implementation is provided as part of the Hebrew University OS course.
