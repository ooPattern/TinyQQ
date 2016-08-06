/*
 * mysqlIntf.h
 *
 *  Created on: Aug 3, 2016
 *      Author: root
 */

#ifndef MYSQLINTF_H_
#define MYSQLINTF_H_

#include <vector>
#include <mysql/mysql.h>

using std::vector;

//SQL执行查询的结果集结构,由于将结果集存储起来比直接访问结果集指针会增加内存,
//但是将结果集封装使用会更加方便如果担心大的结果集会增加很多内存,
//应该从SQL语句入手限制某次查询出现大的结果集
typedef struct {
	int nField;//字段的个数
	MYSQL_FIELD* pField;//字段属性的指针
	vector<MYSQL_ROW> records;//结果集合
} T_SQL_RECORD;

//MYSQL操作接口类
class CMysqlHandle {
public:
	CMysqlHandle();
	~CMysqlHandle();

	//连接数据库
	int Connect(const char* pHost = "127.0.0.1",
				const short port = 3306,
				const char* pUser = "root",
				const char* pPassword = NULL,
				const char* pDbname = NULL);
	//查询数据库,输出结果集
	int SelectQuery(const char* pSQL, T_SQL_RECORD& result);
	//修改数据库
	int ModifyQuery(const char* pSQL);
	//显示查询结果
	void ShowResult(T_SQL_RECORD& result);

private:
	MYSQL* _pConnect;
	MYSQL_RES* _pRes;

	bool _isConnected;//数据库连接状态, true:已连接,false:还没有连接
	//尝试重新连接,连接成功返回0,SQL语句执行失败有可能是SQL语句本身问题,也有可能是连接的问题
	int ReConnect(void);
	//尝试重连方式的执行SQL语句
	int QuerySQL(const char* pSQL, int len);
};

#endif /* MYSQLINTF_H_ */
