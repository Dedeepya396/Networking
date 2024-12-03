#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>
#define size 1000
#include <fcntl.h>    // For non-blocking socket
#include <sys/time.h> // For timeout
#define TIMEOUT_SEC 0
#define TIMEOUT_USEC 100000 // 0.1 seconds
#define chunk_size 10
int num_packets = 0;
int flag = 0;
typedef struct packet
{
    char data[size];
    int seq_num;
} packet;
packet pkts[size];

typedef struct recv_packet
{
    char data[size];
    int seq_num;
} recv_packet;

recv_packet recv_pkts[size];
int num_recv_pkts = 0;

void print_packets()
{
    for (int i = 0; i < num_packets; i++)
    {
        printf("%s", pkts[i].data);
    }
    printf("\n");
}

void clearPackets()
{
    for (int i = 0; i < num_packets; i++)
    {
        strcpy(pkts[i].data, "");
        pkts[i].seq_num = -1;
    }
    for (int i = 0; i < num_recv_pkts; i++)
    {
        strcpy(recv_pkts[i].data, "");
        recv_pkts[i].seq_num = -1;
    }
}

void divide_data(char data[])
{
    int x = 0;
    int data_len = strlen(data);
    data[strcspn(data, "\n")] = '\0';
    int i = 0;
    while (x < data_len)
    {
        int len = chunk_size;
        if (x + len >= data_len)
        {
            len = data_len - x;
        }
        char buff[len + 1];
        strncpy(buff, data + x, len);
        buff[len] = '\0';
        strcpy(pkts[i].data, buff);
        pkts[i].seq_num = i;
        i++;
        strcpy(buff, "");
        x += len;
    }
    num_packets = i;
}
// void send_data(char data[], int server_socket, socklen_t addr_size, struct sockaddr_in client_addr)
// {
//     divide_data(data);
//     int i = 0;
//     char buffer[100];
//     char complete_data[2000];

//     // send data packets
//     for (int i = 0; i < num_packets; i++)
//     {
//         memset(complete_data, 0, sizeof(complete_data));
//         snprintf(complete_data, sizeof(complete_data), "%d:%s", pkts[i].seq_num, pkts[i].data);
//         sendto(server_socket, complete_data, strlen(complete_data), 0, (struct sockaddr *)&client_addr, addr_size);
//     }
//     // send number of packets that has been sent
//     char num_chunks[30];
//     snprintf(num_chunks, sizeof(num_chunks), "%d:%s", num_packets, "Number of chunks");
//     sendto(server_socket, num_chunks, strlen(num_chunks), 0, (struct sockaddr *)&client_addr, addr_size);

//     // receive acknowledgement from client
//     char ack[size];
//     int n = recvfrom(server_socket, ack, sizeof(ack), 0, (struct sockaddr *)&client_addr, &addr_size);
//     if (n <= 0)
//     {
//         perror("Client is disconnected or there is an error in receiving data\n");
//         return;
//     }
//     printf("ACK from client: %s\n", ack);
//     if (strstr(ack, "Received complete data"))
//     {
//         clearPackets();
//         num_packets = 0; // reset number of packets
//         return;
//     }
// }

