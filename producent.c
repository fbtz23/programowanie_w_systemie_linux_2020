#define _GNU_SOURCE
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <sys/poll.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

#define BLOK 640
#define JEDN 2662
#define MAXEV 1024
#define BUFF 13312
typedef struct parameters
{
    struct sockaddr_in server_sck;
    float pace;
} parameters;
typedef struct clientData
{
    int fd;
    int bytesSend;
    char ip[15];
    unsigned short port;
} clientData;

struct cBuffer
{
    int first;
    int last;
    int size;
    int currentSize;
    int* buffer;
};
struct cBuffer* create(int size)
{
    struct cBuffer* buffer = (struct cBuffer*)calloc(1,sizeof(struct cBuffer));
    buffer->size = size;
    buffer->buffer = (int*)calloc(size,sizeof(int));
    buffer->currentSize = 0;
    buffer->first = 0;
    buffer->last = 0;
    return buffer;
}
void destroy(struct cBuffer* buffer)
{
    free(buffer->buffer);
    free(buffer);
}
int add(struct cBuffer* buffer, int element)
{
    if (buffer->currentSize == buffer->size)
    {
        perror("No more place in a buffer!\n");
        return -1;
    }
    buffer->buffer[buffer->last] = element;
    buffer->last = (buffer->last+1)%(buffer->size);
    buffer->currentSize++;
    return 0;
}
int pop(struct cBuffer* buffer)
{
    int temp = buffer->buffer[buffer->first];
    buffer->first = (buffer->first+1)%buffer->size;
    buffer->currentSize--;

    return temp;
}

