#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>

#define PORT 12345            // Port number to be used by the server
#define CHUNK_SIZE 10000      // Size of each file chunk to be sent

int lock = 0; // 0 for opening main // 1 for opening handleClient
char pub_ip_str[INET_ADDRSTRLEN];
int pub_port;

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
    pthread_t *ack_tid;
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
    struct sockaddr_in recv_addr;
    socklen_t recv_len = sizeof(recv_addr);

    int wrongThreadForNTime = 0;
    while (1) {
        while(lock == 0) {}
        int n = recvfrom(data->sockfd, buffer, sizeof(int) , 0, (struct sockaddr *)&recv_addr, &recv_len);
        if(n <= 0 || wrongThreadForNTime > 100) {
            printf("no ack receivec after 10 sec. deleting thread\n");
            pthread_exit(NULL);
            return 0; ///
        }
        int newRequest = *(int *)buffer;
        if (newRequest == -1){
            // یعنی درخواست ارسال فایل داریم
            // یه لاک میذاریم و میریم تو خواب
            // اونور که کارش تموم بشه لاک رو بر میداره
            printf(" ************** send file request, changing lock *****************\n");
            lock = 0;
        } 
        else if (recv_addr.sin_addr.s_addr == data->client_addr.sin_addr.s_addr && recv_addr.sin_port == data->client_addr.sin_port) {
            int seq_num_recvd = *(int *)buffer;
            //printf("Received seq ack: %d\n", seq_num_recvd);
            if (seq_num_recvd < data->total_chunks) {
                data->acknowledged_chunks[seq_num_recvd] = 1;
            }
            wrongThreadForNTime = 0;
        } else{
            wrongThreadForNTime++;
        }
    }

    pthread_exit(NULL);
}

long long current_time_in_ms() {
    struct timespec spec;
    clock_gettime(CLOCK_REALTIME, &spec);
    return spec.tv_sec * 1000 + spec.tv_nsec / 1000000;
}

bool compare_time_in_ms(long long stored_time) {
    long long current_time = current_time_in_ms();
    return (current_time - stored_time) > 100; // Example: Check if more than 200 ms have passed
}

