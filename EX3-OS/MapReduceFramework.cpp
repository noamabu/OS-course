//
// Created by ybarak on 27/06/2024.
//
#include "MapReduceFramework.h"
#include <cstdlib>
#include <cstdio>
#include <atomic>
#include <list>
#include <algorithm>
#include <pthread.h>
#include <unistd.h>

struct JobContext;
struct ThreadContext;

// helper functions
void *threadRun(void *_arg);

unsigned long getInputPairIndex(ThreadContext *threadContext);

void processInputPair(ThreadContext *threadContext, unsigned long index);

void reducePair(ThreadContext *threadContext, IntermediateVec curPair);

void executeShuffleOperation(JobContext *jobContext);

void sortIntermediatePairsByKeys(ThreadContext *threadCtx);

void DestroyMutex(int checkReturnValue);

void InitReduceStage(JobContext *jobContext);

void waitFirstThread(JobContext *curJob);

/**
    a multiple use barrier
 */
class Barrier {
public:
    explicit Barrier(int numThreads);

    void barrier(JobContext *jobContext);

    ~Barrier();


private:
    pthread_mutex_t mutex;
    pthread_cond_t cv;
    int count;
    int numThreads;
};

/**
 *  job information
 */
struct JobContext {
    std::vector<std::vector<IntermediatePair>> shuffleArray;
    std::atomic<uint64_t> *counterAtomic;
    int multiThreadLevel;
    int waitFlag;
    unsigned long maxSize;
    const InputVec *inputVec;
    const MapReduceClient *mapReduceClient;
    Barrier *barrier;
    ThreadContext *threadContexts;
    JobState jobState;
    OutputVec *outputVec;
    pthread_t *threadHandles;
    pthread_mutex_t barrierMutex;
    pthread_mutex_t vectorMutex;
    pthread_mutex_t counterMutex;
    pthread_mutex_t stageMutex;
    pthread_mutex_t waitMutex;
    pthread_cond_t conditionVar;
};

/**
 *  thread information
 */
struct ThreadContext {
    JobContext *jobContext;
    IntermediateVec intermediateVec;
};


Barrier::Barrier(int numThreads)
        : mutex(PTHREAD_MUTEX_INITIALIZER), cv(PTHREAD_COND_INITIALIZER), count(0), numThreads(numThreads) {}

Barrier::~Barrier() {
    if (pthread_mutex_destroy(&mutex) != 0) {
        fprintf(stdout, "system error: on pthread_mutex_destroy.\n");
        exit(1);
    }
    if (pthread_cond_destroy(&cv) != 0) {
        fprintf(stdout, "system error: on pthread_cond_destroy.\n");
        exit(1);
    }
}


void Barrier::barrier(JobContext *jobContext) {
    if (pthread_mutex_lock(&mutex) != 0) {
        fprintf(stdout, "system error: on pthread_mutex_lock.\n");
        exit(1);
    }
    if (++count < numThreads) {
        if (pthread_cond_wait(&cv, &mutex) != 0) {
            fprintf(stdout, "system error: on pthread_cond_wait.\n");
            exit(1);
        }
    } else {
        count = 0;
        executeShuffleOperation(jobContext);
        InitReduceStage(jobContext);

        if (pthread_cond_broadcast(&cv) != 0) {
            fprintf(stdout, "system error: on pthread_cond_broadcast.\n");
            exit(1);
        }
    }
    if (pthread_mutex_unlock(&mutex) != 0) {
        fprintf(stdout, "system error: on pthread_mutex_unlock.\n");
        exit(1);
    }
}


/**
 * Inserts a key-value pair into the intermediate array of the calling thread.
 * The operation is protected by a mutex to ensure thread safety.
 * @param key - the key part of the intermediate pair.
 * @param value - the value part of the intermediate pair.
 * @param context - the context structure of the calling thread, which contains the intermediate array.
 */
void emit2(K2 *key, V2 *value, void *context) {
    auto *threadContext = static_cast<ThreadContext *>(context);
    IntermediatePair intermediatePair = std::make_pair(key, value);
    if (pthread_mutex_lock(&threadContext->jobContext->vectorMutex) != 0) {
        fprintf(stdout, "system error: emit2 failed to lock vectorMutex before adding pair.\n");
        exit(EXIT_FAILURE);
    }
    threadContext->intermediateVec.push_back(intermediatePair);
    if (pthread_mutex_unlock(&threadContext->jobContext->vectorMutex) != 0) {
        fprintf(stdout, "system error: emit2 failed to unlock vectorMutex after adding pair.\n");
        exit(EXIT_FAILURE);
    }
}


