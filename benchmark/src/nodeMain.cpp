#include "defs.h"

#ifdef USE_OPEN_DHT
#include <opendht.h>
#endif

#include <iostream>
#include <pthread.h>

// Global variables are top-notch method of letting callback access variables
struct timespec opStartTime[MAX_OPERATION_CNT], opEndTime[MAX_OPERATION_CNT];
pthread_barrier_t syncBarrier;

// Useful data struct
typedef struct keyDataPair {
    std::string key;
    std::vector<u_int8_t> data;
} keyDataPair;

// Declare functions
dht::DhtRunner* initOpenDhtNode(char* bootstrapAddr, u_int16_t port);
long long calcDeltaT(struct timespec start, struct timespec end);
struct timespec incTime(struct timespec start, long long dt_ns);
void putCallback(int iterCnt, bool success);
bool getCallback(int iterCnt, const std::vector<std::shared_ptr<dht::Value>> &values);
int genKeyDataSet(int setSize, keyDataPair *keyDataSet);

int main(int argc, char* argv[]) {
    // Get basic node info
    uint8_t id = UINT8_MAX;
    if (argc >= 2) {
        id = atoi(argv[1]);
    }
    bool isBootstrapNode = false;
    if (id == 0) {
        isBootstrapNode = true;
    }

    // Local variables
    long long dtPerOp_ns;
    struct timespec startTime, nextOpTime, initTime, initDelayTime;
    int result, iterCnt;

    // Global variable init
    memset(opStartTime, 0x00, sizeof(struct timespec) * MAX_OPERATION_CNT);
    memset(opEndTime, 0x00, sizeof(struct timespec) * MAX_OPERATION_CNT);
    result = pthread_barrier_init(&syncBarrier, NULL, 2);

    // Get init time and set init delay
    result = clock_gettime(CLOCK_MONOTONIC, &initTime);
    if (result != 0) {
        std::cout << "ERROR: clock_gettime returned non-zero when setting start time." << std::endl;
        return -1;
    }
    initDelayTime = incTime(initTime, (long long)NODE_INIT_DELAY_S * (long long)1000000000);

    // Init node then wait for delay time. Any node that is NOT the bootstrap node should do bootstrapping
    dht::DhtRunner *node;
    if (isBootstrapNode) {
        std::cout << "Starting bootstrap node init..." << std::endl;
        node = initOpenDhtNode(nullptr, PORT);
    } else {
        std::cout << "Starting node init..." << std::endl;
        node = initOpenDhtNode(BOOTSTRAP_IP, PORT);
    }

    // Seed RNG and generate random data vectors
    std::cout << "Generating warmup and test data sets." << std::endl;
    srand(time(NULL));
    keyDataPair keyDataSet[MAX_OPERATION_CNT], warmupSet[WARMUP_OP_CNT];
    genKeyDataSet(MAX_OPERATION_CNT, keyDataSet);
    genKeyDataSet(WARMUP_OP_CNT, warmupSet);

    // Calc desired dt (nanoseconds) between each operation
    dtPerOp_ns = 1000000000 / NODE_OPS;

    // Syncronize nodes (roughly)
    if (isBootstrapNode) {
        std::cout << "Node init complete. Press enter when all nodes have compelted init..." << std::endl;
        std::cin >> result;
        node->put(INIT_SYNC_KEY, std::vector<u_int8_t>(1,2));
    } else {
        std::cout << "Node init complete. Waiting for sync from bootstrap node..." << std::endl;
        // Setup listener on SYNC_KEY, then wait on barrier
        node->listen(INIT_SYNC_KEY,
            [](const std::vector<std::shared_ptr<dht::Value>>& values) {
                pthread_barrier_wait(&syncBarrier);
                return false; // stop listening
        });
        result = pthread_barrier_wait(&syncBarrier);
    }

    // Sleep until delay time is reached
    //clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &initDelayTime, nullptr);

    // Set warmup start time and start warmup loop
    std::cout << "Setting start time and starting warmup..." << std::endl;
    result = clock_gettime(CLOCK_MONOTONIC, &startTime);
    if (result != 0) {
        std::cout << "ERROR: clock_gettime returned non-zero when setting start time." << std::endl;
        return -1;
    }
    iterCnt = 0;
    while (iterCnt < WARMUP_OP_CNT) {
        // Determine when to start next operation and wait till then
        nextOpTime = incTime(startTime, dtPerOp_ns * iterCnt);
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &nextOpTime, nullptr);

        // Start operation with appropriate callback
        node->put(warmupSet[iterCnt].key, warmupSet[iterCnt].data);

        iterCnt++;
    }

    // Syncronize nodes again after warmup (roughly)
    if (isBootstrapNode) {
        std::cout << "Warmup complete. Press enter to continue..." << std::endl;
        std::cin >> result;
        node->put(WARMUP_SYNC_KEY, std::vector<u_int8_t>(1,2));
    } else {
        std::cout << "Warmup complete. Waiting for sync from bootstrap node..." << std::endl;
        // Setup listener on SYNC_KEY, then wait on barrier
        node->listen(WARMUP_SYNC_KEY,
                     [](const std::vector<std::shared_ptr<dht::Value>>& values) {
                         pthread_barrier_wait(&syncBarrier);
                         return false; // stop listening
                     });
        result = pthread_barrier_wait(&syncBarrier);
    }

    // Reset start time and start test loop
    std::cout << "Setting start time and starting test..." << std::endl;
    result = clock_gettime(CLOCK_MONOTONIC, &startTime);
    if (result != 0) {
        std::cout << "ERROR: clock_gettime returned non-zero when setting start time." << std::endl;
        return -1;
    }
    iterCnt = 0;
    while (iterCnt < MAX_OPERATION_CNT) {
        // Determine when to start next operation and wait till then
        nextOpTime = incTime(startTime, dtPerOp_ns * iterCnt);
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &nextOpTime, nullptr);

        // Store current time as operation start
        result = clock_gettime(CLOCK_MONOTONIC, &opStartTime[iterCnt]);
        if (result != 0){
            std::cout << "ERROR: clock_gettime returned non-zero when setting operation start time." << std::endl;
            return -1;
        }

        // Start operation with appropriate callback
        node->put(keyDataSet[iterCnt].key, keyDataSet[iterCnt].data, std::bind(putCallback, iterCnt, std::placeholders::_1));

        iterCnt++;
    }

    /*
    for (int i = 0; i < 4; i++) {
        std::string key = "myKey" + std::to_string(i);
        node->get(key, [](const std::vector<std::shared_ptr<dht::Value>> &values) {
            for (const auto &value : values) {
                std::cout << "Found value:" << *value << std::endl;
            }
            return true; // Continue search. False to stop.
        });
    }
     */

    // Wait a bit for any remaining operations to complete
    std::cout << "Loop complete. Pausing for operations to complete..." << std::endl;
    struct timespec tv;
    tv.tv_nsec = 0;
    tv.tv_sec = 5;
    nanosleep(&tv, nullptr);

    // Results
    std::cout << "Cnt | Elapsed Time" << std::endl;
    int i, completedOpCnt = 0;
    long long dt, totalOpTime = 0;
    for (i = 0; i < MAX_OPERATION_CNT; i++) {
        // Verify end time was set
        if (opEndTime[i].tv_sec > 0) {
            dt = calcDeltaT(opStartTime[i], opEndTime[i]);
            totalOpTime += dt;
            completedOpCnt++;
            std::cout << std::to_string(i) << "\t" << std::to_string(dt) << std::endl;
        }
        else{
            std::cout << std::to_string(i) << "\t" << "----" << std::endl;
        }
    }
    std::cout << "Average: " << std::to_string(totalOpTime / completedOpCnt) << std::endl;

    // Shutdown
    std::cout << "Waiting for node to shutdown..." << std::endl;
    node->join();
    std::cout << "Node join complete" << std::endl;
    return 0;
}