// Function to send a file chunk with sequence number
void send_file_chunk_with_seq(FILE *file, struct sockaddr_in *client_addr, int sockfd, int seq_num) {
    char buffer[CHUNK_SIZE + sizeof(int)];
    
    // Copy the sequence number to the buffer
    memcpy(buffer, &seq_num, sizeof(int));

    long position = seq_num * CHUNK_SIZE;

    fseek(file, position, SEEK_SET);
    
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

    //send_file_chunk_with_seq(data->file, &data->client_addr, data->sockfd, chunkNumber);

    int sendingChunks[8] = { -1,-1,-1,-1,-1,-1,-1,-1}; // چانکهایی که ارسال شدن و منتظر جوابیم
    int mustSentChunks[8] = { -1,-1,-1,-1,-1,-1,-1,-1}; //چانک هایی که باید ارسال بشن
    int sentUntil = 0;

    long long times[8] = { 0,0,0,0,0,0,0,0};

    int iiii= 0;
    while(true)
    {
        // بررسی شود که چه چانک هایی برای ارسال داریم
        // آرایه اول رو پر میکنیم تا اگر جا باشه ارسال کنیم
        for(int i = sentUntil; i<data->total_chunks ; i++){
            // اینجا خوب میشه اگر بررسی کنیم که جا چطوره و اگر پر باشه بریک کنیم که این تا اخر لوپ نره
            if(data->acknowledged_chunks[i] == 0){ // اول میبینیم کدوما اک نشدن
                bool chunkExists = false;
                for(int j = 0 ; j < 8; j++){                
                    if (mustSentChunks[j] == i){ // اول چک میکنیم که آیا توی این آرایه وجود داره یا نه
                        chunkExists = true;
                        break;
                    }
                }
                if(!chunkExists){
                    for(int j = 0 ; j < 8; j++){ // اگر توی این آرایه نباشه
                        if (mustSentChunks[j] == -1){ // همچنین اگر جا داشته باشیم اضافه میکنیم
                            mustSentChunks[j] = i;
                            break;
                        }
                    }
                }
            }
        } 

        // چانک ها را ارسال کنیم
        // اما اینجا پاک نمیکنیم. تو مرحله بعد از هر دو آرایه پاک میکنیم
        // اینجا اونایی رو ارسال میکنیم که ارسال نشدن تا حالا
        // همچنین باید چک کنیم که آرایه دوم جا داشته باشه
        for(int i =0; i< 8; i++){
            bool chunkIsInBoth = false; // اگر این ترو بشه یعنی قبلا ارسال شده
            if(mustSentChunks[i] != -1){ // اگر چیزی برای ارسال باشه
                for(int j = 0 ; j< 8 ; j++){
                    if(sendingChunks[j] == mustSentChunks[i]) //اگر قبلا ارسال شده
                    chunkIsInBoth = true;
                    break;
                }
            }
            if(!chunkIsInBoth)
            {
                // اگر دومی جا داشته باشه
                for(int j =0 ; j< 8; j++){
                    if(sendingChunks[j] == -1)
                    {
                        if(mustSentChunks[i] != -1)
                        {
                        //printf("sending %d\n", mustSentChunks[i]);
                        send_file_chunk_with_seq(data->file, &data->client_addr, data->sockfd, mustSentChunks[i]);
                        times[j] = current_time_in_ms();
                        sendingChunks[j] = mustSentChunks[i]; 
                        // ارسال میکنیم و این چانک رو به چانک های در حال ارسال اضافه میکنیم
                        break;
                        }
                    }
                }

            }
        }

        // منتظر بمونیم که یکی از چانکها ارسال بشه
        bool running = true;
        while(running){
            
            for(int i =0 ;i< 8; i++){
                if(sendingChunks[i] != -1 )
                {
                    if(data->acknowledged_chunks[sendingChunks[i]] == 1)
                    {
                        // یعنی این چانک ارسال شده و اک شده
                        // دو تا درایه از دو تا آرایه که مربوط به این چانک هست رو منفی یک میذاریم
                        // باید سنت آنتین رو هم تغییر بدیم اگر نیاز باشه
                        running = false;
                        for(int j = 0; j< 8; j++){
                            if(mustSentChunks[j] == sendingChunks[i])
                            {
                                if(sendingChunks[i] < sentUntil)
                                {
                                    sentUntil = sendingChunks[i];
                                }
                                mustSentChunks[j] = -1;
                                sendingChunks[i] = -1;
                                times[i] = 0;
                            }
                        }

                    }
                }
            }


            // باید تایم ارسال رو داشته باشیم و اگر از یه حدی بیشتر بشه و این هنوز حذف نشده باشه جذف کنیم تا دوباره ارسال بشه
            // باید بررسی کنیم که سنت آنتیل کوچیکتر از اینی باشه که حذف کردیم
            // اگر بزرگتر بود مقدارش رو تغییر میدیم که اول تابع بیاد دوباره ارسالش کنه
            for(int i = 0 ; i< 8 ; i++){
                if(sendingChunks[i] != -1){
                    if(compare_time_in_ms(times[i])){
                        for(int j =0 ; j< 8; j++){
                            if(mustSentChunks[j] == sendingChunks[i])
                            {
                                if(mustSentChunks[j] != -1){
                                    if(mustSentChunks[j] < sentUntil)
                                        sentUntil = mustSentChunks[j];
                                }
                                mustSentChunks[j] = -1;
                            }
                        }
                        sendingChunks[i] = -1;
                        times[i] = 0;
                        running = false;
                    }

                }
            }


        }


    }

    usleep(500 * 1000);

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
    printf("Must send %d chunks: \n", total_chunks);


    while (1) {
        while(lock == 1) {}
        int n = recvfrom(sockfd, buffer, sizeof(int), 0, (struct sockaddr *)&client_addr, &addr_len);
        int doIStart = *(int *)buffer;
        if (doIStart == -1){ 
            // اینجا منتظر میمونیم که درخواست ارسال فایل بیاد. ممکنه این درخواست چند تا بیاد
            // ما اینجا میسازیم. بعدش یه پیام باید ارسال کنیم که برات ساختیم دیگه درخواست نده
            // حالا اونور که پیام رو بگیره. میره رو حالتی که فقط منتظر فایل باشه
            if (!client_exists(clients, &client_addr, MAX_CLIENTS)) {
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (!clients[i].active) {
                        clients[i].client_addr = client_addr;
                        clients[i].active = true;

                        thread_data_t *data = (thread_data_t *)malloc(sizeof(thread_data_t));

                        // Create separate file descriptor for each client
                        FILE *file_copy = fopen("largefile.mkv", "rb");
                        if (!file_copy) {
                            perror("File open failed");
                            continue;
                        }

                        // Allocate separate arrays for each client
                        int *acknowledged_chunks = (int *)calloc(total_chunks, sizeof(int));
                        if (!acknowledged_chunks) {
                            perror("Memory allocation failed");
                            fclose(file_copy);
                            continue;
                        }

                        struct timeval timeout;
                        timeout.tv_sec = 0;  // Zero seconds
                        timeout.tv_usec = 10 * 1000 * 1000; // هر سوکت ۱۰ ثانیه صبر کنه و بعدش اگر چیزی دریافت نکرد حذفش کنیم
                        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

                        data->sockfd = sockfd;
                        data->client_addr = client_addr;
                        data->file = file_copy; // Use the separate file descriptor
                        data->total_chunks = total_chunks;
                        data->acknowledged_chunks = acknowledged_chunks;

                        printf("Creating a thread for:\n");
                        print_sockaddr_in(&data->client_addr);

                        // اینجا پیام ساخته شدن سوکت رو ارسال میکنیم
                        // تا کلاینت دیگه درخواست ساخت نده
                        int created = -2;
                        sendto(sockfd, &created, sizeof(int), 0, (struct sockaddr *)&client_addr, sizeof(client_addr));

                        pthread_t tid;
                        pthread_create(&tid, NULL, handle_client, (void *)data);
                        pthread_detach(tid);
                        break;
                    }
                }
            }
            lock = 1; // تابع مین رو قفل میکنیم تا از این به بعد فقط منتظر اک باشیم و دیگه درخواست ها اینجا نیاد
        }
    }


    fclose(file);
    close(sockfd);
    return 0;
}
