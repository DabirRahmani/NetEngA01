#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>

#define SERVER_PORT 12345      // Port number the server is using
#define SERVER_IP "127.0.0.1"  // IP address of the server
#define CHUNK_SIZE 10000       // Size of each chunk of file to be received
#define NUM_THREADS 8          // Number of threads to be used for parallel processing

// Struct to hold thread data
typedef struct {
    int thread_id;                    // Thread identifier
    int sockfd;                       // Socket file descriptor
    struct sockaddr_in server_addr;   // Server address structure
    FILE *file;                       // File pointer to the output file
} thread_data_t;

// Function for threads to receive file chunks
void *receive_file_chunk(void *arg) {
    thread_data_t *data = (thread_data_t *)arg; // Cast the argument to thread_data_t
    char buffer[CHUNK_SIZE + sizeof(int)];      // Buffer to hold received data plus sequence number
    int bytes_received;                         // Number of bytes received
    int seq_num;                                // Sequence number

    while (1) {
        // Receive a chunk from the server
        bytes_received = recvfrom(data->sockfd, buffer, CHUNK_SIZE + sizeof(int), 0, NULL, NULL);
        if (bytes_received > 0) {
            // Extract the sequence number from the buffer
            seq_num = *(int *)buffer;

            if(seq_num == -1) {
                printf("seq num = -1 recvd. closing TID %d", data->thread_id);
                break;
            }

            // Write the received chunk to the correct position in the file
            fseek(data->file, seq_num * CHUNK_SIZE, SEEK_SET);
            fwrite(buffer + sizeof(int), 1, bytes_received - sizeof(int), data->file);

            // Print details of the received chunk
            int chunk_length = bytes_received - sizeof(int);
            int position = seq_num * CHUNK_SIZE;
            printf("Chunk Length: %d, Seq: %d, Position: %d, TID: %d\n", chunk_length, seq_num, position, data->thread_id);

            // Send ACK for the received chunk
            sendto(data->sockfd, &seq_num, sizeof(int), 0, (struct sockaddr *)&data->server_addr, sizeof(data->server_addr));
        }
    }

    pthread_exit(NULL);  // Exit the thread
}

int main() {
    int sockfd;  // Socket file descriptor
    struct sockaddr_in server_addr;  // Server address structure
    pthread_t threads[NUM_THREADS];  // Array of thread identifiers
    thread_data_t thread_data[NUM_THREADS];  // Array of thread data structures
    FILE *file = fopen("received_largefile.mkv", "wb");  // Open the output file for writing

    if (!file) {
        perror("File open failed");
        exit(EXIT_FAILURE);
    }

    // Create a UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        fclose(file);
        exit(EXIT_FAILURE);
    }

    // Set up the server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;            // IPv4
    server_addr.sin_port = htons(SERVER_PORT);   // Server port
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);  // Server IP address

    // Send a request to the server to start the file transfer
    const char *request = "REQUEST_FILE";
    sendto(sockfd, request, strlen(request), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));

    // Create threads to receive file chunks
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_data[i].thread_id = i;                  // Set the thread ID
        thread_data[i].sockfd = sockfd;                // Set the socket file descriptor
        thread_data[i].server_addr = server_addr;      // Set the server address
        thread_data[i].file = file;                    // Set the file pointer
        pthread_create(&threads[i], NULL, receive_file_chunk, (void *)&thread_data[i]);  // Create a thread
    }

    // Wait for all threads to finish
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    // Close the file and socket
    fclose(file);
    close(sockfd);

    return 0;
}
