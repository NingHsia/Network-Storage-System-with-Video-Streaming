#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include "opencv2/opencv.hpp"
#include <queue>
#include <string>

using namespace std;
using namespace cv;

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
	char data[4000];
} segment;

queue<segment> seg_buf;

queue<Mat> buffer_mat;
pthread_mutex_t buf_mutex = PTHREAD_MUTEX_INITIALIZER;

int width, height, imgSize;
int flag = 0;
Mat imgClient;


int segment_size, ind;
char ipfrom[1000];
char *ptr;
int portfrom;
int expectedseqnum = 1;
int accu_size = 0;

int receiversocket, portNum, nBytes;
float loss_rate;
segment s_tmp;
struct sockaddr_in sender, agent, receiver, tmp_addr;
socklen_t recv_size, agent_size, tmp_size;
char ip[3][50];
int port[3], i;

void setIP(char *dst, char *src) {
    if(strcmp(src, "0.0.0.0") == 0 || strcmp(src, "local") == 0
            || strcmp(src, "localhost")) {
        sscanf("127.0.0.1", "%s", dst);
    } else {
        sscanf(src, "%s", dst);
    }
}

void flush_buffer(){
    while(!seg_buf.empty()){
        memset(&s_tmp, 0, sizeof(s_tmp));
        s_tmp = seg_buf.front();
        seg_buf.pop();
        memcpy(&imgClient.data[accu_size], &s_tmp.data, s_tmp.head.length);
        accu_size += s_tmp.head.length;
        if(accu_size == imgSize){
            pthread_mutex_lock(&buf_mutex);
            buffer_mat.push(imgClient);
            pthread_mutex_unlock(&buf_mutex);
            uchar *uptr = new uchar[imgSize];
            imgClient.data = uptr;
            imgClient = Mat::zeros(height,width, CV_8UC3);   
            accu_size = 0;
        }
    }
}

void *thr_rs(void* arg) {
    while(1){
        /*Receive message from agent*/
        memset(&s_tmp, 0, sizeof(s_tmp));
        segment_size = recvfrom(receiversocket, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, &agent_size);
        if(s_tmp.head.fin == 1){
            printf("recv     fin\n");
            s_tmp.head.ack = 1;
            sendto(receiversocket, &s_tmp, segment_size, 0, (struct sockaddr *)&agent, agent_size);
            printf("send     finack\n");
            flush_buffer();
            flag = 1;
            break;
        }
        else{
            ind = s_tmp.head.seqNumber;
            if(ind == expectedseqnum){ // is expect segment  
                if(ind == 1){ // first segment: width & height & imgSize
                    if(s_tmp.head.length != 3 * sizeof(int)){
                        fprintf(stderr, "error: first segment length wrong\n");
                        exit(1);
                    }
                    printf("recv     data	#%d\n", ind);
                    memcpy(&width, &s_tmp.data, sizeof(int));
                    memcpy(&height, &s_tmp.data[sizeof(int)], sizeof(int));
                    memcpy(&imgSize, &s_tmp.data[sizeof(int) * 2], sizeof(int));
                    imgClient = Mat::zeros(height,width, CV_8UC3); 
                    s_tmp.head.ack = 1;
                    s_tmp.head.ackNumber = expectedseqnum;

                    sendto(receiversocket, &s_tmp, segment_size, 0, (struct sockaddr *)&agent, agent_size);
                    printf("send     ack	#%d\n", expectedseqnum);
                    expectedseqnum ++;
                }
                else{ // other segments: part of a frame
                    if(seg_buf.size() < 32){ // buffer not full
                        printf("recv     data	#%d\n", ind);

                        seg_buf.push(s_tmp);
                        s_tmp.head.ack = 1;
                        s_tmp.head.ackNumber = expectedseqnum;
                        sendto(receiversocket, &s_tmp, segment_size, 0, (struct sockaddr *)&agent, agent_size);
                        printf("send     ack	#%d\n", expectedseqnum);
                        expectedseqnum ++;
                    }
                    else{ // buffer full so flush
                        printf("drop    data	#%d\n", s_tmp.head.seqNumber);
                        s_tmp.head.ackNumber = expectedseqnum - 1;
                        s_tmp.head.ack = 1;
                        sendto(receiversocket, &s_tmp, segment_size, 0, (struct sockaddr *)&agent, agent_size);
                        printf("send     ack	#%d\n", expectedseqnum-1);
                        flush_buffer();
                        printf("flush\n");
                    }
                }        
                
            }
            else{ // is not expect segment
                printf("drop    data	#%d\n", s_tmp.head.seqNumber);
                s_tmp.head.ackNumber = expectedseqnum - 1;
                s_tmp.head.ack = 1;
                sendto(receiversocket, &s_tmp, segment_size, 0, (struct sockaddr *)&agent, agent_size);
                printf("send     ack	#%d\n", expectedseqnum-1);
            }
        }
    }
}



int main(int argc, char* argv[]){
    if(argc != 4){
        fprintf(stderr,"用法: %s <agent IP> <agent port> <receiver port>\n", argv[0]);
        fprintf(stderr, "例如: ./receiver 127.0.0.1 8888 8889\n");
        exit(1);
    } else {
        setIP(ip[0], "local"); // receiversender ip
        setIP(ip[1], argv[1]); // agent ip

        sscanf(argv[3], "%d", &port[0]); // receiver port
        sscanf(argv[2], "%d", &port[1]); // agent port
    }

    /*Create UDP socket*/
    receiversocket = socket(PF_INET, SOCK_DGRAM, 0);

    /*Configure settings in agent struct*/
    agent.sin_family = AF_INET;
    agent.sin_port = htons(port[1]);
    agent.sin_addr.s_addr = inet_addr(ip[1]);
    memset(agent.sin_zero, '\0', sizeof(agent.sin_zero));    

    /*Configure settings in receiver struct*/
    receiver.sin_family = AF_INET;
    receiver.sin_port = htons(port[0]);
    receiver.sin_addr.s_addr = inet_addr(ip[0]);
    memset(receiver.sin_zero, '\0', sizeof(receiver.sin_zero)); 

    /*bind socket*/
    bind(receiversocket,(struct sockaddr *)&receiver,sizeof(receiver));

    /*Initialize size variable to be used later on*/
    recv_size = sizeof(receiver);
    agent_size = sizeof(agent);
    tmp_size = sizeof(tmp_addr);

    srand(time(NULL));

    if(!imgClient.isContinuous()){
        imgClient = imgClient.clone();
    }

    pthread_t thr;
    pthread_create(&thr, NULL, thr_rs, NULL);

    Mat img;
    if(!img.isContinuous()){
        img = img.clone();
    }
    img = Mat::zeros(height,width, CV_8UC3);   

    while(1){
        pthread_mutex_lock(&buf_mutex);
        int bufSize = buffer_mat.size();

        if(buffer_mat.size() <= 0){
            if(flag == 1){
                pthread_mutex_unlock(&buf_mutex);
                break;
            }
            pthread_mutex_unlock(&buf_mutex);
            continue;
        }

                             
        img = buffer_mat.front();
        buffer_mat.pop();
        pthread_mutex_unlock(&buf_mutex);
                    
        imshow("Video", img);
        char c = (char)waitKey(33.3333);
        img = Mat::zeros(height,width, CV_8UC3);   
    }
    




    destroyAllWindows();
    pthread_join(thr, NULL);
    
    return 0;
}