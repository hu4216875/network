#include<stdio.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<string.h>
#include<unistd.h>
#include<stdlib.h>
#include<arpa/inet.h>
#include<sys/epoll.h>
#include <errno.h>

#define LISTEN_BACK_LOG 100
#define LISTEN_PORT 8000
#define BUFFER_SIZE 1024
#define MAX_EVETNS 1024
#define MAX_SESSION 1000

#define MAX_WRITE_BUFF 1000


class Session 
{
public:
    Session()
    {
        m_writeFlag = 0;
        m_readIndex = 0;
        m_writeIndex = 0;
        m_isUse = false;
    }

    void Init(struct sockaddr_in* clientAddr, int epollfd, int fd)
    {
        printf("conn client:%s\n", inet_ntoa(clientAddr->sin_addr));
        m_writeFlag = 0;
        m_readIndex = 0;
        m_writeIndex = 0;
        m_isUse = true;
        m_epollfd = epollfd;
        m_fd = fd;

        struct epoll_event event;
        event.events = EPOLLIN;
        event.data.fd = fd;
        epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    }

    int GetFd() 
    {
        return m_fd;
    }

    bool GetUse()
    {
        return m_isUse;
    }

   void SessionRead(char* buffer) {
        printf("fd:%d, recv:%s\n", m_fd, buffer);
        char writeBuffer[100];
        memset(writeBuffer, 0, sizeof(writeBuffer));
        sprintf(writeBuffer, "%s", "hello, world-----");
        SessionWrite(writeBuffer, strlen(writeBuffer));
    }

    int SessionWrite(char* buffer, int len) 
    {
        if(canWrite(len)<0){
            printf("fd:%d buff full\n", m_fd);
            return -1;
        }
        writeBuffer(buffer, len);
        if(!m_writeFlag) {
            struct epoll_event event;
            event.events =  EPOLLIN | EPOLLOUT;
            event.data.fd = m_fd;
            int ret = epoll_ctl(m_epollfd, EPOLL_CTL_MOD, m_fd, &event);
            m_writeFlag = 1;
        }
        return 0;
    }

    void SendData() 
    {
        if(m_readIndex < m_writeIndex) {
            int len = m_writeIndex - m_readIndex;
            int nWriteLen = send(m_fd, m_writeBuffer + m_readIndex, len, 0);
            m_readIndex += nWriteLen;
        } else {
            int len1 = MAX_WRITE_BUFF - m_writeIndex;
            int writeLen1 = send(m_fd, m_writeBuffer + m_readIndex, len1, 0);
            m_readIndex += writeLen1 % MAX_WRITE_BUFF;
            if(writeLen1==len1) {
                int writeLen2 = send(m_fd, m_writeBuffer,m_writeIndex, 0);
                m_readIndex = writeLen2;
            }
        }

        if(m_readIndex==m_writeIndex) {
            struct epoll_event event;
            event.events = EPOLLIN;
            event.data.fd = m_fd;
            epoll_ctl(m_epollfd, EPOLL_CTL_MOD, m_fd, &event);
            m_writeFlag = 0;
        }
    }

    void Release() 
    {
        if(m_fd > 0) {
            struct epoll_event event;
            event.events = EPOLLIN;
            event.data.fd = m_fd;
            epoll_ctl(m_epollfd, EPOLL_CTL_DEL, m_fd, &event);
            close(m_fd);
            m_isUse = false;
        }
    }
private:
    Session(const Session&);
    Session operator=(const Session&);

    bool canWrite(int len) {
        int use = (MAX_WRITE_BUFF + m_writeIndex - m_readIndex) % MAX_WRITE_BUFF;
        if (len > (MAX_WRITE_BUFF - 1 - use)) {
            return -1;
        }
        return 0;
    }

    void writeBuffer(char* buffer, int len) 
    {
        int maxIndex = MAX_WRITE_BUFF - 1;
        int temp = maxIndex - m_writeIndex + 1;
        if(len < temp) {
            memcpy(m_writeBuffer + m_writeIndex, buffer, len);
        } else {
            memcpy(m_writeBuffer + m_writeIndex, buffer, temp);
            memcpy(m_writeBuffer, buffer + temp, len - temp);
        }
        m_writeIndex = (m_writeIndex + len) % MAX_WRITE_BUFF;
    }

private:
    int m_epollfd;
    int m_fd;
    bool m_isUse;
    int m_writeFlag;
    char m_writeBuffer[MAX_WRITE_BUFF];
    int m_writeIndex;
    int m_readIndex;
};