dht::DhtRunner* initOpenDhtNode(char* bootstrapAddr, u_int16_t port) {
    dht::DhtRunner* node = new(dht::DhtRunner);

    node->run((in_port_t)port, dht::crypto::generateIdentity(), true);

    if (bootstrapAddr != nullptr) {
        node->bootstrap(bootstrapAddr, std::to_string(port).c_str());
    }

    return node;
}

long long calcDeltaT(struct timespec start, struct timespec end) {
    // This function assumes time elapsed (in nanoseconds) will not overflow long type
    long long dt_ns = 0, dt_s = 0;

    dt_s = end.tv_sec - start.tv_sec;
    dt_ns = end.tv_nsec - start.tv_nsec;
    dt_ns += dt_s * 1000000000;

    return dt_ns;
}

struct timespec incTime(struct timespec start, long long dt_ns) {
    // This function assumes nanoseconds will not overflow long long type
    struct timespec newTime;
    long long new_ns = 0, overflow_s = 0;

    new_ns = start.tv_nsec + dt_ns;
    if (new_ns >= 1000000000) {
        overflow_s = new_ns / 1000000000;
        new_ns = new_ns % 1000000000;
    }

    newTime.tv_sec = start.tv_sec + overflow_s;
    newTime.tv_nsec = new_ns;

    return newTime;
}

void putCallback(int iterCnt, bool success) {
    int result;
    result = clock_gettime(CLOCK_MONOTONIC, &opEndTime[iterCnt]);
    if (result != 0){
        std::cout << "ERROR: PUT CALLBACK: clock_gettime returned non-zero when setting start time." << std::endl;
    }
}

bool getCallback(int iterCnt, const std::vector<std::shared_ptr<dht::Value>> &values) {
    int result;
    result = clock_gettime(CLOCK_MONOTONIC, &opEndTime[iterCnt]);
    if (result != 0){
        std::cout << "ERROR: GET CALLBACK: clock_gettime returned non-zero when setting start time." << std::endl;
    }
}

int genKeyDataSet(int setSize, keyDataPair *keyDataSet) {
    int i, j, dataSize, keyIdx;
    u_int8_t val;

    for (i = 0; i < setSize; i++) {
        // gen data
        std::vector<u_int8_t> data;
        dataSize = rand() % MAX_DATA_LEN;
        for (j = 0; j < dataSize; j++) {
            val = rand() % UINT8_MAX;
            data.push_back(val);
        }

        // choose random key from valid key set
        std::string key;
        keyIdx = rand() & VALID_KEY_COUNT;
        key = validKeys[keyIdx];

        // Add pair to keyDataSet
        keyDataSet[i].key = key;
        keyDataSet[i].data = data;
    }

    return 0;
}