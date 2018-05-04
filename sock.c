#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define MAX_CONN 512            //最大链接数
#define LISTENQ 10              //链接队列长度
#define BUFFER_SIZE 1024        //缓冲区大小
#define PORT 6789               //监听端口
#define IP_ADDR "47.96.18.185"  //监听IP
#define MAX_EVENTS 20           //最大事件数
#define WAIT_TIMEOUT 30         //超时时间

char pong[] = "world";          //回复字符
int total_clients = 0;          //总的链接数

//设置文件描述符为非阻塞模式
int setFdNonBlocking(int fd){
    //读取文件描述符的标识位
    int flags = fcntl(fd, F_GETFL, 0);
    if(flags < 0){
        printf("ERROR: get fd flags error: %s(error: %d)\n", strerror(errno), errno);
        return -1;
    }

    //设置文件描述符的标识位
    flags |= O_NONBLOCK;
    if(fcntl(fd, F_SETFL, flags) < 0){
        printf("ERROR: set fd flags error: %s(error: %d)\n", strerror(errno), errno);
        return -1;
    }

    return 0;
}

//创建socket
int createServerSocket(){
    struct sockaddr_in serveraddr;
    int listenfd;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);     //创建套接字
    if(listenfd < 0){
        printf("ERROR: create socket error: %s(error: %d)\n", strerror(errno), errno);
        return -1;
    }

    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(PORT);

    if(bind(listenfd, (struct sockaddr*) &serveraddr, sizeof(serveraddr)) != 0){
        printf("ERROE: bind socket error: %s(error: %d)\n", strerror(errno), errno);
        return -1;
    }
    if(listen(listenfd, LISTENQ) < 0){
        printf("ERROR: listen socket error: %s(error: %d)\n", strerror(errno), errno);
        return -1;
    }

    return listenfd;
}

int main(){
    struct epoll_event ev, events[MAX_EVENTS];
    int epfd, listenfd, acceptfd, sock_fd, conn_fd, nfds, i;
    struct sockaddr_in clientaddr;
    socklen_t client_len = sizeof(struct sockaddr_in);
    ssize_t read;
    char buffer[BUFFER_SIZE];

    listenfd = createServerSocket();
    if(listenfd < 0){
        return -1;
    }
    //把socket文件描述符设置为非阻塞模式
    if(setFdNonBlocking(listenfd) < 0){
        return -1;
    }

    epfd = epoll_create(MAX_CONN);                  //生成epoll句柄
    if(epfd < 0){
        printf("ERROR: create epoll instance error: %s(error: %d)\n", strerror(errno), errno);
        return -1;
    }

    ev.data.fd = listenfd;
    ev.events = EPOLLIN | EPOLLET;                  //对应的文件描述符可以读 | 开启边缘触发
    epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev);  //注册epoll事件

    memset(&buffer, 0, sizeof(buffer));
    printf("INFO: server is ready...\n");
    while(1){
        nfds = epoll_wait(epfd, events, MAX_EVENTS, WAIT_TIMEOUT);
        for(i=0; i<nfds; i++){
            if(events[i].data.fd == listenfd){      //有新的链接
                conn_fd = accept(listenfd, (struct sockaddr*)&clientaddr, &client_len);
                if(conn_fd < 0){
                    printf("ERROR: accept error: %s(error: %d)\n", strerror(errno), errno);
                } else {
                    if(setFdNonBlocking(conn_fd) < 0){      //边缘触发模式，如果不设置成非阻塞模式，可能会导致进程饿死，所以直接close
                        close(conn_fd);
                        continue;
                    }

                    total_clients++;
                    printf("INFO: sock %d connected.\n", conn_fd);
                    ev.data.fd = conn_fd;
                    ev.events = EPOLLIN | EPOLLET;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, conn_fd, &ev);   //新增套接字
                }
            } else if(events[i].events & EPOLLIN){  //可读事件
                if((sock_fd = events[i].data.fd) < 0){
                    continue;
                }

                if((read = recv(sock_fd, buffer, BUFFER_SIZE, 0)) > 0){    //链接已中止或返回错误
                    printf("INFO: recv message: %s from sock %d\n", buffer, sock_fd);

                    if(setFdNonBlocking(sock_fd) == 0){      //边缘触发模式，如果不设置成非阻塞模式，可能会导致进程饿死，所以直接close
                        ev.data.fd = sock_fd;
                        ev.events = EPOLLOUT | EPOLLET;
                        epoll_ctl(epfd, EPOLL_CTL_MOD, sock_fd, &ev);   //修改监听事件为可读
                        continue;
                    }
                }

                printf("ERROR: recv error: %s(error: %d)\n", strerror(errno), errno);
                epoll_ctl(epfd, EPOLL_CTL_DEL, sock_fd, &ev);   //有链接失败的，就删除
                close(sock_fd);
                total_clients--;
                events[i].data.fd = -1;
            } else if(events[i].events & EPOLLOUT){     //可写事件
                sock_fd = events[i].data.fd;
                if((sock_fd = events[i].data.fd) < 0){
                    continue;
                }

                if(setFdNonBlocking(sock_fd) == 0){      //边缘触发模式，如果不设置成非阻塞模式，可能会导致进程饿死，所以直接close
                    printf("INFO: echo world to sock %d\n", sock_fd);
                    send(sock_fd, pong, BUFFER_SIZE, 0);

                    ev.data.fd = sock_fd;
                    ev.events = EPOLLIN | EPOLLET;
                    epoll_ctl(epfd, EPOLL_CTL_MOD, sock_fd, &ev);
                } else {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, sock_fd, &ev);   //有链接失败的，就删除
                    close(sock_fd);
                    total_clients--;
                    events[i].data.fd = -1;
                }
            } else if(events[i].events & EPOLLERR || events[i].events & EPOLLHUP){  //监控到错误
                printf("ERROR: epoll error:%s(error: %d)\n", strerror(errno), errno);
                sock_fd = events[i].data.fd;
                if(sock_fd > 0){
                    epoll_ctl(epfd, EPOLL_CTL_DEL, sock_fd, &ev);   //有链接失败的，就删除
                    close(sock_fd);
                    events[i].data.fd = -1;
                    if(sock_fd != listenfd){
                        total_clients--;
                    }
                }
            }
        }
    }

    close(epfd);

    return 0;
}