/**
 * Adds a key-value pair to the output array of the thread that calls this function.
 * The operation is performed within a mutex lock to ensure thread safety.
 * @param key - the key component of the output pair.
 * @param value - the value component of the output pair.
 * @param context - the context of the calling thread, which includes the output array.
 */
void emit3(K3 *key, V3 *value, void *context) {
    auto *threadContext = static_cast<ThreadContext *>(context);
    OutputPair newOutputPair = std::make_pair(key, value);
    if (pthread_mutex_lock(&threadContext->jobContext->vectorMutex) != 0) {
        fprintf(stderr, "system error: emit3 failed to lock vectorMutex before adding output pair.\n");
        exit(EXIT_FAILURE);
    }
    threadContext->jobContext->outputVec->push_back(newOutputPair);
    if (pthread_mutex_unlock(&threadContext->jobContext->vectorMutex) != 0) {
        fprintf(stderr, "system error: emit3 failed to unlock vectorMutex after adding output pair.\n");
        exit(EXIT_FAILURE);
    }
}


/**
 * Starts a new map-reduce job with the specified parameters.
 * @param client - the client object that contains the map and reduce functions.
 * @param inputVec - the vector that holds the input key-value pairs.
 * @param outputVec - the vector that will hold the output key-value pairs.
 * @param multiThreadLevel - the number of threads to use for the job
 * @return - a JobHandle that represents the job and its state.
 */
JobHandle startMapReduceJob(const MapReduceClient &client,
                            const InputVec &inputVec, OutputVec &outputVec,
                            int multiThreadLevel) {
    // Allocate resources for job context
    auto *barrier = new Barrier(multiThreadLevel);
    auto *threads = new pthread_t[multiThreadLevel];
    auto *atomicCounter = new std::atomic<uint64_t>(0);
    auto *threadContexts = new ThreadContext[multiThreadLevel];
    auto *jobContext = new JobContext;

    // Initialize thread contexts
    for (int i = 0; i < multiThreadLevel; ++i) {
        threadContexts[i] = {jobContext};
    }

    // Set job context fields
    jobContext->threadContexts = threadContexts;
    jobContext->multiThreadLevel = multiThreadLevel;
    jobContext->barrier = barrier;
    jobContext->counterAtomic = atomicCounter;
    jobContext->inputVec = &inputVec;
    jobContext->outputVec = &outputVec;
    jobContext->mapReduceClient = &client;
    jobContext->threadHandles = threads;
    jobContext->waitFlag = 0;
    jobContext->conditionVar = PTHREAD_COND_INITIALIZER;
    jobContext->barrierMutex = PTHREAD_MUTEX_INITIALIZER;
    jobContext->vectorMutex = PTHREAD_MUTEX_INITIALIZER;
    jobContext->stageMutex = PTHREAD_MUTEX_INITIALIZER;
    jobContext->waitMutex = PTHREAD_MUTEX_INITIALIZER;
    jobContext->counterMutex = PTHREAD_MUTEX_INITIALIZER;
    jobContext->maxSize = inputVec.size();
    jobContext->jobState = {UNDEFINED_STAGE, 0};

    // Create threads
    for (int i = 0; i < multiThreadLevel; ++i) {
        if (pthread_create(&threads[i], nullptr, threadRun, &threadContexts[i]) != 0){
            fprintf(stdout, "system error: Unable to create a new thread.\n");
            exit(EXIT_FAILURE);
        }
    }

    return jobContext;
}


void executeMapping(ThreadContext *threadContext) {
    threadContext->jobContext->jobState.stage = MAP_STAGE;
    unsigned long inputIndex;
    unsigned long inputSize = threadContext->jobContext->inputVec->size();
    while (true) {
        inputIndex = getInputPairIndex(threadContext);
        if (inputIndex >= inputSize) {
            break;
        }
        processInputPair(threadContext, inputIndex);
    }
}

