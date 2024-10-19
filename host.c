#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdbool.h>

#define PORT 12345            // Port number to be used by the server
#define CHUNK_SIZE 10000      // Size of each file chunk to be sent

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

int main() {
    int sockfd;  // Socket file descriptor
    struct sockaddr_in server_addr, client_addr;  // Server and client address structures
    socklen_t addr_len = sizeof(client_addr);  // Length of the client address
    char buffer[CHUNK_SIZE];  // Buffer to hold incoming data

    // Create a UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Bind the socket to the specified port
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;  // IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY;  // Any incoming interface
    server_addr.sin_port = htons(PORT);  // Port number

    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Open the file to be sent
    FILE *file = fopen("largefile.mkv", "rb");
    if (!file) {
        perror("File open failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    // اینجا باید اول یه ارایه بولین بسازیم برای تعداد سکوينس ها
    // همه رو اول فالس میذاریم
    // Initialize a boolean array to track received sequence numbers
    fseek(file, 0, SEEK_END); // Move file pointer to the end
    long file_size = ftell(file); // Get the current position, which is the size
    fseek(file, 0, SEEK_SET); // Move file pointer back to the beginning
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

    int seq_num = 0;  // Sequence number for file chunks

    printf("Must send %d chunks: \n", total_chunks); // سایز فایل رو داره اشتباه میفرسته

    bool started = false;

    // Main loop to receive requests and send file chunks
    while (1) {
        // Receive a message from the client
        int n = recvfrom(sockfd, buffer, CHUNK_SIZE, 0, (struct sockaddr *)&client_addr, &addr_len);
        int seq_num_recvd = *(int *)buffer;

        if (seq_num_recvd >= 0) {

            if(started)
            {
                printf("Received seq ack: %d\n", seq_num_recvd);
                // اینجا به ازای هر سکوینسی که دریافت کنیم باید اون بولینش رو ترو کنیم 
                if (seq_num_recvd < total_chunks) {
                    acknowledged_chunks[seq_num_recvd] = 1;
                } else {
                    if(seq_num_recvd > total_chunks){
                        break;
                        // بریک کردیم اما ممکنه هنوز بعضی از چانک ها ارسال نشده باشن
                    } 
                }
            }

            // Send a chunk of the file with the current sequence number
            send_file_chunk_with_seq(file, &client_addr, sockfd, seq_num);
            seq_num++;
            started = true;
        }
    }

    seq_num = 0;
    bool finished = false;

    while (1) {

        // ارسال چانک هایی که جا موندن
        //check array and if its finished. break;
        finished = true;
        for (int i = 0; i < total_chunks; i++) {
            if(acknowledged_chunks[i] == 0) {
                finished = false;
                seq_num = i;
                break;
            }
        }

        if(finished){
            int endMessage = -1;
            sendto(sockfd, &endMessage, sizeof(int), 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
            break;
        }
        else{

            // الان مشخص شده که کدوم رو باید بفرستیم
            send_file_chunk_with_seq(file, &client_addr, sockfd, seq_num);
                            
            // Receive a message from the client
            printf("waiting for seq_num_recvd");
            int n = recvfrom(sockfd, buffer, CHUNK_SIZE, 0, (struct sockaddr *)&client_addr, &addr_len);
            int seq_num_recvd = *(int *)buffer;
            printf("seq_num_recvd %d \n", seq_num_recvd);

            if (seq_num_recvd < total_chunks) {
                acknowledged_chunks[seq_num_recvd] = 1;
            }
        }

    }

    // Close the file and socket
    fclose(file);
    close(sockfd);

    return 0;
}
