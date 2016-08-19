/*
 * server.h
 *
 *  Created on: Aug 3, 2016
 *      Author: root
 */

#ifndef SERVER_H_
#define SERVER_H_

#include <sys/epoll.h>
#include <functional>
#include <vector>
#include <map>

using std::vector;
using std::map;

//epoll事件回调函数类型,无返回值,无参数
typedef std::function<void (void)> EVENT_CALLBACK_FUNC;
//连接成功回调函数类型,无返回值,参数为整型
typedef std::function<void (int)> CONNECTION_CALLBACK_FUNC;
//应用程序使用的回调函数,用户使用,无返回值,参数为无类型指针
typedef std::function<void (void*)> CALLBACK_FUNC;

//服务器接口类
class CTcpServer {
public:
    //构造函数
    CTcpServer();

    //析构函数
    ~CTcpServer();

    //启动服务器端口监听
    int StartServer(unsigned short port);

    //连接成功调用的回调函数
    void SetConnectCallback(CONNECTION_CALLBACK_FUNC& cb);

    //消息处理调用的回调函数
    void SetMessageCallback(CALLBACK_FUNC& cb);

    //事件循环处理
    void Loop(void);

private:
    //监测事件的对象,每个epoll事件对应一个对象
    typedef struct {
    	int _fd;//监测的套接字
    	int _events;//关注的事件类型

    	//事件对应的回调函数
    	void* pArg;//回调函数的入口参数
    	EVENT_CALLBACK_FUNC _readCb;//读事件
    	EVENT_CALLBACK_FUNC _writeCb;//写事件
    	EVENT_CALLBACK_FUNC _closeCb;//关闭连接事件
    	EVENT_CALLBACK_FUNC _errorCb;//错误事件
    } T_CHANNEL;

    const int RECV_BUF_SIZE = 16 * 1024;//接收缓冲区大小
    const int INIT_EVENT_SIZE = 16;//初始化事件列表大小,不能为空

    T_CHANNEL _acceptChn;//监听套接字的epoll事件
    map<int,T_CHANNEL> _connChn;//连接成功套接字的epoll事件,int为连接socket

    //用户注册使用的回调函数
    CONNECTION_CALLBACK_FUNC _connCb;
    CALLBACK_FUNC _msgCb;

    //epoll对象
    int _epollFd;
    vector<struct epoll_event> _eventList;

    //初始化服务器,监听该端口的连接
    int InitServer(unsigned short port);

    //设置套接字为非阻塞模式
    int SetSocketNonBlock(int socket);

    //监听套接字回调函数,有新连接产生
    void AcceptRegister(void);

    //新连接套接字读事件回调函数,接收到客户端的数据
    void ReadRegister(void);

    //epoll对象 - 处理事件(读/写/错误)
    void HandleEvent(int nEvents);

    //epoll对象 - 初始化事件
    int InitEvent(void);

    //epoll对象 - 等待事件
    int WaitEvent(void);

    //epoll对象 - 修改事件
    int ModifyEvent(int operation, T_CHANNEL& channel);

    //关闭服务器的连接
    void CloseConnection(void);

    //nocopy
    CTcpServer(const CTcpServer&);
	CTcpServer& operator=(const CTcpServer&);
};

#endif /* SERVER_H_ */