void send_data(char data[], int server_socket, socklen_t addr_size, struct sockaddr_in client_addr)
{
    divide_data(data);
    char complete_data[2000];
    fd_set read_fds;
    struct timeval timeout;

    fcntl(server_socket, F_SETFL, O_NONBLOCK);

    for (int i = 0; i < num_packets; i++)
    {
        int ack_received = 0;
        while (!ack_received)
        {
            // Send the current data packet
            memset(complete_data, 0, sizeof(complete_data));
            snprintf(complete_data, sizeof(complete_data), "%d:%s", pkts[i].seq_num, pkts[i].data);
            sendto(server_socket, complete_data, strlen(complete_data), 0, (struct sockaddr *)&client_addr, addr_size);

            // Initialize the file descriptor set
            FD_ZERO(&read_fds);
            FD_SET(server_socket, &read_fds);

            // Set the timeout
            timeout.tv_sec = TIMEOUT_SEC;
            timeout.tv_usec = TIMEOUT_USEC;

            // Wait for ACK using select
            int activity = select(server_socket + 1, &read_fds, NULL, NULL, &timeout);

            if (activity > 0 && FD_ISSET(server_socket, &read_fds))
            {
                // Receive ACK from the client
                char ack[100];
                int n = recvfrom(server_socket, ack, sizeof(ack), 0, (struct sockaddr *)&client_addr, &addr_size);
                if (n > 0)
                {
                    ack[n] = '\0';
                    int received_seq_num = atoi(ack); // Expecting ACK with sequence number
                    if (received_seq_num == pkts[i].seq_num)
                    {
                        printf("ACK received for packet %d\n", received_seq_num);
                        ack_received = 1; // Move to the next packet
                    }
                }
            }
            else
            {
                // Timeout occurred, resend the packet
                printf("Timeout, resending packet %d\n", pkts[i].seq_num);
            }
        }
    }

    // Send the number of chunks after all packets are sent
    char num_chunks[30];
    snprintf(num_chunks, sizeof(num_chunks), "%d:%s", num_packets, "Number of chunks");
    sendto(server_socket, num_chunks, strlen(num_chunks), 0, (struct sockaddr *)&client_addr, addr_size);

    // Wait for final acknowledgment from the client
    FD_ZERO(&read_fds);
    FD_SET(server_socket, &read_fds);
    timeout.tv_sec = TIMEOUT_SEC;
    timeout.tv_usec = TIMEOUT_USEC;
    int activity = select(server_socket + 1, &read_fds, NULL, NULL, &timeout);

    if (activity > 0 && FD_ISSET(server_socket, &read_fds))
    {
        char final_ack[100];
        int n = recvfrom(server_socket, final_ack, sizeof(final_ack), 0, (struct sockaddr *)&client_addr, &addr_size);
        if (n > 0)
        {
            final_ack[n] = '\0';
            if (strstr(final_ack, "Received complete data"))
            {
                printf("Client received all packets successfully.\n");
            }
        }
    }

    // Clear packets after successful transmission
    clearPackets();
    num_packets = 0;
    int flags = fcntl(server_socket, F_GETFL, 0);
    fcntl(server_socket, F_SETFL, flags & ~O_NONBLOCK);
}

void receive_data(int server_socket, struct sockaddr_in server_addr, socklen_t addr_size)
{
    int n = 0;
    int present_seq = -1;
    int received_chunks = 0;
    char buffer[size];
    char data[size];
    int k = 0;

    while (1)
    {
        // Receive the data packet
        n = recvfrom(server_socket, buffer, sizeof(buffer), 0, (struct sockaddr *)&server_addr, &addr_size);
        if (n < 0)
        {
            perror("Error receiving message from server");
            exit(EXIT_FAILURE);
        }
        buffer[n] = '\0';

        // Extract the sequence number and data
        strcpy(data, buffer + strcspn(buffer, ":") + 1);
        int seq_num = atoi(buffer);
        if (strcmp(data, "Number of chunks") != 0)
        {
            // Process the data packet
            present_seq++;
            received_chunks++;
            strcpy(recv_pkts[k].data, data);
            recv_pkts[k].seq_num = seq_num;
            k++;

            // Print received packet
            printf("Received packet %d: %s\n", seq_num, data);

            // Send ACK for the received chunk
            char ack[30];
            snprintf(ack, sizeof(ack), "%d", seq_num);
            sendto(server_socket, ack, strlen(ack), 0, (struct sockaddr *)&server_addr, addr_size);
            if(strcmp(data,"exit")==0)
                flag = 1;
        }
        else if (strcmp(data, "Number of chunks") == 0)
        {
            // Handle the final chunk transmission confirmation
            int num_chunks = atoi(buffer);
            if (num_chunks == received_chunks)
            {
                printf("All chunks received (%d chunks)\n", num_chunks);
                for (int i = 0; i < num_chunks; i++)
                {
                    printf("%s", recv_pkts[i].data);
                }
                sendto(server_socket, "Received complete data", strlen("Received complete data"), 0, (struct sockaddr *)&server_addr, addr_size);
            }
            break;
        }
    }
    clearPackets();
    num_recv_pkts = 0;
}

int main()
{
    char ip_addr[] = "127.0.0.1";
    int port = 8080;
    int server_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (server_socket < 0)
    {
        perror("Error in creating socket\n");
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip_addr);
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Error binding socket");
        return EXIT_FAILURE;
    }
    addr_size = sizeof(client_addr);
    printf("Yes\n");
    char buffer[100];
    int n = recvfrom(server_socket, buffer, 100, 0, (struct sockaddr *)&client_addr, &addr_size);
    printf("%s\n", buffer);
    char a[size];
    while (1)
    {
        printf("Enter data to send to client: ");
        fgets(a, size, stdin);
        if (strcmp("exit\n", a) == 0)
        {
            send_data(a, server_socket, addr_size, client_addr);
            break;
        }
        send_data(a, server_socket, addr_size, client_addr);
        printf("Received data\n");
        printf("Client:\n");
        receive_data(server_socket, server_addr, addr_size);
        printf("\n");
        if(flag==1)
            break;
    }
}

// tcp maintains track of every segment that has been received or transmitted, where as udp doesn't, use seq number
// for ack we have to use seq number, we need to send data after a time out but we shouldn't check after every packet
// sliding window!!
