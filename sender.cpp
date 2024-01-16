#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include "opencv2/opencv.hpp"
#include <queue>
#include <pthread.h>
#include <unistd.h>
#include <math.h>
#include <list>

using namespace std;
using namespace cv;

#define BUFF_SIZE 4000
#define TIMEOUT 0.001

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
	char data[BUFF_SIZE];
} segment;

list<segment> seg_list;
int width, height, imgSize;
char filename[1024];
int window_threshold = 16;
int window_size = 1;
int nextseqnum = 1;
int send_base = 1;
clock_t start, end;


int sendersocket, portNum, nBytes;
float loss_rate;
segment s_tmp;
struct sockaddr_in sender, agent, tmp_addr;
socklen_t sender_size, agent_size, tmp_size;
char ip[3][50];
int port[3], i;
int total_data = 0;
int drop_data = 0;
int segment_size, ind;
char ipfrom[1000];
char *ptr;
int portfrom;

void setIP(char *dst, char *src) {
    if(strcmp(src, "0.0.0.0") == 0 || strcmp(src, "local") == 0
            || strcmp(src, "localhost")) {
        sscanf("127.0.0.1", "%s", dst);
    } else {
        sscanf(src, "%s", dst);
    }
}

int find_max(int a, int b){
    if(a>b) return a;
    else return b;
}

void modify_seg_list(VideoCapture *cap){
    Mat imgServer;
    if(!imgServer.isContinuous()){
        imgServer = imgServer.clone();
    }

    Mat img;
    if(!img.isContinuous()){
        img = img.clone();
    }

    while(seg_list.size() < window_size){
        imgServer = Mat::zeros(height,width, CV_8UC3);
        img = Mat::zeros(height,width, CV_8UC3);
        *cap >> imgServer;
        
        char c = (char)waitKey(33.3333);
        int size = imgServer.total() * imgServer.elemSize();
        if(size == 0){ // last frame => send fin
            memset(&s_tmp, 0, sizeof(s_tmp));
            s_tmp.head.fin = 1;
            s_tmp.head.seqNumber = nextseqnum;
            nextseqnum ++;
            seg_list.push_back(s_tmp);
            break;
        }
        else{
            int accu_size = 0;
            while(accu_size < imgSize){
                memset(&s_tmp, 0, sizeof(s_tmp));
                if(imgSize - accu_size > BUFF_SIZE){ // 裝滿整個 segment
                    memcpy(&s_tmp.data, &imgServer.data[accu_size], BUFF_SIZE);
                    accu_size += BUFF_SIZE;
                    s_tmp.head.length = BUFF_SIZE;
                }
                else{
                    memcpy(&s_tmp.data, &imgServer.data[accu_size], imgSize - accu_size);
                    s_tmp.head.length = imgSize - accu_size;
                    accu_size = imgSize;
                }
                s_tmp.head.seqNumber = nextseqnum;
                nextseqnum ++;
                seg_list.push_back(s_tmp);
            }
        }
    }
}