void parseAddr(char* argv, char* ip, unsigned int* port)
{
    char *saveptr;
    char *foo, *bar;
    char* endptr;
    foo = strtok_r(argv, "[]:", &saveptr);
    if(foo==NULL)
    {
        printf("error param \n");
        exit(23);
    }
    bar = strtok_r(NULL, "[]:", &saveptr);
    if(bar!= NULL)
    {
        strcpy(ip,foo);
        *port = strtol(bar,&endptr,10);
        if(*endptr)
        {
            printf("error param \n");
            exit(23);
        }
    }
    else
    {
        strcpy(ip,"127.0.0.1");
        *port = strtol(foo,&endptr,10);
        if(*endptr)
        {
            printf("error param \n");
            exit(23);
        }
    }
}
int checkBytesAvailable(int fd)
{
    int available = 0;
    if (ioctl(fd, FIONREAD, &available) == -1) {
        printf("error ioctl checkbytes\n");
        exit(23);
    }
    return available;
}
void ParseParam(int argc, char* argv[], parameters* p)
{
    char* endptr;
    char c;
    float pace;
    char ip[15] ={0};
    unsigned int port;
    while((c = getopt(argc, argv, "p"))!=-1)
    {
        switch(c)
        {
            case 'p':
                pace = strtof(argv[optind],&endptr);
                if(*endptr)
                {
                    printf("wrong param %s\n",argv[optind]);
                    exit(23);
                }
                p->pace = pace;
                break;
            default:
                printf("Wrong prm \n");
        }
    }
    optind++;
    if(optind<argc)
        parseAddr(argv[optind],ip,&port);
    else
    {
        printf("too few arguments\n");
        exit(23);
    }
    //printf("argv: %s\nip \n%s\nport \n%hd\n",argv[optind], ip, port);
    if (inet_aton(ip, &(p->server_sck.sin_addr)) == 0) {
            printf("error inet_aton parseParam %d\n", errno);
            perror("error inet_aton\n");
            exit(23);
    }
    p->server_sck.sin_port = htons(port);
    p->server_sck.sin_family = AF_INET;
}
void NextChar(char* c)
{
    if((*c > 64 && *c < 90) || (*c >96 && *c < 122))
        *c = *c+1;
    else if(*c == 122)
        *c=65;
    else
        *c=97;
}
void sleepTime(long* s, long *ns, float p)
{
    float ss = BLOK/(JEDN*p);
    while(ss!=0)
    {
        if(ss>=1)
        {
            *s +=1;
            ss-=1;
        }
        else
        {
            *ns = ss*1000000000;
            ss=0;
        }
    }
}
void childFun(int fd, parameters* param)
{
    struct timespec ts;
    long s = 0;
    long ns = 0;
    sleepTime(&s,&ns,param->pace);
    ts.tv_sec=s;
    ts.tv_nsec=ns;
    printf("%ld %ld\n",ts.tv_sec,ts.tv_nsec);
    char c = 'a';
    int pipeCapacity =  fcntl(fd, F_GETPIPE_SZ);
    char* buf = (char*)calloc(BLOK,sizeof(char));
    memset(buf,c,BLOK);
    NextChar(&c);
    int available = checkBytesAvailable(fd);
    while(1)
    {
        while((pipeCapacity-available) >= BLOK)
        {
            if(write(fd,buf,BLOK)==-1)
            {
                    close(fd);
                    printf("End EPIPE \n");
                    free(buf);
                    exit(23);
            }
            memset(buf,c,BLOK);
            NextChar(&c);
            nanosleep(&ts,NULL);
            available = checkBytesAvailable(fd);
        }
    }
}
void epollCreate(int* epoll_fd, int socketserver, int timer_fd)
{
    struct epoll_event event ={0};
    clientData* server = (clientData*)calloc(1,sizeof(clientData));
    server->fd = socketserver;
    event.events = EPOLLIN;
    event.data.ptr = server;
    *epoll_fd = epoll_create1(0);
    if(*epoll_fd == -1)
    {
        printf("error epoll create1\n");
        exit(23);
    }
    if(epoll_ctl(*epoll_fd, EPOLL_CTL_ADD, socketserver ,&event))
    {
        printf("error epoll_ctl1 \n");
        close(*epoll_fd);
        exit(23);
    }
    clientData* timer = (clientData*)calloc(1,sizeof(clientData));
    timer->fd = timer_fd;
    event.events = EPOLLIN | EPOLLET;
    event.data.ptr = timer;
    if(epoll_ctl(*epoll_fd, EPOLL_CTL_ADD, timer_fd ,&event))
    {
        printf("error epoll_ctl2 \n");
        close(*epoll_fd);
        exit(23);
    }
}
void createSocket(int* desc)
{
    *desc = socket(AF_INET, SOCK_STREAM,0);
    if(*desc == -1)
    {
        printf("cannot create socket\n");
        exit(23);
    }
}
void createServer(parameters* param, int* socket_desc)
{
    createSocket(socket_desc);
    if(bind(*socket_desc,(struct sockaddr *)&param->server_sck,sizeof(param->server_sck))<0)
    {
        printf("error bind %d\n", errno);
        perror("error bind\n");
        exit(23);
    }
    //printf("bind done\n");
    if(listen(*socket_desc,MAXEV) < 0)
    {
        printf("listen error\n");
        exit(23);
    }
    //printf("listen done\n");
}
double getTimestamp()
{
    struct timespec tms;
    if(clock_gettime(CLOCK_REALTIME,&tms))
    {
        printf("error clock\n");
        exit(23);
    }
    double sec = (double)tms.tv_nsec/1000000000;
    double dTime = (double)tms.tv_sec + sec;
    return dTime;
}
void createAndSetTimer(int* timerfd)
{
    *timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if(*timerfd == -1)
    {
        printf("error timerfdcreate\n");
        exit(23);
    }
    struct itimerspec ts;
    ts.it_value.tv_sec = 5;
    ts.it_value.tv_nsec = 0;
    ts.it_interval.tv_sec = 5;
    ts.it_interval.tv_nsec = 0;
    if(timerfd_settime(*timerfd,0,&ts,NULL)<0)
    {
        printf("error timerfdsettime\n");
        exit(23);
    }
}
void disconnectClient(clientData* client,int* clientsNumber,volatile int* reservedBytes, int epoll_fd, struct epoll_event* event, int fd)
{
    char* buf = (char*)calloc(1,BUFF);
    fprintf(stderr,"%f %s:%d %d\n",getTimestamp(),client->ip,client->port,BUFF-client->bytesSend);
    *reservedBytes= *reservedBytes - (BUFF-client->bytesSend);
    *clientsNumber = *clientsNumber-1;
    if(client->bytesSend>0)
        if(read(fd,buf,(BUFF-client->bytesSend)) == -1)
        {
            printf("error read pipe\n");
            free(event->data.ptr);
            free(buf);
            exit(23);
        }
    if(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->fd ,event))
    {
        printf("error epoll_ctl \n");
        close(epoll_fd);
        free(event->data.ptr);
        free(buf);
        exit(23);
    }
    close(client->fd);
    free(event->data.ptr);
    free(buf);
}
void sendToClient(clientData* client, volatile int* reservedBytes, int fd,int epoll_fd, struct epoll_event* event, int* clientsNumber)
{
    char* buf = (char*)calloc(1,BUFF);
    if(read(fd,buf,BUFF/4) == -1)
    {
        printf("error read pipe\n");
        exit(23);
    }
    if(write(client->fd,buf,BUFF/4)==-1)
    {
        disconnectClient(client,clientsNumber,reservedBytes,epoll_fd,event,fd);
    }
    *reservedBytes-=(BUFF/4);
    //printf("%d \n",*reservedBytes);
    client->bytesSend+=(BUFF/4);
    //printf("bytes send to: %d %d\n",client->bytesSend,client->fd);
    if(client->bytesSend==BUFF)
    {
        disconnectClient(client,clientsNumber,reservedBytes,epoll_fd,event,fd);
    }
    free(buf);
}
void parentFun(int fd, parameters* param)
{
    uint64_t tempbuf;
    struct epoll_event events[MAXEV] = {0};
    struct epoll_event event;
    int epoll_fd;
    int clientsNumber = 0;
    int event_count = 0;
    int socketserver,new_socket = 0;
    volatile int reservedBytes = 0;
    createServer(param,&socketserver);
    int timer_fd = 0;
    long flow = 0;
    createAndSetTimer(&timer_fd);
    struct cBuffer* sockets = create(MAXEV);
    epollCreate(&epoll_fd, socketserver, timer_fd);
    //printf("timer %d\n%ld \n%ld \n%ld\n %ld\n",timer_fd, test.it_interval.tv_sec, test.it_interval.tv_nsec, test.it_value.tv_sec, test.it_value.tv_nsec);
    while(1)
    {
        while( (checkBytesAvailable(fd)-reservedBytes >= BUFF) && (sockets->currentSize >0)) //gdy klient jest w buforze i mam dla niego dane
        {
            reservedBytes+=BUFF;
            int outFd = pop(sockets);
            printf("Z bufora wychodzi: %d\n\n",outFd);
            clientData* client = (clientData*)calloc(1,sizeof(clientData));
            client->fd=outFd;
            client->bytesSend=0;
            struct sockaddr_in local_sin;
            socklen_t local_sinlen = sizeof(local_sin);
            if(getpeername(outFd, (struct sockaddr*)&local_sin, &local_sinlen)==-1)
            {
                printf("error getppername\n");
                exit(23);
            }
            strcpy(client->ip,inet_ntoa(local_sin.sin_addr));
            client->port = local_sin.sin_port;
            event.events = EPOLLOUT | EPOLLRDHUP;
            event.data.ptr = client;
            if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, outFd ,&event))
            {
                printf("error epoll_ctl \n");
                close(epoll_fd);
                exit(23);
            }
        }
        event_count = epoll_wait(epoll_fd,events,MAXEV, 0);
        for(int i=0; i<event_count; i++)
        {
            clientData* tmp = (struct clientData*)events[i].data.ptr;
            if(tmp->fd == socketserver)
            {
                new_socket = accept4(socketserver,NULL, NULL, SOCK_NONBLOCK); //nowy klient
                //printf("accept done %d\n", new_socket);
                if(event_count == MAXEV) //gdy jest maksymalna ilosc klientow
                    close(new_socket);
                else if ( (checkBytesAvailable(fd)-reservedBytes) >= BUFF && (sockets->currentSize==0)) //gdy klient moze dostac paczke i nie ma nikogo w kolejce
                {
                    clientsNumber++;
                    reservedBytes+=BUFF;
                    clientData* client = (clientData*)calloc(1,sizeof(clientData));
                    client->fd=new_socket;
                    client->bytesSend=0;
                    struct sockaddr_in local_sin;
                    socklen_t local_sinlen = sizeof(local_sin);
                    getpeername(new_socket, (struct sockaddr*)&local_sin, &local_sinlen);
                    strcpy(client->ip,inet_ntoa(local_sin.sin_addr));
                    client->port = local_sin.sin_port;
                    event.events = EPOLLOUT | EPOLLRDHUP;
                    event.data.ptr = client;
                    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_socket ,&event))
                    {
                        printf("error epoll_ctl \n");
                        close(epoll_fd);
                        exit(23);
                    }
                }
                else //brakuje danych dla klienta, wiec wchodzi do bufora cyklicznego
                {
                    //printf("do bufora trafia: %d %d\n",new_socket, reservedBytes);
                    clientsNumber++;
                    add(sockets,new_socket);
                }
            }
            else if(tmp->fd == timer_fd && events[i].events & EPOLLIN) //obsluga zegara z raportami co 5sek
            {
                if(read(timer_fd,&tempbuf,sizeof(uint64_t)) == -1)
                {
                    printf("err read\n");
                }
                fprintf(stderr,"Ilosc klientow: %d Zajetosc magazynu: %d %.2f%% Przeplyw: %ld\n",clientsNumber,checkBytesAvailable(fd),(double)checkBytesAvailable(fd)/fcntl(fd, F_GETPIPE_SZ)*100, checkBytesAvailable(fd)-flow);
                flow = checkBytesAvailable(fd);
            }
            else
            {
                if(events[i].events & EPOLLRDHUP || events[i].events & EPOLLERR) //rozlaczenie
                {
                    disconnectClient(tmp,&clientsNumber,&reservedBytes,epoll_fd,&events[i],fd);
                }
                else if(events[i].events & EPOLLOUT) //odebranie i wyslanie do klienta
                {
                    sendToClient(tmp,&reservedBytes,fd,epoll_fd,&events[i],&clientsNumber);
                }
            }
        }
    }
    close(epoll_fd);
}
void CreateChild(int* fd, parameters* param)
{
    if(pipe(fd)==-1)
    {
        printf("Error pipe\n");
        close(fd[0]);
        close(fd[1]);
        exit (23);
    }
    pid_t pid = fork();
    switch (pid) {
        case -1:
            perror("");
            printf("fork\n");
            return;
        case 0:
            close(fd[0]);
            childFun(fd[1], param);
            exit(0);
        default:
            close(fd[1]);
            parentFun(fd[0], param);
            break;
    }
}
int main (int argc, char* argv[])
{
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
    parameters param;
    param.pace = 1;
    memset(&param.server_sck, 0, sizeof(struct sockaddr_in));
    int magazyn[2];
    ParseParam(argc,argv,&param);
    CreateChild(magazyn,&param);
}