void executeReduce(ThreadContext *threadContext) {
    unsigned long OutputPairIndex = 0;
    while (OutputPairIndex < threadContext->jobContext->shuffleArray.size()) {
        OutputPairIndex = getInputPairIndex(threadContext);
        if (OutputPairIndex >= threadContext->jobContext->shuffleArray.size()) {
            break;
        }
        std::vector<std::vector<IntermediatePair>> cur_vec = threadContext->jobContext->shuffleArray;
        const IntermediateVec cur_pairs_vec = threadContext->jobContext->shuffleArray.at(OutputPairIndex);
        reducePair(threadContext, cur_pairs_vec);
    }
}

/**
 * The main function that runs the map-reduce job on a thread.
 * @param _arg - Pointer to the ThreadContext structure associated with the thread.
 */
void *threadRun(void *_arg) {
    auto *threadContext = (ThreadContext *) _arg;

    executeMapping(threadContext);

    sortIntermediatePairsByKeys(threadContext);

    threadContext->jobContext->barrier->barrier(threadContext->jobContext);

    executeReduce(threadContext);
    return nullptr;
}

/**
 * Retrieves the next available index and increments the atomic counter.
 * @param threadContext - Pointer to the ThreadContext structure associated with the thread.
 * @return The next available index for the thread to process.
 */
unsigned long getInputPairIndex(ThreadContext *threadContext) {
    unsigned long mask = (1 << 31) - 1;
    unsigned long curValue = (threadContext->jobContext->counterAtomic->fetch_add(1,
                                                                                  std::memory_order_relaxed));
    unsigned long nextValueIndex = curValue & mask;
    return nextValueIndex;
}


void atomicCounterHandler(ThreadContext *threadContext) {
    if (pthread_mutex_lock(&threadContext->jobContext->counterMutex) != 0) {
        fprintf(stdout, "system error: Unable to lock counterMutex .\n");
        exit(EXIT_FAILURE);
    }

    unsigned long incrementValue = 0x80000000;
    threadContext->jobContext->counterAtomic->fetch_add(incrementValue, std::memory_order_relaxed);

    unsigned long counterValue = threadContext->jobContext->counterAtomic->load(std::memory_order_relaxed);
    unsigned long processedPairs = (counterValue >> 31) & 0x7fffffff;

    float completionRate =
            (static_cast<float>(processedPairs) / static_cast<float>(threadContext->jobContext->maxSize)) *
            100.0f;
    threadContext->jobContext->jobState.percentage = completionRate;

    if (pthread_mutex_unlock(&threadContext->jobContext->counterMutex) != 0) {
        fprintf(stdout, "system error: Unable to unlock counterMutex.\n");
        exit(EXIT_FAILURE);
    }

}

/**
 * Processes the next pair of key-value inputs and sends them to the map function.
 * @param threadContext
 * @param i - The index of the pair to process.
 */
void processInputPair(ThreadContext *threadContext, unsigned long i) {
    (*(threadContext->jobContext->mapReduceClient)).map((threadContext->jobContext->inputVec)->at(i).first,
                                                        (threadContext->jobContext->inputVec)->at(i).second,
                                                        threadContext);
    atomicCounterHandler(threadContext);
}

/**
 * Reduces the current pair of key-value inputs and sends them to the reduce function.
 * @param threadContext - The context of the thread that is processing the pair.
 * @param curPair - The current pair of key-value inputs to reduce.
 */
void reducePair(ThreadContext *threadContext, const IntermediateVec curPair) {
    (*(threadContext->jobContext->mapReduceClient)).reduce(&curPair, threadContext);
    atomicCounterHandler(threadContext);
}

void InitReduceStage(JobContext *jobContext) {
    jobContext->maxSize = jobContext->shuffleArray.size();
    jobContext->counterAtomic->store(0);
    jobContext->counterAtomic->fetch_add(0x8000000000000000);
    jobContext->counterAtomic->fetch_add(0x4000000000000000);
    jobContext->jobState = {REDUCE_STAGE, 0.0f};
}


/**
 *  Configures the environment for the shuffle stage of the job.
 * @param jobDetails - The JobContext structure
 */
