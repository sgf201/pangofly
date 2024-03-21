#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define SHM_SIZE 1000 // 共享内存的大小
#define SHM_NAME "/my_shared_memory" // 共享内存的名称
class my_string: std::string
{
    public:
        my_string(char* str)
        {
            data = str;
        }
        char * c_str()
        {
            return data;
        }
    private:
        char* data;
};

int main() {
    // 创建或打开共享内存对象
    // 创建子进程
    pid_t pid = fork();
    if (pid == -1) {
        std::cerr << "Failed to fork." << std::endl;
        return 1;
    }

    if (pid == 0) {
        // 子进程，访问共享内存并输出字符串
        //std::string received_message((char*)shared_memory);

        int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
        if (shm_fd == -1) {
            std::cerr << "Failed to create or open shared memory." << std::endl;
            return 1;
        }

        // 设置共享内存大小
        if (ftruncate(shm_fd, SHM_SIZE) == -1) {
            std::cerr << "Failed to set shared memory size." << std::endl;
            return 1;
        }

        // 将共享内存映射到指定地址
        void* shared_memory = mmap((void*)0x7f5c29100000, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (shared_memory == MAP_FAILED) {
            std::cerr << "Failed to map shared memory." << std::endl;
            return 1;
        }
        //std::string message_at((char*)shared_memory, 100);
        my_string * message_at =new (shared_memory)my_string((char*)shared_memory+50);
        std::string message = "Hello, shared memory!";
        std::strcpy((char*)message_at->c_str(), message.c_str());
        printf("string:%p\n",shared_memory);
        printf("string:%p\n",message_at);
        printf("string:%p\n",message_at->c_str());
        sleep(30);
        // 解除内存映射
        if (munmap(shared_memory, SHM_SIZE) == -1) {
            std::cerr << "Failed to unmap shared memory in child process." << std::endl;
            return 1;
        }
        //std::cout << "Child process received: " << received_message << std::endl;
    } else {
        sleep(1);
        // 创建或打开共享内存对象
        int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
        if (shm_fd == -1) {
            std::cerr << "Failed to create or open shared memory." << std::endl;
            return 1;
        }

        // 设置共享内存大小
        if (ftruncate(shm_fd, SHM_SIZE) == -1) {
            std::cerr << "Failed to set shared memory size." << std::endl;
            return 1;
        }

        // 将共享内存映射到指定地址
        void* shared_memory = mmap((void*)0x7f5c29100000, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (shared_memory == MAP_FAILED) {
            std::cerr << "Failed to map shared memory." << std::endl;
            return 1;
        }
        printf("string:%p\n",(my_string*)shared_memory);
        // 访问共享内存并输出字符串
        std::string * received_message =new (shared_memory)std::string();
        //printf("string:%p\n",received_message);
        //std::cout << "Parent process received: " << *received_message << std::endl;
        std::cout << "shared_memory received: " << ((my_string*)shared_memory)->c_str() << std::endl;
        // 解除内存映射并关闭共享内存对象
        if (munmap(shared_memory, SHM_SIZE) == -1) {
            std::cerr << "Failed to unmap shared memory in parent process." << std::endl;
            return 1;
        }
        if (shm_unlink(SHM_NAME) == -1) {
            std::cerr << "Failed to unlink shared memory." << std::endl;
            return 1;
        }
    }

    return 0;
}