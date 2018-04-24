#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define MAX_CONN 512            //最大链接数
#define LISTENQ 10              //链接队列长度
#define BUFFER_SIZE 1024        //缓冲区大小
#define PORT 6789               //监听端口
#define IP_ADDR "47.96.18.185"  //监听IP
#define MAX_EVENTS 20           //最大时间数
#define WAIT_TIMEOUT 30         //超时时间

char pong[] = "world";          //回复字符
int total_clients = 0;          //总的链接数

int main(){
    struct epoll_event ev, events[MAX_EVENTS];
    struct sockaddr_in serveraddr, clientaddr;
    int epfd;
    int listenfd, acceptfd;
    int nfds;
    int sock_fd, conn_fd;
    char buffer[BUFFER_SIZE];

    epfd = epoll_create(MAX_CONN);                  //生成epoll句柄
    if(epfd < 0){
        printf("create epoll instance error: %s(error: %d)\n", strerror(errno), errno);
        return -1;
    }

    listenfd = socket(AF_INET, SOCK_STREAM, 0);     //创建套接字
    if(listenfd < 0){
        printf("create socket error: %s(error: %d)\n", strerror(errno), errno);
        return -1;
    }

    ev.data.fd = listenfd;
    ev.events = EPOLLIN;                            //对应的文件描述符可以读
    epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev);  //注册epoll事件

    memset(&buffer, 0, sizeof(buffer));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(PORT);

    if(bind(listenfd, (struct sockaddr*) &serveraddr, sizeof(serveraddr)) != 0){
        printf("bind socket error: %s(error: %d)\n", strerror(errno), errno);
        return -1;
    }
    if(listen(listenfd, LISTENQ) < 0){
        printf("listen socket error: %s(error: %d)\n", strerror(errno), errno);
        return -1;
    }

    socklen_t client_len = sizeof(struct sockaddr_in);
    ssize_t read;
    int i;
    while(1){
        nfds = epoll_wait(epfd, events, MAX_EVENTS, WAIT_TIMEOUT);
        for(i=0; i<nfds; i++){
            if(events[i].data.fd == listenfd){      //有新的链接
                conn_fd = accept(listenfd, (struct sockaddr*)&clientaddr, &client_len);
                if(conn_fd < 0){
                    printf("accept error: %s(error: %d)\n", strerror(errno), errno);
                } else {
                    total_clients++;
                    printf("%d client connected.\n", total_clients);
                    ev.data.fd = conn_fd;
                    ev.events = EPOLLIN;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, conn_fd, &ev);   //新增套接字
                }
            } else if(events[i].events & EPOLLIN){  //可读事件
                if((sock_fd = events[i].data.fd) < 0){
                    continue;
                }
                if((read = recv(sock_fd, buffer, BUFFER_SIZE, 0)) < 0){
                    if(errno == ECONNRESET){
                        total_clients--;
                        printf("one client disconnected while recv.\n");
                    } else {
                        printf("recv error: %s(error: %d)\n", strerror(errno), errno);
                    }
                } else if(read == 0){
                    close(sock_fd);
                    total_clients--;
                    printf("one client disconnected.\n");
                    events[i].data.fd = -1;
                }

                printf("recv message: %s from sock %d\n", buffer, sock_fd);
                ev.data.fd = sock_fd;
                ev.events = EPOLLOUT;
                epoll_ctl(epfd, EPOLL_CTL_MOD, sock_fd, &ev);   //修改监听事件为可读
            } else if(events[i].events & EPOLLOUT){     //可写事件
                sock_fd = events[i].data.fd;
                printf("echo world to client\n");
                send(sock_fd, pong, BUFFER_SIZE, 0);

                ev.data.fd = sock_fd;
                ev.events = EPOLLIN;
                epoll_ctl(epfd, EPOLL_CTL_MOD, sock_fd, &ev);
            }
        }
    }

    return 0;
}