void configureShuffleEnvironment(JobContext *jobDetails) {
    if (pthread_mutex_lock(&jobDetails->stageMutex) != 0) {
        fprintf(stdout, "system error: Failed to lock stage mutex at shuffle initialization.\n");
        exit(EXIT_FAILURE);
    }

    // Initialize the counter and set the upper bit to mark start of shuffle
    jobDetails->counterAtomic->store(0x8000000000000000);

    // Update the job state to indicate the shuffle stage has begun
    jobDetails->jobState.stage = SHUFFLE_STAGE;
    jobDetails->jobState.percentage = 0;

    // Compute the total number of key-value pairs across all threads
    unsigned long pairCount = 0;
    for (int idx = 0; idx < jobDetails->multiThreadLevel; idx++) {
        pairCount += (jobDetails->threadContexts + idx)->intermediateVec.size();
    }
    jobDetails->maxSize = pairCount;

    // Adjust the counter to include the pair count at a specific bit position
    jobDetails->counterAtomic->fetch_add(pairCount << 31);

    if (pthread_mutex_unlock(&jobDetails->stageMutex) != 0) {
        fprintf(stdout, "system error: Failed to unlock stage mutex after shuffle setup.\n");
        exit(EXIT_FAILURE);
    }
}


/**
 * Helper function to find the largest key in the current intermediate vectors.
 * This version uses a direct reference to keep track of the largest pair, simplifying the logic.
 * @param jobContext - Contains all thread contexts and their vectors.
 * @return The largest intermediate pair found across all threads.
 */
IntermediatePair findLargestKey(JobContext *jobContext) {
    IntermediatePair *largestPair = nullptr;

    for (int i = 0; i < jobContext->multiThreadLevel; ++i) {
        IntermediateVec &vec = (jobContext->threadContexts + i)->intermediateVec;
        if (!vec.empty() && (!largestPair || *largestPair->first < *vec.back().first)) {
            largestPair = &vec.back();
        }
    }

    return largestPair ? *largestPair : IntermediatePair(nullptr,
                                                         nullptr); // Return an empty pair if none found
}

/**
 * Collects all pairs with the same key as the provided largest pair.
 * This version uses iterators for clarity and better control over vector manipulation.
 * @param jobContext - Contains all thread contexts and their vectors.
 * @param largestPair - The key to match against.
 * @return A vector of all pairs matching the largest key.
 */
std::vector<IntermediatePair>
collectPairsWithKey(JobContext *jobContext, const IntermediatePair &largestPair) {
    std::vector<IntermediatePair> collectedPairs;

    for (int i = 0; i < jobContext->multiThreadLevel; ++i) {
        IntermediateVec &vec = jobContext->threadContexts[i].intermediateVec;

        // Use a reverse iterator to efficiently pop from the back of the vector
        for (auto rit = vec.rbegin(); rit != vec.rend();) {
            if (!(*rit->first < *largestPair.first) && !(*largestPair.first < *rit->first)) {
                collectedPairs.push_back(*rit);
                rit = std::vector<IntermediatePair>::reverse_iterator(
                        vec.erase((++rit).base())); // Erase and move iterator back to the next element
            } else {
                break; // Since we're assuming vec is sorted, or we are looking for the largest key until mismatch
            }
        }
    }

    return collectedPairs;
}


/**
 * Updates the shuffle progress in the job context based on current state.
 * @param jobContext  - The context of the job containing all thread contexts and vectors.
 */
void updateShuffleProgress(JobContext *jobContext) {
    jobContext->jobState.percentage = static_cast<float>(jobContext->shuffleArray.size())
                                      / jobContext->maxSize * 100.0f;
}

/**
 * Executes the shuffle operation, organizing data into shuffled arrays by key.
 * @param jobContext - The context of the job containing all thread contexts and vectors.
 */
void executeShuffleOperation(JobContext *jobContext) {
    configureShuffleEnvironment(jobContext);

    while (jobContext->jobState.percentage < 100) {
        IntermediatePair largestPair = findLargestKey(jobContext);
        if (!largestPair.first) { // No more pairs available, stop the shuffle
            break;
        }

        std::vector<IntermediatePair> matchedPairs = collectPairsWithKey(jobContext, largestPair);
        if (!matchedPairs.empty()) {
            jobContext->shuffleArray.push_back(matchedPairs);
            updateShuffleProgress(jobContext);
        }
    }
}


/**
 * Sorts the intermediate vector for the current thread based on keys
 * @param threadCtx - ThreadContext structure associated with the thread
 */
