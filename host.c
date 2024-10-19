#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>

#define PORT 12345            // Port number to be used by the server
#define CHUNK_SIZE 10000      // Size of each file chunk to be sent

typedef struct {
    int sockfd;
    struct sockaddr_in client_addr;
    FILE *file;
    long file_size;
    int total_chunks;
    int *acknowledged_chunks;
} thread_data_t;

typedef struct {
    struct sockaddr_in client_addr;
    bool active;
} client_info_t;

typedef struct {
    int sockfd;
    struct sockaddr_in client_addr;
    int *acknowledged_chunks;
    int total_chunks;
} ack_thread_data_t;

bool client_exists(client_info_t clients[], struct sockaddr_in *client_addr, int max_clients) {
    for (int i = 0; i < max_clients; i++) {
        if (clients[i].active && clients[i].client_addr.sin_addr.s_addr == client_addr->sin_addr.s_addr && clients[i].client_addr.sin_port == client_addr->sin_port) {
            return true;
        }
    }
    return false;
}

void *listen_for_acks(void *arg) {
    ack_thread_data_t *data = (ack_thread_data_t *)arg;
    char buffer[CHUNK_SIZE];
    socklen_t addr_len = sizeof(data->client_addr);

    while (1) {
        int n = recvfrom(data->sockfd, buffer, sizeof(int), 0, (struct sockaddr *)&data->client_addr, &addr_len);
        int seq_num_recvd = *(int *)buffer;
        printf("Received seq ack: %d\n", seq_num_recvd);
        if (seq_num_recvd < data->total_chunks) {
            data->acknowledged_chunks[seq_num_recvd] = 1;
        }
    }

    pthread_exit(NULL);
}



// Function to send a file chunk with sequence number
void send_file_chunk_with_seq(FILE *file, struct sockaddr_in *client_addr, int sockfd, int seq_num) {
    char buffer[CHUNK_SIZE + sizeof(int)];
    
    // Copy the sequence number to the buffer
    memcpy(buffer, &seq_num, sizeof(int));
    
    // Read a chunk of the file into the buffer
    int bytes_read = fread(buffer + sizeof(int), 1, CHUNK_SIZE, file);
    
    // Send the chunk with the sequence number to the client
    sendto(sockfd, buffer, sizeof(int) + bytes_read, 0, (struct sockaddr *)client_addr, sizeof(*client_addr));
}

void *handle_client(void *arg) {
    thread_data_t *data = (thread_data_t *)arg;
    char buffer[CHUNK_SIZE];
    socklen_t addr_len = sizeof(data->client_addr);
    int seq_num = 0;
    bool finished = false;
    bool first_time = true;

    // Create the acknowledgment listening thread
    ack_thread_data_t *ack_data = (ack_thread_data_t *)malloc(sizeof(ack_thread_data_t));
    ack_data->sockfd = data->sockfd;
    ack_data->client_addr = data->client_addr;
    ack_data->acknowledged_chunks = data->acknowledged_chunks;
    ack_data->total_chunks = data->total_chunks;

    pthread_t ack_tid;
    pthread_create(&ack_tid, NULL, listen_for_acks, (void *)ack_data);

    while (seq_num <= data->total_chunks) {
        send_file_chunk_with_seq(data->file, &data->client_addr, data->sockfd, seq_num);
        seq_num++;
    }

    //usleep(500 * 1000);

    printf("Acknowledged Chunks:\n");
    for (int i = 0; i < data->total_chunks; i++) {
        printf("%d ", data->acknowledged_chunks[i]);
    }

    //send remaining chunks
    seq_num = 0;
    while (!finished) {
        if(seq_num == 0) finished = true;
        for (int i = seq_num; i < data->total_chunks; i++) {
            if (data->acknowledged_chunks[i] == 0) {
                finished = false;
                seq_num = i;
                break;
            }
        }
        send_file_chunk_with_seq(data->file, &data->client_addr, data->sockfd, seq_num);

        if(seq_num == data->total_chunks -1){
            seq_num = 0;
        }
        
    }

    pthread_exit(NULL);
}

void print_sockaddr_in(struct sockaddr_in *addr) {
    char ip[INET_ADDRSTRLEN];
    
    // Convert the IP address from binary to text form
    inet_ntop(AF_INET, &(addr->sin_addr), ip, INET_ADDRSTRLEN);
    
    // Print the elements of sockaddr_in
    printf("Family: %d\n", addr->sin_family);
    printf("Port: %d\n", ntohs(addr->sin_port)); // Convert port from network byte order to host byte order
    printf("IP Address: %s\n", ip);
}

int main() {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[CHUNK_SIZE];
    const int MAX_CLIENTS = 10; // Define the maximum number of clients
    client_info_t clients[MAX_CLIENTS] = {}; // Array to store client info

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    FILE *file = fopen("largefile.mkv", "rb");
    if (!file) {
        perror("File open failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    int total_chunks = (file_size + CHUNK_SIZE - 1) / CHUNK_SIZE;
    int *acknowledged_chunks = (int *)calloc(total_chunks, sizeof(int));
    if (!acknowledged_chunks) {
        perror("Memory allocation failed");
        fclose(file);
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < total_chunks; i++) {
        acknowledged_chunks[i] = 0;
    }

    printf("Must send %d chunks: \n", total_chunks);


    while (1) {
        int n = recvfrom(sockfd, buffer, CHUNK_SIZE, 0, (struct sockaddr *)&client_addr, &addr_len);
        if (n > 0) {
            if (!client_exists(clients, &client_addr, MAX_CLIENTS)) {
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (!clients[i].active) {
                        clients[i].client_addr = client_addr;
                        clients[i].active = true;

                        thread_data_t *data = (thread_data_t *)malloc(sizeof(thread_data_t));
                        data->sockfd = sockfd;
                        data->client_addr = client_addr;
                        data->file = file;
                        data->file_size = file_size;
                        data->total_chunks = total_chunks;
                        data->acknowledged_chunks = acknowledged_chunks;

                        printf("Creating a thread for:\n");
                        print_sockaddr_in(&data->client_addr);

                        pthread_t tid;
                        pthread_create(&tid, NULL, handle_client, (void *)data);
                        pthread_detach(tid);
                        break;
                    }
                }
            }
        }
    }

    fclose(file);
    close(sockfd);
    return 0;
}
