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

#define BLOK 30720
#define CONS 4435
#define DEG 819
#define BUFF 13312
typedef struct parameters
{
    struct sockaddr_in server_sck;
    float pace_deg;
    float pace_con;
    int capacity;
} parameters;
typedef struct exitParam
{
    double ping;
    double readTime;
    char ip[15];
    unsigned short port;
} exitParam;
void checkPtr(char* endptr, char* str)
{
    if( *endptr )
    {
        printf("Error. Wrong argument (%s) \n", str);
        exit(23);
    }
}
void exitFunction(int status,void* dn)
{
    exitParam* ep = (exitParam*)dn;
    fprintf(stderr,"Opoznienie: %f Czas: %f Ip: %s:%d\n",ep->ping,ep->readTime,ep->ip,ep->port);
    free(dn);
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
            printf("error param %s\n",argv);
            exit(23);
        }
    }
    else
    {
        strcpy(ip,"127.0.0.1");
        *port = strtol(foo,&endptr,10);
        if(*endptr)
        {
            printf("error param %s\n", argv);
            exit(23);
        }
    }
}
void ParseParam(int argc, char* argv[], parameters* p)
{
    char* endptr;
    char c;
    float pace;
    unsigned int mag = 0;
    char ip[15] ={0};
    unsigned int port;
    while((c = getopt(argc, argv, "cpd"))!=-1)
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
                p->pace_con = pace;
                break;
            case 'd':
                pace = strtof(argv[optind],&endptr);
                if(*endptr)
                {
                    printf("wrong param %s\n",argv[optind]);
                    exit(23);
                }
                p->pace_deg = pace;
                break;
            case 'c':
                mag = strtol(argv[optind],&endptr,10);
                if(*endptr)
                {
                    printf("wrong param %s\n",argv[optind]);
                    exit(23);
                }
                p->capacity = mag*BLOK;
                break;
            default:
                printf("Wrong prm \n");
        }
    }
    if(argc==8)
        parseAddr(argv[argc-1],ip,&port);
    else
    {
        printf("too few arguments\n");
        exit(23);
    }
    //printf("ip %s\nport %hd\n", ip, port);
    if (inet_aton(ip, &(p->server_sck.sin_addr)) == 0) {
        printf("error inet_aton parseParam %d\n", errno);
        perror("error inet_aton\n");
        exit(23);
    }
    p->server_sck.sin_port = htons(port);
    p->server_sck.sin_family = AF_INET;
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
void sleepTime(long* s, long *ns, float p, int buf, int cons)
{
    float ss = buf/(cons*p);
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
double getTimestampMono()
{
    struct timespec tms;
    if(clock_gettime(CLOCK_MONOTONIC,&tms))
    {
        printf("error clock\n");
        exit(23);
    }
    double sec = (double)tms.tv_nsec/1000000000;
    double dTime = (double)tms.tv_sec + sec;
    return dTime;
}
int connectTo(parameters* param)
{
    int socket = 0;
    createSocket(&socket);
    if(connect(socket,(struct sockaddr*)&param->server_sck, sizeof(param->server_sck)) <0)
    {
        printf("connect failed\n");
        exit(23);
    }
    return socket;
}
void generateReport()
{
    double ts = getTimestamp();
    fprintf(stderr,"%f %d\n", ts, getpid());
}
void receiveFr(parameters* param)
{
    long occupied = 0;
    char* buf = (char*)calloc(BUFF,sizeof(char));
    struct timespec ts = {0};
    long s,ns;
    s = ns = 0;
    sleepTime(&s,&ns,param->pace_con,BUFF/4,CONS);
    ts.tv_sec=s;
    ts.tv_nsec=ns;
    //printf("%ld %ld\n",s,ns);
    int conNum = 0;
    while( (occupied+BUFF)<=param->capacity)
    {
        double pingTime[2];
        double readTime[2];
        int socket = connectTo(param);
        //exit(23);
        pingTime[0] = getTimestampMono();
        int nb = 0;
        for(int i=0; i<4; i++) {
            double degTimes[2] = {0};
            degTimes[0] = getTimestamp();
            nb = read(socket, buf, BUFF / 4);
            if (nb < 0) {
                printf("error read\n");
                exit(23);
            }
            if(i==0)
            {
                pingTime[1] = getTimestampMono();
                readTime[0] = getTimestamp();
            }
            occupied = occupied + nb;
            nanosleep(&ts,NULL);
            degTimes[1] = getTimestamp();
            double delta = degTimes[1]-degTimes[0];
            if(conNum!=0 || i>0)
            {
                //printf("%f %f\n",delta,(delta*DEG*param->pace_deg));
                occupied = occupied - (int)(delta*DEG*param->pace_deg);
            }
            if(occupied<0)
                occupied=0;
        }
        conNum++;
        readTime[1] = getTimestamp();
        exitParam* exp = (exitParam*)calloc(1,sizeof(exitParam));
        exp->ping = pingTime[1] - pingTime[0];
        exp->readTime = readTime[1] - readTime[0];
        struct sockaddr_in local_sin;
        socklen_t local_sinlen = sizeof(local_sin);
        if(getsockname(socket, (struct sockaddr*)&local_sin, &local_sinlen)==-1)
        {
            printf("error getsockname\n");
            exit(23);
        }
        strcpy(exp->ip,inet_ntoa(local_sin.sin_addr));
        exp->port = local_sin.sin_port;
        close(socket);
        on_exit(exitFunction,(void*)exp);
        //printf("Zajete: %ld\n",occupied);
    }
    generateReport();
}
int main (int argc, char* argv[])
{
    parameters param;
    ParseParam(argc,argv,&param);
    //printf("parametry %f %f %d\n",param.pace_deg,param.pace_con,param.capacity);
    receiveFr(&param);
    return 0;
}