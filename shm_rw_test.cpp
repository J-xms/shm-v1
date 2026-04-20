#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

constexpr size_t MEMORY_SIZE = 1024 * 1024;
constexpr int DURATION_SECONDS = 120;
constexpr int OPS_PER_SECOND = 10;
constexpr size_t STRING_SIZE = 256;

volatile sig_atomic_t running = 1;

void signalHandler(int signum) {
    if (signum == SIGINT) {
        running = 0;
    }
}

class ShmHandler {
public:
    ShmHandler(const char* name) : fd(-1), addr(nullptr), name_(name), cleanup_(false) {}
    
    ~ShmHandler() {
        if (addr) munmap(addr, MEMORY_SIZE);
        if (fd != -1) close(fd);
        // if (cleanup_) shm_unlink(name_);
    }
    
    bool open(bool auto_cleanup = false) {
        cleanup_ = auto_cleanup;
        fd = ::shm_open(name_, O_RDWR, 0666);
        if (fd == -1) {
            std::cerr << "shm_open failed: " << strerror(errno) << std::endl;
            return false;
        }
        
        if (ftruncate(fd, MEMORY_SIZE) == -1) {
            std::cerr << "ftruncate failed: " << strerror(errno) << std::endl;
            close(fd);
            fd = -1;
            return false;
        }
        
        addr = mmap(nullptr, MEMORY_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (addr == MAP_FAILED) {
            std::cerr << "mmap failed: " << strerror(errno) << std::endl;
            close(fd);
            fd = -1;
            addr = nullptr;
            return false;
        }
        
        return true;
    }
    
    void* getAddr() const { return addr; }
    bool isRunning() const { return running; }
    
private:
    int fd;
    void* addr;
    const char* name_;
    bool cleanup_;
};

uint64_t getPhysicalAddress(void* vaddr) {
    uint64_t pagesize = sysconf(_SC_PAGESIZE);
    uint64_t virtual_addr = (uint64_t)vaddr;
    uint64_t page_start = virtual_addr & ~(pagesize - 1);
    
    std::ifstream pagemap("/proc/self/pagemap", std::ios::binary);
    if (!pagemap.is_open()) {
        return 0;
    }
    
    uint64_t pagemap_offset = (page_start / pagesize) * sizeof(uint64_t);
    pagemap.seekg(pagemap_offset);
    
    uint64_t entry;
    pagemap.read(reinterpret_cast<char*>(&entry), sizeof(entry));
    pagemap.close();
    
    if ((entry & (1ULL << 63)) == 0) {
        return 0;
    }
    
    uint64_t physical_frame = entry & ((1ULL << 55) - 1);
    return physical_frame * pagesize + (virtual_addr - page_start);
}

std::string generateRandomString(size_t length) {
    static const char charset[] = 
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::string result;
    result.reserve(length);
    
    for (size_t i = 0; i < length - 1; ++i) {
        result += charset[rand() % (sizeof(charset) - 1)];
    }
    result += '\0';
    return result;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <shared_memory_name>" << std::endl;
        std::cerr << "Example: " << argv[0] << " /test_shm" << std::endl;
        std::cerr << "Note: name should start with /" << std::endl;
        return 1;
    }
    
    signal(SIGINT, signalHandler);
    
    const char* shm_name = argv[1];
    
    srand(time(nullptr));
    
    ShmHandler shm(shm_name);
    if (!shm.open(true)) {
        return 1;
    }
    
    void* mapped_addr = shm.getAddr();
    
    std::cout << "Shared Memory opened via shm_open:" << std::endl;
    std::cout << "  Name: " << shm_name << std::endl;
    std::cout << "  Virtual Address: " << mapped_addr << std::endl;
    std::cout << "  Physical Address: 0x" << std::hex << getPhysicalAddress(mapped_addr) << std::dec << std::endl;
    std::cout << "  Size: " << MEMORY_SIZE << " bytes" << std::endl;
    std::cout << "\nStarting 120s read/write test..." << std::endl;
    std::cout << "Operations: " << OPS_PER_SECOND << " per second" << std::endl;
    std::cout << "Press Ctrl-C to stop and cleanup." << std::endl;
    std::cout << "-------------------------------------------" << std::endl;
    
    auto start_time = std::chrono::steady_clock::now();
    int total_ops = 0;
    
    while (shm.isRunning()) {
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count();
        
        if (elapsed >= DURATION_SECONDS) {
            break;
        }
        
        for (int i = 0; i < OPS_PER_SECOND; ++i) {
            if (!shm.isRunning()) break;
            
            size_t offset = (rand() % ((MEMORY_SIZE - STRING_SIZE) / STRING_SIZE)) * STRING_SIZE;
            char* target_addr = static_cast<char*>(mapped_addr) + offset;
            
            std::string random_str = generateRandomString(STRING_SIZE);
            
            memcpy(target_addr, random_str.c_str(), STRING_SIZE);
            
            char read_buffer[STRING_SIZE];
            memcpy(read_buffer, target_addr, STRING_SIZE);
            
            uint64_t virt_addr = reinterpret_cast<uint64_t>(target_addr);
            uint64_t phys_addr = getPhysicalAddress(target_addr);
            
            bool verify_ok = (memcmp(random_str.c_str(), read_buffer, STRING_SIZE) == 0);
            
            std::cout << "[" << elapsed << "s] Op #" << ++total_ops << std::endl;
            std::cout << "  Virtual Addr: 0x" << std::hex << virt_addr << std::dec << std::endl;
            std::cout << "  Physical Addr: 0x" << std::hex << phys_addr << std::dec << std::endl;
            std::cout << "  Write: " << random_str.substr(0, 32) << "..." << std::endl;
            std::cout << "  Read:  " << std::string(read_buffer, 32) << "..." << std::endl;
            std::cout << "  Verify: " << (verify_ok ? "OK" : "FAIL") << std::endl;
            
            std::this_thread::sleep_for(std::chrono::milliseconds(1000 / OPS_PER_SECOND));
        }
    }
    
    std::cout << "-------------------------------------------" << std::endl;
    if (!running) {
        std::cout << "Interrupted by user. Cleaning up..." << std::endl;
    } else {
        std::cout << "Test completed. Total operations: " << total_ops << std::endl;
    }
    
    return 0;
}