int main(int argc, char* argv[]){
    if(argc != 5){
        fprintf(stderr,"用法: %s <agent IP> <agent port> <sender port> <file name>\n", argv[0]);
        fprintf(stderr, "例如: ./sender 127.0.0.1 8888 8887 tmp.mpg\n");
        exit(1);
    } else {
        setIP(ip[0], "local"); // sender ip
        setIP(ip[1], argv[1]); // agent ip

        sscanf(argv[3], "%d", &port[0]); // sender port
        sscanf(argv[2], "%d", &port[1]); // agent port
        memset(&filename, 0, sizeof(filename));
        strcpy(filename, argv[4]);
    }

    /*Create UDP socket*/
    sendersocket = socket(PF_INET, SOCK_DGRAM, 0);

    /*Configure settings in sender struct*/
    sender.sin_family = AF_INET;
    sender.sin_port = htons(port[0]);
    sender.sin_addr.s_addr = inet_addr(ip[0]);
    memset(sender.sin_zero, '\0', sizeof(sender.sin_zero));  

    /*Configure settings in agent struct*/
    agent.sin_family = AF_INET;
    agent.sin_port = htons(port[1]);
    agent.sin_addr.s_addr = inet_addr(ip[1]);
    memset(agent.sin_zero, '\0', sizeof(agent.sin_zero));   

    /*bind socket*/
    bind(sendersocket,(struct sockaddr *)&sender,sizeof(sender)); 

    /*Initialize size variable to be used later on*/
    sender_size = sizeof(sender);
    agent_size = sizeof(agent);
    tmp_size = sizeof(tmp_addr);

    // server
    VideoCapture cap(filename);

    // get the resolution of the video and sent to agent
    width = cap.get(CV_CAP_PROP_FRAME_WIDTH);
    height = cap.get(CV_CAP_PROP_FRAME_HEIGHT);
    imgSize = width * height * 3;
    memcpy(&s_tmp.data, &width, sizeof(int));
    memcpy(&s_tmp.data[sizeof(int)], &height, sizeof(int));
    memcpy(&s_tmp.data[sizeof(int) * 2], &imgSize, sizeof(int));
    s_tmp.head.length = 3 * sizeof(int);
    s_tmp.head.seqNumber = nextseqnum;
    nextseqnum ++;
    seg_list.push_back(s_tmp);

    int final = 0;

    while(1){
        modify_seg_list(&cap);

        // 傳 window_size 個 segment or 傳到 fin 為止
        list<segment>::iterator i;
        i = seg_list.begin();
        if(i->head.seqNumber != send_base){
            fprintf(stderr, "error: send base & seq not match");
            exit(1);
        }
        for(int cnt = 0; cnt < window_size && i != seg_list.end(); cnt ++, i ++){ 
            memset(&s_tmp, 0, sizeof(s_tmp));
            s_tmp = *i;
            sendto(sendersocket, &s_tmp, sizeof(segment), 0, (struct sockaddr *)&agent, agent_size);            
            if(s_tmp.head.fin == 1){
                printf("send     fin\n");
            }
            else printf("send     data	#%d,    winSize = %d\n", s_tmp.head.seqNumber, window_size);
            if(cnt == 0){
                // timer start
                start = clock();
            }
            if(s_tmp.head.fin == 1){
                window_size = cnt + 1;
                break;
            }
        }
        
        // 預期收 window_size 個 ack or 等到 timeout
        int expectedacknum = send_base;
        end = clock();
        double   duration;
        duration = (double)(end-start) / (double)CLOCKS_PER_SEC;
        int ret;

        while(expectedacknum < send_base + window_size && duration < TIMEOUT){
            memset(&s_tmp, 0, sizeof(s_tmp));
            ret = recvfrom(sendersocket, &s_tmp, sizeof(s_tmp), MSG_DONTWAIT, (struct sockaddr *)&agent, &agent_size);
            if(ret <= 0){ // 這輪 while 沒有收到 recv
                end = clock();
                duration = (double)(end-start) / (double)CLOCKS_PER_SEC;
                continue;
            }

            if(s_tmp.head.ack == 0) {
                fprintf(stderr, "收到來自 agent 的 non-ack segment\n");
                exit(1);
            }

            ind = s_tmp.head.ackNumber;
            if(s_tmp.head.fin == 1){
                printf("recv     fin\n");
                final = 1;
                break;
            }
            else
                printf("recv     ack	#%d\n", ind);
            if(ind == expectedacknum){ // 收到一個想要的 ack
                start = clock();
            }
            expectedacknum = ind + 1;
            i = seg_list.begin();
            while(!seg_list.empty() && i->head.seqNumber <= ind){
                seg_list.pop_front();
                i = seg_list.begin();
            }
            end = clock();
            duration = (double)(end-start) / (double)CLOCKS_PER_SEC;
        }
        if(final == 1) break; // 收完 finack
        if(expectedacknum == send_base + window_size){ // 完整 send/recv 完整個 window
            if(window_size < window_threshold) window_size *= 2;
            else window_size ++;
        }
        else{ // timeout
            window_threshold = find_max((int)floor(window_size/2), 1);
            window_size = 1;
            printf("time out,                   threshold = %d\n", window_threshold);
        }
        send_base = expectedacknum;
    }
    
    


    return 0;
}