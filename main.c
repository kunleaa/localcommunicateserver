#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>

#ifndef OPEN_MAX
#define OPEN_MAX 1024
#endif

#define MAXLINE 4096
#define EVENT_MAX 20

typedef struct tagIpAndPort
{
	int iIp;
	int iPort;
}IpAndPort;

typedef struct tagEvent_Data{
	int fd;
	int offset;
	char buf[MAXLINE];
}Event_Data;

void set_event_d(int fd, struct epoll_event *evt, Event_Data *met)
{
	met->fd = fd;
	met->offset = 0;
	memset(&met->buf, 0, sizeof met->buf);
	evt->data.ptr = met;
}

int GetIpAndPort(IpAndPort* pstIpPort)
{
	int port = 0;
	int ip[4];
	int iptemp = 0;
	FILE *fp=NULL;

	fp = fopen("IpAndPort.d","rt");
	if(fp == NULL)
	{
		printf("can not open IpandPort.d\n");
		return -1;
	}

	fscanf(fp,"IP %d.%d.%d.%d\n",&ip[0],&ip[1],&ip[2],&ip[3]);
	fscanf(fp,"PORT %d\n",&port);
	printf("IP %d.%d.%d.%d\n",ip[0],ip[1],ip[2],ip[3]);
	printf("PORT %d\n",port);

	if(port<0)
	{
		perror("Usage:Port is useless\n");
		return -1;
	}

	iptemp = ip[0]<<12+ip[1]<<8+ip[2]<<4+ip[3];
	pstIpPort->iIp = iptemp;
	pstIpPort->iPort = port;

	fclose(fp);
	return 0;
}

int SetSocket(IpAndPort *pstIpPort)
{
	
	int sockfd = 0;
	int addr_len = 0;
	struct sockaddr_in server_addr;
	unsigned long int host = pstIpPort->iIp;
	int portnumber =pstIpPort->iPort;

	sockfd = socket(AF_INET,SOCK_STREAM,0);
	//设置lisenfd非阻塞
	fcntl(sockfd, F_SETFL, O_NONBLOCK);
	
	if(-1 == sockfd)
	{
		perror("Socket Error:");
		return -1;
	}
	memset(&server_addr,0,sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(portnumber);
	server_addr.sin_addr.s_addr = htonl(host);

	if(-1 == bind(sockfd,(struct sockaddr*)(&server_addr),sizeof(struct sockaddr)))
	{
		perror("Bind error:");
		return -1;
	}

	if(-1 == listen(sockfd,1024))
	{
		perror("Listen error:");
		return -1;
	}

	return sockfd;
}

int ProcSocket(int listenfd)
{
	int epfd = 0;
	struct epoll_event evt,events[EVENT_MAX];
	Event_Data stEvent_d[OPEN_MAX];

	struct sockaddr_in client_addr;
	unsigned int addr_len = 0;
	int clientfd = 0;
	int i,j,nready,n,wpos;
	epfd=epoll_create(256);
	set_event_d(listenfd,&evt,&stEvent_d[0]);
	evt.events = EPOLLIN | EPOLLET;

	epoll_ctl(epfd,EPOLL_CTL_ADD,listenfd,&evt);
	for(i = 1; i < OPEN_MAX;i++) stEvent_d[i].fd=-1;

	for(;;)
	{
		nready = epoll_wait(epfd,events,EVENT_MAX,-1);
		for(i = 0; i < nready; i++)
		{
			//读取存储描述符信息的指针
			Event_Data *pstED = (Event_Data*)events[i].data.ptr;
			if (pstED->fd == listenfd)
			{
				//ET模式下存在多个client connect只通知一次的情况,需要循环accept直到读到EAGAIN
				for(;;)
				{
					addr_len = sizeof(struct sockaddr_in);

					if(-1 == (clientfd = accept(listenfd,(struct socketaddr *)(&client_addr),&addr_len)))
					{
						if(errno == EAGAIN)
							break;
						else
							perror("Accept error:");
					}
					else
					{
						printf("Connect from %s:%u ...!\n",(char*)inet_ntoa(client_addr.sin_addr),ntohs(client_addr.sin_port));
					}
					//找到可用的event_d来存放event.data
					for (j = 1; j < OPEN_MAX; j++) 
					{
						if (stEvent_d[j].fd == -1) 
							break;
					}
					if (j == OPEN_MAX) {
						perror("too many clients");
						break;
					}
					//设置客户端fd非阻塞
					fcntl(clientfd, F_SETFL, O_NONBLOCK);
					set_event_d(clientfd,&evt,&stEvent_d[j]);
					evt.events = EPOLLIN | EPOLLET;
					epoll_ctl(epfd, EPOLL_CTL_ADD, clientfd, &evt);
				}
			}
			else if(events[i].events & EPOLLIN)
			{
				//ET模式，重复读直到EAGAIN说明无数据可读或者读到错误及EOF
				for (;pstED->offset < MAXLINE;) {
					n = read(pstED->fd, pstED->buf + pstED->offset, MAXLINE - pstED->offset);
					if (n <= 0) {
						if (errno == EAGAIN) break;
						if (errno == EINTR) continue;
						close(pstED->fd);
						pstED->fd = -1;
						break;
					}
					pstED->offset += n;
				}
				printf("Date is:\n%s\n",pstED->buf);
				//close(pstED->fd);
				//修改为监听描述符写就绪
				evt.events = EPOLLOUT | EPOLLET;
				evt.data.ptr = pstED;
				epoll_ctl(epfd, EPOLL_CTL_MOD, pstED->fd, &evt);
			}
			else if(events[i].events & EPOLLOUT)
			{
				wpos = 0;
				//ET模式下，重复写直到无数据可发或者EAGAIN
				for (;wpos < pstED->offset;) 
				{
					n = write(pstED->fd, pstED->buf + wpos, pstED->offset - wpos);
					if (n < 0) 
					{
						if (errno == EAGAIN) break;
						if (errno == EINTR) continue;
						close(pstED->fd);
						pstED->fd = -1;
						break;
					}
					wpos += n;
				}
				pstED->offset = 0;
				//修改为监听描述符读就绪
				evt.events = EPOLLIN | EPOLLET;
				evt.data.ptr = pstED;
				epoll_ctl(epfd, EPOLL_CTL_MOD, pstED->fd, &evt);
																																						}
		}

	}
	close(listenfd);
}

void ParsePacket()
{

}

void SendPacket()
{

}

void LoginAndLogout()
{

}


void RegistNewUser()
{

}
int main(int argc,char*argv[])
{
	int sockfd = 0;	
	IpAndPort stIpPort;

	if(-1 == GetIpAndPort(&stIpPort))
	{
		return -1;
	}

	sockfd = SetSocket(&stIpPort);
	if(-1 == sockfd)
	{
		return -1;
	}

	if(-1 == ProcSocket(sockfd))
	{
		return -1;
	}
	return 0;
}
