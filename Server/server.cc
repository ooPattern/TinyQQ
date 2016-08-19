/*
 * server.cc
 *
 *  Created on: Aug 4, 2016
 *      Author: root
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <assert.h>
#include "server.h"

//构造函数
CTcpServer::CTcpServer()
{
    //初始化变量
    bzero(&_acceptChn, sizeof(_acceptChn));
    _connChn.clear();
    _epollFd = -1;
    //必须有初始值,不能为空,该参数用作epoll_wait接口的参数,必须预先分配内存
    _eventList.resize(INIT_EVENT_SIZE);
    //初始化连接成功回调函数和消息处理回调函数
    _connCb = NULL;
    _msgCb = NULL;
}

//析构函数
CTcpServer::~CTcpServer()
{
    
}

//启动服务器端口监听
int CTcpServer::StartServer(unsigned short port)
{
	int socket = -1;

	//初始化服务器,监听端口
	if((socket = InitServer(port)) < 0)
	{
		printf("InitServer() return err\n");
		return -1;
	}
	//初始化事件epoll对象
	if(InitEvent() < 0)
	{
		printf("InitEvent() return err\n");
		return -1;
	}
	//注册监听套接字到epoll对象中
	_acceptChn._fd = socket;
	_acceptChn._events = EPOLLIN | EPOLLPRI;
	//绑定回调函数,在处理事件HandleEvent接口根据对象选择对应的事件回调函数
	_acceptChn._readCb = std::bind(&CTcpServer::AcceptRegister, this);
	_acceptChn._writeCb = NULL;
	_acceptChn._errorCb = NULL;
	_acceptChn._closeCb = NULL;
	if(ModifyEvent(EPOLL_CTL_ADD, _acceptChn) < 0)
	{
		printf("ModifyEvent() return err\n");
		return -1;
	}

	return 0;
}

//初始化服务器,监听该端口的连接
int CTcpServer::InitServer(unsigned short port)
{
	int sockfd = -1;
    struct sockaddr_in sa;
    
    //初始化服务器端口
    bzero(&sa, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);

    if((sockfd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0)) < 0)
    {
        printf("socket() return err\n");
        return -1;
    }
    //设置套接字为非阻塞模式
    if(SetSocketNonBlock(sockfd) != 0)
    {
    	close(sockfd);
    	printf("SetSocketNonBlock() err\n");
    	return -1;
    }
    //设置keepalive和地址重用
    int on = 1;
    if((setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE|SO_REUSEADDR,
                    (void*)&on, sizeof(on))) < 0)
    {
        close(sockfd);
        printf("setsockopt return err\n");
        return -1;
    }
    //绑定监听地址
    if(bind(sockfd, (struct sockaddr*)&sa, sizeof(sa)) < 0)
    {
        close(sockfd);
        printf("bind return err\n");
        return -1;
    }
    //开始监听端口
    if(listen(sockfd, 0) < 0)
    {
        close(sockfd);
        printf("listen() return err\n");
        return -1;
    }

    return sockfd;
}

//设置套接字为非阻塞模式
int CTcpServer::SetSocketNonBlock(int socket)
{
	int flags = 0;

	if((flags = fcntl(socket, F_GETFL, NULL)) < 0)
	{
		printf("fcntl get flags err\n");
		return -1;
	}
	flags |= O_NONBLOCK;
	if(fcntl(socket, F_SETFL, flags) < 0)
	{
		printf("fcntl set nonblock flag err\n");
		return -1;
	}

	return 0;
}

//设置连接成功调用的回调函数,在端口接受新连接会调用
void CTcpServer::SetConnectCallback(CONNECTION_CALLBACK_FUNC& cb)
{
	_connCb = cb;
}

//消息处理调用的回调函数,在服务器接收到客户端的数据后会调用
void CTcpServer::SetMessageCallback(CALLBACK_FUNC& cb)
{
	_msgCb = cb;
}

//监听套接字回调函数,有新连接产生
void CTcpServer::AcceptRegister(void)
{
    int connfd = -1;
    struct sockaddr peerAddr;
    socklen_t peerLen;

    //接受新连接
    connfd = accept(_acceptChn._fd, &peerAddr, &peerLen);
    if(connfd < 0)
    {
        printf("accept return err\n");
        return;
    }
    //设置套接字为非阻塞模式
    if(SetSocketNonBlock(connfd) != 0)
    {
    	printf("AcceptRegister() set nonblock socket err\n");
    	return;
    }
    //注册事件到连接成功的套接字
    T_CHANNEL connChn;
    connChn._fd = connfd;
    connChn._events = EPOLLIN;
    connChn.pArg = NULL;
    //绑定读事件(服务器接收客户端的数据),该函数无参数
    connChn._readCb = std::bind(&CTcpServer::ReadRegister, this);
    connChn._writeCb = NULL;
    connChn._closeCb = NULL;
    connChn._errorCb = NULL;
    //构造新连接对应的map元素,并添加到map,采用map类型是因为连接socket不重复,
    //在处理事件时可以快速查找到对应的连接信息
    _connChn.insert(std::make_pair(connfd, connChn));
    //新连接添加到epoll对象中,注册读事件,客户端发送数据会响应读事件
    ModifyEvent(EPOLL_CTL_ADD, connChn);
    
    //调用连接成功的回调函数,参数为新连接的套接字
    if(_connCb != NULL)
    {
    	_connCb(connfd);
    }
}

//新连接套接字读事件回调函数,接收到客户端的数据
void CTcpServer::ReadRegister(void)
{
    int fd = 0;
    char buf[RECV_BUF_SIZE];

    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if(n > 0)
    {
        //调用用户注册的消息响应函数
        if(_msgCb != NULL)
        {
        	//参数暂时为空
        	_msgCb(NULL);
        }
    }
    else if(0 == n)
    {
        printf("ReadRegister() need to close socket?\n");
    }
    else
    {
        printf("ReadRegister() recv err\n");
    }
}

//初始化事件
int CTcpServer::InitEvent(void)
{
    //初始化epoll对象
    _epollFd = epoll_create1(0);
}

//修改事件
int CTcpServer::ModifyEvent(int operation, T_CHANNEL& channel)
{
    struct epoll_event event;

    //注册epoll对象的事件
    bzero(&event, sizeof(event));
    event.events = channel._events;
    event.data.fd = channel._fd;
    if(epoll_ctl(_epollFd, operation, channel._fd, &event) < 0)
    {
        printf("ModifyEvent() epoll err\n");
        return -1;
    }

    return 0;
}

//等待事件
int CTcpServer::WaitEvent(void)
{
    int nEvents = 0;
    int timeoutMs = 5000;

    //设置超时时间,扫描是否有事件发生,注意_eventList不能为空,
    //不能随意clear,就算在CTcpServer析构函数时clear也要注意是否还会调用
    //epoll_wait(),前提时使用epoll_wait必须保证_eventList为空
    nEvents = epoll_wait(_epollFd, 
                        &(*_eventList.begin()),
                        static_cast<int>(_eventList.size()),
                        timeoutMs);
    //有事件发生
    if(nEvents > 0)
    {
        //检查是否需要扩充容量
        if(nEvents == static_cast<int>(_eventList.size()))
        {
            _eventList.resize(_eventList.size()*2);
        }
    }
    //没有事件发生,只是超时
    else if(0 == nEvents)
    {
        printf("WaitEvent() nothing happended, timeout\n");
    }
    //发生了错误?
    else
    {
        printf("WaitEvent() epoll error\n");
    }

    return nEvents;
}

//利用epoll处理读写事件,事件不能阻塞
void CTcpServer::HandleEvent(int nEvents)
{
    T_CHANNEL chn;

    assert(nEvents <= static_cast<int>(_eventList.size()));
    for(int i = 0; i < nEvents; i++)
    {
        //epoll对象为监听套接字
        if(_eventList[i].data.fd == _acceptChn._fd)
        {
            chn = _acceptChn;
        }
        //epoll对象为连接成功的套接字
        else
        {
            map<int,T_CHANNEL>::iterator it = _connChn.find(_eventList[i].data.fd);
            //正常情况不应该找不到套接字
            if(it == _connChn.end())
            {
                printf("HandleEvent() can not find socket err\n");
                continue;
            }
            chn = it->second;
        }

        //根据epoll事件类型处理
        int revents = _eventList[i].events;
        //关闭连接事件
        if((revents & EPOLLHUP) && !(revents & EPOLLIN))
        {
            printf("HandleEvent() close socket\n");
            if(chn._closeCb != NULL)
            {
            	chn._closeCb();
            }
        }
        //错误事件
        if(revents & EPOLLERR)
        {
            printf("HandleEvent() socket error\n");
            if(chn._errorCb != NULL)
            {
            	chn._errorCb();
            }
        }
        //读事件
        if(revents & (EPOLLIN | EPOLLPRI | EPOLLRDHUP))
        {
            //printf("HandleEvent() read event\n");
            if(chn._readCb != NULL)
            {
            	chn._readCb();
            }
        }
        //写事件
        if(revents & EPOLLOUT)
        {
            printf("HandleEvent() write event\n");
            if(chn._writeCb != NULL)
            {
            	chn._writeCb();
            }
        }
    }
}

//事件循环处理
void CTcpServer::Loop(void)
{
    bool quit = false;

    while(!quit)
    {
        //延时等待是否有事件发生
        int nEvents = 0;
        if((nEvents = WaitEvent()) > 0)
        {
            //处理发生的事件
            HandleEvent(nEvents);
        }
    }
}