void sortIntermediatePairsByKeys(ThreadContext *threadCtx) {
    std::sort(threadCtx->intermediateVec.begin(),
              threadCtx->intermediateVec.end(),
              [](const IntermediatePair &a, const IntermediatePair &b) {
                  return *(a.first) < *(b.first);
              });
}


/**
 * Waits for the job to complete based on the specified waitFlag.
 *
 * The behavior of this function is determined by the waitFlag in the job:
 * - 0: Waits for all threads associated with the job to complete (joins all thread handles).
 * - 1: Waits for a specific already waiting thread.
 * - 2: The job has already finished, so no waiting is necessary.
 *
 * @param job A JobHandle that holds all the information related to this job.
 */
void waitForJob(JobHandle job) {
    JobContext *curJob = ((JobContext *) job);
    if (curJob->waitFlag == 2) {
        return;
    }
    if (pthread_mutex_lock(&curJob->waitMutex) != 0) {
        fprintf(stdout, "system error: Failed to lock stage mutex at shuffle initialization.\n");
        exit(EXIT_FAILURE);
    }
    if (curJob->waitFlag == 0) {
        waitFirstThread(curJob);
    }
    if (curJob->waitFlag == 1) {
        if (pthread_cond_wait(&curJob->conditionVar, &curJob->barrierMutex) != 0) {
            fprintf(stderr, "system error: on pthread_cond_wait.\n");
            exit(EXIT_FAILURE);
        }
    }
}


/**
 * Waits for all threads associated with the job to complete and updates the waitFlag accordingly.
 * @param curJob A pointer to the JobContext structure representing the current job.
 */
void waitFirstThread(JobContext *curJob) {
    curJob->waitFlag = 1;
    if (pthread_mutex_unlock(&curJob->waitMutex) != 0) {
        fprintf(stdout, "system error: Failed to unlock stage mutex after shuffle setup.\n");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < curJob->multiThreadLevel; i++) {
        pthread_join(curJob->threadHandles[i], NULL);
    }
    if (pthread_cond_broadcast(&curJob->conditionVar) != 0) {
        fprintf(stdout, "system error: on pthread_cond_broadcast.\n");
        exit(EXIT_FAILURE);
    }
    curJob->waitFlag = 2;
}

/**
 * Retrieve the JobState from the struct JobContext.
 * @param job - struct that holds all the information in this job.
 * @param state - JobState to insert the job state.
 */
void getJobState(JobHandle job, JobState *state) {
    auto *curJob = (JobContext *) job;
    if (pthread_mutex_lock(&curJob->stageMutex) != 0) {
        fprintf(stdout, "system error: on pthread_mutex_lock.\n");
        exit(EXIT_FAILURE);
    }
    *state = curJob->jobState;
    if (pthread_mutex_unlock(&curJob->stageMutex) != 0) {
        fprintf(stdout, "system error: on pthread_mutex_unlock.\n");
        exit(EXIT_FAILURE);
    }
}

/**
 * Close the job handle and free all the resources.
 */
void closeJobHandle(JobHandle job) {
    waitForJob(job);
    JobContext *curJob = ((JobContext *) job);
    delete curJob->counterAtomic;
    delete curJob->barrier;
    delete[] curJob->threadHandles;
    delete[] curJob->threadContexts;
    if (pthread_mutex_destroy(&curJob->counterMutex) != 0) {
        fprintf(stdout, "system error: on pthread_mutex/cond_destroy.\n");
        exit(EXIT_FAILURE);
    }
    if (pthread_mutex_destroy(&curJob->stageMutex) != 0) {
        fprintf(stdout, "system error: on pthread_mutex/cond_destroy.\n");
        exit(EXIT_FAILURE);
    }
    if (pthread_cond_destroy(&curJob->conditionVar) != 0) {
        fprintf(stdout, "system error: on pthread_mutex/cond_destroy.\n");
        exit(EXIT_FAILURE);
    }
    if (pthread_mutex_destroy(&curJob->vectorMutex) != 0) {
        fprintf(stdout, "system error: on pthread_mutex/cond_destroy.\n");
        exit(EXIT_FAILURE);
    }
    if (pthread_mutex_destroy(&curJob->barrierMutex) != 0) {
        fprintf(stdout, "system error: on pthread_mutex/cond_destroy.\n");
        exit(EXIT_FAILURE);
    }
    delete curJob;
}
