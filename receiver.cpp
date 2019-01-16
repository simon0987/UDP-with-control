//
//  receiver.cpp
//  CN_HW2
//
//  Created by 柯哲邦 on 2018/12/15.
//  Copyright © 2018年 柯哲邦. All rights reserved.
//

#include <iostream>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string>
#include <vector>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
using namespace std;

typedef struct {
    int length;
    int seqNumber;
    int ackNumber;
    int fin;
    int syn;
    int ack;
} header;

typedef struct{
    header head;
    char data[1000];
} segment;
typedef struct{
    char data[1024];
    int length;
    int used;
} Buffer;

char IP[30];
int port,buffer_size = 32;
char filePath[150];
Buffer *buff;

int total_used = 0;
int base = 1, seq = 1;
int expected_base = 1;

void setIP(char *dst, char *src) {
    if(strcmp(src, "0.0.0.0") == 0 || strcmp(src, "local") == 0
       || strcmp(src, "localhost")) {
        sscanf("127.0.0.1", "%s", dst);
    } else {
        sscanf(src, "%s", dst);
    }
}

int main(int argc,char* argv[]){

    int sockfd;
    struct sockaddr_in my_addr,agentaddr;
    socklen_t agentaddr_size = sizeof(agentaddr);
    if(argc!=4){
        fprintf(stderr,"用法: %s <recv IP> <recv port> <new file path and name>\n", argv[0]);
        fprintf(stderr, "例如: ./receiver local 8889 result.txt\n");
        exit(1);
    }
    else{
        setIP(IP, argv[1]);
        sscanf(argv[2],"%d", &port);
        sscanf(argv[3], "%s",filePath);
    }
    
    buff = (Buffer*)calloc(buffer_size, sizeof(Buffer));
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd<0){
        perror("socket creation failed.");
        exit(EXIT_FAILURE);
    }
    /*bind the socket*/
    memset((char *)&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(port);
    inet_pton(AF_INET, IP, &my_addr.sin_addr);
    bind(sockfd,(struct sockaddr *)&my_addr,sizeof(my_addr));
    
    /*write to the file*/
    int file;
    file = open(filePath, O_WRONLY | O_TRUNC | O_CREAT,0444);
    
    segment rcv_segment, send_segment;
    int flag = true;
    while(true){
        recvfrom(sockfd, &rcv_segment, sizeof(segment), 0, (struct sockaddr*)&agentaddr, &agentaddr_size);
        seq = rcv_segment.head.seqNumber;
        int expected_base_new = expected_base % buffer_size;
        if(expected_base_new ==0)
            expected_base_new = buffer_size;
        /*the receive segment is data*/
        if(rcv_segment.head.ack == 0 && rcv_segment.head.fin == 0){
            //means the 33,65,97...th segment, we need to drop the segment first, then flush.
            if(total_used == 1)
                flag = true;
            if(expected_base_new==1 && expected_base!=1 && flag){
                std::cout << "drop\tdata\t#" << seq << std::endl;
                send_segment.head.ackNumber = expected_base-1;
                send_segment.head.length = 0;
                memset(send_segment.data, 0, 1024*sizeof(char));
                sendto(sockfd, &send_segment, sizeof(segment), 0, (struct sockaddr*)&agentaddr, agentaddr_size);
                flag = false;
                continue;
            }
            /*if buffer size is full*/
            if(total_used == buffer_size){
                std::cout << "flush" << std::endl;
                for(int i = 0 ; i < buffer_size;i++){
                    write(file, buff[i+1].data, buff[i+1].length);
                }
                //after writing the whole buffer size to file
                memset(buff, 0, buffer_size * sizeof(Buffer));
                total_used = 0;
            }
            //sequence number is not the expected one; thus, drop data
            if(seq > expected_base){
                std::cout << "drop\tdata\t#" << seq << std::endl;
                send_segment.head.ackNumber = expected_base-1;
            }
            else if(seq < expected_base){
                std::cout << "drop\tdata\t#" << seq << std::endl;
                send_segment.head.ackNumber = seq;
            }
            else{
                //ack the data
                std::cout << "recv\tdata\t#" << seq << endl;
                
                memcpy(buff[expected_base_new].data, rcv_segment.data, rcv_segment.head.length);
                buff[expected_base_new].length = rcv_segment.head.length;
                buff[expected_base_new].used = 1;
                send_segment.head.ack = 1;
                send_segment.head.ackNumber = expected_base;
                expected_base++;
                total_used++;
            }
        }
        /*the receive segment is fin*/
        else if(rcv_segment.head.fin == 1){
            std::cout << "recv\tfin" << std::endl;
            send_segment.head.fin = 1;
        }
        
        send_segment.head.length = 0;
        memset(send_segment.data, 0, 1024*sizeof(char));
        sendto(sockfd, &send_segment, sizeof(segment), 0, (struct sockaddr*)&agentaddr, agentaddr_size);
        if(send_segment.head.ack==1 && send_segment.head.fin == 0)
            std::cout << "send\tack\t#" << send_segment.head.ackNumber << std::endl;
        else if(send_segment.head.fin ==1){
            std::cout << "send\tfinack" << std::endl;
            break;
        }
    }
    
    //write all the remaining buffer to the file.
    for(int i = 0 ; buff[i+1].used==1 && i < buffer_size;i++){
        if(i==0)
            std::cout << "flush" << std::endl;
        write(file, buff[i+1].data, buff[i+1].length);
    }
    
    free(buff);
    close(sockfd);
    return 0;
}