class SessionMgr {
public:
    int CreateSession(int epollfd, int fd, struct sockaddr_in* clientAddr) 
    {
        for(int i=0;i<MAX_SESSION;i++) 
        {
            Session* session = &m_sessions[i];
            if(!session->GetUse()) 
            {
                session->Init(clientAddr, epollfd, fd);
                return 0;
            }
        }
        return 1;
    }

    Session* GetSession(int fd)
    {
        for(int i=0;i<MAX_SESSION;i++) {
            Session* session = &m_sessions[i];
            if(session->GetFd()==fd) {
                return session;
            }
        }
        return nullptr;
    }

    void DestorySession(int fd)
    {
        Session* session = GetSession(fd);
        if(session)
        {
            session->Release();
        }
    }

private:
    Session m_sessions[MAX_SESSION];
};

class Reactor
{
public:
    int Init()
    {
        m_listenfd = socket(AF_INET, SOCK_STREAM, 0);
        if(m_listenfd==0) {
            printf("socket err:%d\n", errno);
            return -1;
        }

        struct sockaddr_in myaddr;
        memset(&myaddr, 0, sizeof(myaddr));
        myaddr.sin_family = AF_INET;
        myaddr.sin_port = htons(LISTEN_PORT);
        myaddr.sin_addr.s_addr = INADDR_ANY;

        int err =  0;
        err = bind(m_listenfd, (struct sockaddr *)&myaddr, sizeof(myaddr));
        if(err!=0) {
            printf("bind error:%d\n", err);
            return -1;
        }

        err = listen(m_listenfd, LISTEN_BACK_LOG);
        if(err!=0) {
            printf("listen err:%d\n", err);
            return -1;
        }

        m_epollfd = epoll_create(1);
        struct epoll_event event;
        struct epoll_event ev;
        struct epoll_event events[MAX_EVETNS];

        event.events = EPOLLIN;
        event.data.fd = m_listenfd;
        epoll_ctl(m_epollfd, EPOLL_CTL_ADD, m_listenfd, &event);

        printf("server listen:%d...\n", LISTEN_PORT);
        return 0;

    }

    int Loop()
    {
        struct epoll_event events[MAX_EVETNS];
        while(1) 
        {
            int nReady = epoll_wait(m_epollfd, events, MAX_EVETNS, -1);
            if(nReady<0) {
                printf("epoll_wait err:%d errno:%d\n", nReady, errno);
                break;
            }
            for(int i=0;i<nReady;i++){
                int fd = events[i].data.fd;
                if((events[i].events & EPOLLIN) > 0) {
                    if(fd==m_listenfd) {
                        printf("new client come \n");
                        struct sockaddr_in clientAddr;
                        socklen_t len = sizeof(clientAddr);
                        int connfd = 0;
                        if ((connfd = accept(m_listenfd, (struct sockaddr *)&clientAddr, &len)) == -1) {
                            printf("accept socket error: %s(errno: %d)\n", strerror(errno), errno);
                            return 0;
                        }

                        int ret = m_sessionMgr.CreateSession(m_epollfd, connfd, &clientAddr);
                        if(ret>0){
                            printf("no session use\n");
                        }
                    } else {
                        char buff[BUFFER_SIZE];
                        int nRead = recv(fd, buff, BUFFER_SIZE-1, 0);
                        Session * session = m_sessionMgr.GetSession(fd);
                        if(session) 
                        {
                            if(nRead>0) 
                            {
                                session->SessionRead(buff);
                            }
                            else if(nRead==0)
                            {
                                session->Release();
                            } else {
                                printf("recv fd:%d, err:%d\n", fd, nRead);
                            }
                        }
                    }
                }
                if((events[i].events & EPOLLOUT) > 0) {
                    Session * session = m_sessionMgr.GetSession(fd);
                    if(session) 
                    {
                        session->SendData();
                    }
                    else
                    {
                        printf("fd:%d session not exist", fd);
                    }
                }
            }   
        }
        return 0;
    }

    void Destory()
    {
        close(m_listenfd);
    }

private:
    int m_epollfd;
    int m_listenfd;
    SessionMgr m_sessionMgr;
};




int main() {
   Reactor reactor;

   reactor.Init();
   reactor.Loop();
   reactor.Destory();
   return 0;
}