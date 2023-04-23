#define _POSIX_SOURCE
#define _BSD_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/mman.h>
#include <string.h>

#define NUM_SELLERS 3
#define NUM_BUYERS 3
#define SHM_SIZE sizeof(int)

void seller(int id);
void customer(int id);

volatile sig_atomic_t stop = 0;

void sigterm_handler(int sig) {
    stop = 1;
}

int shared_memories[NUM_SELLERS];
sem_t *customer_queues_semaphores[NUM_SELLERS];
sem_t *seller_finished_semaphores[NUM_SELLERS];
char *customer_queues_name[] = {"/customer_queues_1", "/customer_queues_2", "/customer_queues_3"};
char *seller_finished_name[] = {"/seller_finished_1", "/seller_finished_2", "/seller_finished_3"};

int main() {
    signal(SIGINT, sigterm_handler);

    char shm_name[16] = "shm_name_";
    pid_t seller_pids[NUM_SELLERS];
    pid_t customer_pids[NUM_BUYERS];

    for (int i = 0; i < NUM_SELLERS; i++) {
        customer_queues_semaphores[i] = sem_open(customer_queues_name[i], O_CREAT, 0666, 1);
        seller_finished_semaphores[i] = sem_open(seller_finished_name[i], O_CREAT, 0666, 0);

        shm_name[strlen(shm_name)] = (char)(i + 65);
        shared_memories[i] = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
        if (shared_memories[i] == -1) {
            exit(-1);
        }
        if (ftruncate(shared_memories[i], SHM_SIZE) == -1) {
            exit(-1);
        }
        char* ptr = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shared_memories[i], 0);
        int value = *((int*)ptr);
        *((int*)ptr) = -1;
    }

    for (int i = 0; i < NUM_SELLERS; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            seller(i);
            exit(0);
        } else {
            seller_pids[i] = pid;
        }
    }
    usleep(100000);  // Приостанавливаем выполнение процесса на 0.1 секунду чтобы просто семафоры переключились

    for (int i = 0; i < NUM_BUYERS; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            customer(i);
            exit(0);
        } else {
            customer_pids[i] = pid;
        }
        usleep(10000);  // Приостанавливаем выполнение процесса на 0.01 секунду чтобы просто семафоры переключились
    }

    for (int i = 0; i < NUM_BUYERS; i++) {
        waitpid(customer_pids[i], NULL, 0);
    }

    // Отправляем сигнал SIGINT всем seller процессам
    for (int i = 0; i < NUM_SELLERS; i++) {
        kill(seller_pids[i], SIGINT);
    }

    printf("\n\n");
    for (int i = 0; i < NUM_SELLERS; i++) {
        sem_close(customer_queues_semaphores[i]);
        sem_close(seller_finished_semaphores[i]);
        sem_unlink(customer_queues_name[i]);
        sem_unlink(seller_finished_name[i]);

        shm_name[strlen(shm_name)] = (char)(i + 65);
        shm_unlink(shm_name);

        printf("All deleted in %d\n", i);
    }
    printf("Parent stopped!\n");

    return 0;
}


void seller(int id) {
    printf("Seller %d started working\n", id);

    char* ptr = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shared_memories[id], 0);
    int value = *((int*)ptr);
    *((int*)ptr) = -1;

    while (!stop) {
        sem_wait(seller_finished_semaphores[id]);
        value = *((int*)ptr);
        if (value != -1) {
            printf("Seller %d started with customer %d\n", id, value);
            fflush(NULL);
            sleep(1);
            sem_post(seller_finished_semaphores[id]);
            *((int*)ptr) = -1;
        }
        usleep(10000);  // Приостанавливаем выполнение процесса на 0.01 секунду чтобы просто семафоры переключились
    }

    if (munmap(ptr, SHM_SIZE) == -1) {
        exit(-1);
    }
}


void customer(int id) {
    srand(time(NULL) + id);

    for (int i = 0; i < NUM_SELLERS; ++i) {
        if (stop) {
            printf("Customer %d stopped\n", id);
            return;
        }

        int seller_id = rand() % NUM_SELLERS;
        char* ptr = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shared_memories[seller_id], 0);

        int tmp;

        printf("Customer %d in queue to seller %d\n", id, seller_id);
        fflush(NULL);
        sem_wait(customer_queues_semaphores[seller_id]);
        printf("Customer %d send data to seller\n", id);
        fflush(NULL);
        *((int*)ptr) = id;
        sem_post(seller_finished_semaphores[seller_id]);
        usleep(10000);  // Приостанавливаем выполнение процесса на 0.01 секунду просто для переключения
        printf("Customer %d wait for seller %d\n", id, seller_id);
        sem_wait(seller_finished_semaphores[seller_id]);
        printf("Customer %d finished with seller %d\n", id, seller_id);
        fflush(NULL);
        sem_post(customer_queues_semaphores[seller_id]);

        if (munmap(ptr, SHM_SIZE) == -1) {
            exit(-1);
        }
        sleep(1);
    }
    printf("Customer %d finished\n", id);
}
