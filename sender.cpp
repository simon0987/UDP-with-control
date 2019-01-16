//
//  main.cpp
//  CN_HW2
//
//  Created by 柯哲邦 on 2018/12/14.
//  Copyright © 2018年 柯哲邦. All rights reserved.
//

#include <iostream>
#include <time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string>
#include <fstream>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>


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
    segment seg;
    int acked;
    int sent;
} Window_seg;

char srcIP[30], agentIP[30], filePath[150];
int srcPort,agentPort,threshold = 16,timeout_sec = 1, timeout_micro = 0;

void make_segment(header *,char*,int,segment*);

void setIP(char *dst, char *src) {
    if(strcmp(src, "0.0.0.0") == 0 || strcmp(src, "local") == 0
       || strcmp(src, "localhost")) {
        sscanf("127.0.0.1", "%s", dst);
    } else {
        sscanf(src, "%s", dst);
    }
}

int main(int argc,char * argv[]) {
    int sockfd;
    struct sockaddr_in senderaddr,agentaddr; //addresses
    
    if(argc!=6){
        fprintf(stderr,"用法: %s <sender IP> <sender port> <agent IP> <agent port> <new file path and name>\n", argv[0]);
        fprintf(stderr, "例如: ./sender local local 8887 8888 a.txt\n");
        exit(1);
    }
    else{
        setIP(srcIP, argv[1]);
        setIP(agentIP, argv[2]);
        sscanf(argv[3], "%d", &srcPort);
        sscanf(argv[4], "%d", &agentPort);
        sscanf(argv[5], "%s",filePath);
    }
    
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd<0){
        perror("socket creation failed.");
        exit(EXIT_FAILURE);
    }
    /*bind sender socket*/
    memset((char *)&senderaddr, 0 ,sizeof(senderaddr));
    senderaddr.sin_family = AF_INET;
    senderaddr.sin_port = htons(srcPort);
    inet_pton(AF_INET, srcIP, &senderaddr.sin_addr);
    if(bind(sockfd, (struct sockaddr *)&senderaddr,sizeof(senderaddr))< 0){
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    
    // to connect to the agent, we have to know the agent's info
    memset((char *)&agentaddr, 0, sizeof(agentaddr));
    agentaddr.sin_family = AF_INET;
    agentaddr.sin_port = htons(agentPort);
    inet_pton(AF_INET, agentIP, &agentaddr.sin_addr);
    socklen_t agentaddr_size = sizeof(agentaddr);
    
    
    //construct the header of the segment
    header send_h;
    memset(&send_h, 0, sizeof(header));
    
    //set timeout: 1 sec
    struct timeval timeout;
    memset(&timeout, 0, sizeof(struct timeval));
    timeout.tv_sec = timeout_sec;
    timeout.tv_usec = timeout_micro;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
    /*read all the data and make each of them into a segment,
     and put in the window.*/
    int init_size = 2048;
    Window_seg *window = (Window_seg*) malloc(init_size * sizeof(Window_seg));
    char data[1000];
    int file = open(filePath, O_RDONLY);
    int input_data_size = 0;
    int total_segment = 0;
    int size = init_size;
    //every iteration makes a segment of data

    while(true){
        memset(data, 0, 1000*sizeof(char));
        input_data_size = read(file, data, 1000);
        // no more data ready to read.
        if(input_data_size == 0)
            break;
        //if the window is not enough.
        if(total_segment==size){
            size*=2;
            Window_seg *new_mem =(Window_seg *) realloc(window, size * sizeof(Window_seg));
            window = new_mem;
        }
        send_h.seqNumber = total_segment + 1;
        /*make each segment and put it into the window
          use total_segment to record the real segment in window*/
        make_segment(&send_h, data, input_data_size, &window[total_segment].seg);
        window[total_segment].acked = 0;    //set the previous acked &sent to 0
        window[total_segment].sent = 0;     //when the ack comes, set it to 1
        total_segment++;
    }
    
    /* send segment and receive ack*/
    int rcv_length;
    segment rcv_seg;
    int base = 1,win_size = 1;
    bool done = false;
    
    while(!done){
        /*send segment*/
        int total_send = 0;
        for(int i = base; i < base + win_size&&i <= total_segment;i++){
            sendto(sockfd, &window[i-1].seg, sizeof(segment), 0, (struct sockaddr *)&agentaddr, agentaddr_size);
            //if the seg is already sent, means it timeouted before and start again
            if(window[i-1].sent)
                printf("resnd\tdata\t#%d,\twinsize = %d\n",window[i-1].seg.head.seqNumber,win_size);
            else
                printf("send\tdata\t#%d,\twinsize = %d\n",window[i-1].seg.head.seqNumber,win_size);
            window[i-1].sent = 1;
            total_send++;
        }
        
        struct timeval start,end,diff;
        timeout.tv_sec = timeout_sec;
        timeout.tv_usec = timeout_micro;
        gettimeofday(&start, NULL);
        /*receive ack*/
        int total_rcv = 0;
        while (total_send > total_rcv) {
            rcv_length = recvfrom(sockfd, &rcv_seg, sizeof(rcv_seg), 0, (struct sockaddr*) &agentaddr, &agentaddr_size);
            if(rcv_length==-1 && errno == EAGAIN){
                timeout.tv_sec = timeout_sec;
                timeout.tv_usec = timeout_micro;
                break;
            }
            if(rcv_length == 0)
                continue;
            
            gettimeofday(&end, NULL);
            diff.tv_sec = end.tv_sec-start.tv_sec;
            diff.tv_usec = end.tv_usec-start.tv_usec;
            
            timeout.tv_sec = timeout.tv_sec - diff.tv_sec;
            if(timeout.tv_usec < diff.tv_usec&&timeout.tv_sec > diff.tv_sec){
                timeout.tv_sec--;
                timeout.tv_usec += 1000000;
                timeout.tv_usec -= diff.tv_usec;
            }
            else
                timeout.tv_usec = timeout.tv_usec - diff.tv_usec;
            
            std::cout << "recv\tack\t#" << rcv_seg.head.ackNumber << std::endl;
            //we got this ack already.
            if(window[rcv_seg.head.ackNumber-1].acked==1)
                continue;
            window[rcv_seg.head.ackNumber-1].acked = 1;
            total_rcv++;
        }
        /*segment loss  --> timeout*/
        if(total_rcv!=total_send){
            if(win_size/2 > 1)
                threshold = win_size/2;
            else
                threshold = 1;
            win_size = 1;
            std::cout << "time\tout,\t\tthreshold = " << threshold << std::endl;
            //when timeout happens, we find the base again.
            for(int i =base ; i <= total_segment;i++){
                if(window[i-1].acked==0){
                    base = i;
                    break;
                }
                if(i == total_segment)
                    done = true;
            }
        }
        //all segments in window are all acked, move the window.
        else{
            base += win_size;
            if(win_size < threshold)
                win_size*=2;
            else
                win_size+=1;
            if(base > total_segment)
                done = true;
        }
    }
        
    /*sender send fin,which means finish!*/
    segment fin_seg;
    send_h.fin = 1;
    send_h.seqNumber = total_segment;
    make_segment(&send_h, NULL, 0, &fin_seg);
    while(true){
        sendto(sockfd, &fin_seg, sizeof(segment), 0, (struct sockaddr *) &agentaddr, agentaddr_size);
        std::cout << "send\tfin" << std::endl;
        rcv_length = recvfrom(sockfd, &rcv_seg, sizeof(rcv_seg), 0, (struct sockaddr*) &agentaddr, &agentaddr_size);
        if(rcv_length==-1 && errno == EAGAIN)
            continue;
        if(rcv_seg.head.fin == 1){
            std::cout << "recv\tfinack" << std::endl;
            break;
        }
    }
    free(window);
    close(sockfd);
    return 0;
}

//set header, data and length to make a segment
void make_segment(header *h, char *data, int length, segment *seg){
    h->length = length;
    memset(seg, 0, sizeof(segment));
    memcpy(&seg->head, h, sizeof(header));
    memcpy(seg->data, data, length);
}
