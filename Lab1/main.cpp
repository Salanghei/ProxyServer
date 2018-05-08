#include <iostream>
#include <fstream>
#include <stdio.h>
#include <windows.h>
#include <process.h>
#include <string.h>
#include <winsock.h>
using namespace std;

#pragma comment(lib,"Ws2_32.lib")
#define MAXSIZE 65507
#define HTTP_PORT 80

struct HttpHeader {
	char method[4];
	char url[1024];
	char host[1024];
	char cookie[1024 * 10];
	HttpHeader() {
		ZeroMemory(this, sizeof(HttpHeader));
	}
};
bool InitSocket();
void ParseHttpHead(char *buffer, HttpHeader *httpHeader);
boolean ParseDate(char *buffer, char *field, char *tempDate);
void makeNewHTTP(char *buffer, char *value);
void makeFilename(char *url, char *filename);
void makeCache(char *buffer, char *url);
void getCache(char *buffer, char *filename);
bool ConnectToServer(SOCKET *serverSocket, char *host);
unsigned int _stdcall ProxyThread(LPVOID lpParameter);
//代理服务器参数
SOCKET ProxyServer;
SOCKADDR_IN ProxyServerAddr;
const int ProxyPort = 10240;

bool haveCache = false;
bool needCache = true;

struct ProxyParam {
	SOCKET cilentSocket;
	SOCKET serverSocket;
};

int main(int argc, TCHAR* argv[]) {
	printf("代理服务器正在启动\n");
	printf("初始化...\n");
	if (!InitSocket()) {
		printf("服务器初始化失败\n");
		return -1;
	}
	printf("代理服务器正在运行，监听 : %d\n", ProxyPort);
	SOCKET acceptSocket = INVALID_SOCKET;
	ProxyParam *lpProxyParam;
	HANDLE hThread;
	DWORD dwThreadID;
	//代理服务器循环监听
	while (true) {
		haveCache = false;
		needCache = true;
		acceptSocket = accept(ProxyServer, NULL, NULL);
		lpProxyParam = new ProxyParam;
		if (lpProxyParam == NULL) {
			continue;
		}
		lpProxyParam->cilentSocket = acceptSocket;
		hThread = (HANDLE)_beginthreadex(NULL, 0, &ProxyThread, (LPVOID)lpProxyParam, 0, 0);
		CloseHandle(hThread);
		Sleep(200);
	}
	closesocket(ProxyServer);
	WSACleanup();
	return 0;
}

//初始化套接字
bool InitSocket() {
	//加载套接字库（必须）
	WORD wVersionRequested;
	WSADATA wsaData;
	int err; ////套接字加载时错误提示
	wVersionRequested = MAKEWORD(2, 2); //版本是2.2
										//加载dll文件Socket库
	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {//找不到winsock.dll
		printf("加载winsock.dll失败，错误：%d", WSAGetLastError());
		return false;
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
		printf("不能找到正确的winsock版本\n");
		WSACleanup();
		return false;
	}
	ProxyServer = socket(AF_INET, SOCK_STREAM, 0); //创建TCP连接的套接字
	if (INVALID_SOCKET == ProxyServer) {
		printf("创建套接字失败，错误代码为 %d\n", WSAGetLastError());
		return false;
	}
	ProxyServerAddr.sin_family = AF_INET;
	ProxyServerAddr.sin_port = htons(ProxyPort); //指向代理服务器的端口
	ProxyServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;
	if (bind(ProxyServer, (SOCKADDR*)&ProxyServerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
		printf("绑定套接字失败\n");
		return false;
	}
	if (listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR) {
		printf("监听端口%d失败", ProxyPort);
		return false;
	}
	return true;
}

//解析TCP报文中的HTTP头部
void ParseHttpHead(char *buffer, HttpHeader *httpHeader) {
	char *p;
	char *ptr;
	const char *delim = "\r\n";
	p = strtok_s(buffer, delim, &ptr);
	//printf("%s\n", p);
	if (p[0] == 'G') {  //GET方式
		memcpy(httpHeader->method, "GET", 3);
		memcpy(httpHeader->url, &p[4], strlen(p) - 13);  //url的长度
	}
	else if (p[0] == 'P') {  //POST方式
		memcpy(httpHeader->method, "POST", 4);
		memcpy(httpHeader->url, &p[5], strlen(p) - 14);
	}
	//printf("%s\n", httpHeader->url);
	p = strtok_s(NULL, delim, &ptr);
	while (p) {
		switch (p[0]) {
		case 'H':  //host
			memcpy(httpHeader->host, &p[6], strlen(p) - 6);
			break;
		case 'C': //cookie
			if (strlen(p) > 8) {
				char header[8];
				ZeroMemory(header, sizeof(header));
				memcpy(header, p, 6);
				if (!strcmp(header, "Cookie")) {
					memcpy(httpHeader->cookie, &p[8], strlen(p) - 8);
				}
			}
			break;
		default:
			break;
		}
		p = strtok_s(NULL, delim, &ptr);
	}
}

//分析HTTP头部的field字段，如果包含该field则返回true，并获取日期
boolean ParseDate(char *buffer, char *field, char *tempDate) {
	char *p, *ptr, temp[5];
	//const char *field = "If-Modified-Since";
	const char *delim = "\r\n";
	ZeroMemory(temp, 5);
	p = strtok_s(buffer, delim, &ptr);
	//printf("%s\n", p);
	int len = strlen(field) + 2;
	while (p) {
		if (strstr(p, field) != NULL) {
			memcpy(tempDate, &p[len], strlen(p) - len);
			//printf("tempDate: %s\n", tempDate);
			return true;
		}
		p = strtok_s(NULL, delim, &ptr);
	}
	return false;
}

//改造HTTP请求报文
void makeNewHTTP(char *buffer, char *value) {
	const char *field = "Host";
	const char *newfield = "If-Modified-Since: ";
	//const char *delim = "\r\n";
	char temp[MAXSIZE];
	ZeroMemory(temp, MAXSIZE);
	char *pos = strstr(buffer, field);
	for (int i = 0; i < strlen(pos); i++) {
		temp[i] = pos[i];
	}
	*pos = '\0';
	while (*newfield != '\0') {  //插入If-Modified-Since字段
		*pos++ = *newfield++;
	}
	while (*value != '\0') {
		*pos++ = *value++;
	}
	*pos++ = '\r';
	*pos++ = '\n';
	for (int i = 0; i < strlen(temp); i++) {
		*pos++ = temp[i];
	}
	//printf("buffer: %s\n", buffer);
}

//根据url构造文件名
void makeFilename(char *url, char *filename) {
	//char filename[100];  // 构造文件名
	//ZeroMemory(filename, 100);
	char *p = filename;
	while (*url != '\0') {
		if (*url != '/' && *url != ':' && *url != '.') {
			*p++ = *url;
		}
		url++;
	}
}


//进行缓存
void makeCache(char *buffer, char *url) {
	char *p, *ptr, num[10], tempBuffer[MAXSIZE + 1];
	const char * delim = "\r\n";
	ZeroMemory(num, 10);
	ZeroMemory(tempBuffer, MAXSIZE + 1);
	memcpy(tempBuffer, buffer, strlen(buffer));
	p = strtok_s(tempBuffer, delim, &ptr);//提取第一行
	memcpy(num, &p[9], 3);
	if (strcmp(num, "200") == 0) {  //状态码是200时缓存
		//printf("url : %s\n", url);
		char filename[100] = { 0 };  // 构造文件名
		makeFilename(url, filename);
		//printf("filename : %s\n", filename);
		FILE *out;
		if (fopen_s(&out, filename, "wb") == 0) {
			fwrite(buffer, sizeof(char), strlen(buffer), out);
			fclose(out);
		}
		printf("\n报文已缓存！\n");
	}
}

//获取缓存
void getCache(char *buffer, char *filename) {
	char *p, *ptr, num[10], tempBuffer[MAXSIZE + 1];
	const char * delim = "\r\n";
	ZeroMemory(num, 10);
	ZeroMemory(tempBuffer, MAXSIZE + 1);
	memcpy(tempBuffer, buffer, strlen(buffer));
	p = strtok_s(tempBuffer, delim, &ptr);//提取第一行
	memcpy(num, &p[9], 3);
	if (strcmp(num, "304") == 0) {  //主机返回的报文中的状态码为304时返回已缓存的内容
		printf("获取本地缓存！\n");
		ZeroMemory(buffer, strlen(buffer));
		FILE *in;
		if (fopen_s(&in, filename, "rb") == 0) {
			fread(buffer, sizeof(char), MAXSIZE, in);
			fclose(in);
		}
		needCache = false;
	}
}

//根据主机创建目标服务器套接字，并连接
bool ConnectToServer(SOCKET *serverSocket, char *host) {
	SOCKADDR_IN serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(HTTP_PORT); //本地字节顺序 ---> 网络字节顺序
	HOSTENT *hostent = gethostbyname(host);
	if (!hostent) {
		return false;
	}
	IN_ADDR inAddr = *((IN_ADDR*)*hostent->h_addr_list);
	serverAddr.sin_addr.S_un.S_addr = inet_addr(inet_ntoa(inAddr));
	*serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (*serverSocket == INVALID_SOCKET) {
		return false;
	}
	if (connect(*serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) ==
		SOCKET_ERROR) {
		closesocket(*serverSocket);
		return false;
	}
	return true;

}

//线程执行函数
unsigned int _stdcall ProxyThread(LPVOID lpParameter) {
	char Buffer[MAXSIZE], fileBuffer[MAXSIZE];
	char *CacheBuffer, *DateBuffer;
	ZeroMemory(Buffer, MAXSIZE);
	SOCKADDR_IN clientAddr;
	int length = sizeof(SOCKADDR_IN);
	int recvSize;
	int ret;
	//接受客户端的http请求
	recvSize = recv(((ProxyParam*)lpParameter)->cilentSocket, Buffer, MAXSIZE, 0);
	if (recvSize <= 0) {
		goto error;
	}
	HttpHeader *httpHeader = new HttpHeader();
	CacheBuffer = new char[recvSize + 1];
	ZeroMemory(CacheBuffer, recvSize + 1);
	memcpy(CacheBuffer, Buffer, recvSize);
	ParseHttpHead(CacheBuffer, httpHeader);

	//缓存
	DateBuffer = new char[recvSize + 1];
	ZeroMemory(DateBuffer, strlen(Buffer) + 1);
	memcpy(DateBuffer, Buffer, strlen(Buffer) + 1);
	//printf("DateBuffer: \n%s\n", DateBuffer);
	char filename[100];
	ZeroMemory(filename, 100);
	makeFilename(httpHeader->url, filename);
	//printf("filename : %s\n", filename);
	char *field = "Date";
	char date_str[30];  //保存字段Date的值
	ZeroMemory(date_str, 30);
	ZeroMemory(fileBuffer, MAXSIZE);
	FILE *in;
	if (fopen_s(&in, filename, "rb") == 0) {
		printf("\n代理服务器在该url下有相应缓存！\n");
		fread(fileBuffer, sizeof(char), MAXSIZE, in);
		fclose(in);
		//printf("fileBuffer : \n%s\n", fileBuffer);
		ParseDate(fileBuffer, field, date_str);
		printf("date_str: %s\n", date_str);
		makeNewHTTP(Buffer, date_str);
		printf("\n======改造后的请求报文======\n%s\n", Buffer);
		haveCache = true;
		goto success;
	}

	//网站屏蔽
	if (strcmp(httpHeader->url, "http://mail.hit.edu.cn/") == 0) {
		printf("\n=====================================\n\n");
		printf("您所前往的网站已被屏蔽！\n");
		goto error;
	}

	//网站引导：钓鱼
	if (strcmp(httpHeader->url, "http://today.hit.edu.cn/") == 0) {
		printf("\n=====================================\n\n");
		printf("钓鱼成功：您所前往的http://today.hit.edu.cn/已被引导至http://jwts.hit.edu.cn\n");
		memcpy(httpHeader->host, "jwts.hit.edu.cn", 22);
	}

	//用户过滤
	//char hostname[128];
	//int retnew = gethostname(hostname, sizeof(hostname));
	//HOSTENT *hent = gethostbyname(hostname);
	//char *ip = inet_ntoa(*(in_addr*)*hent->h_addr_list);  //获取本地ip地址
	//if (strcmp(ip, "172.20.56.45") == 0) {
	//	printf("\n=====================================\n\n");
	//	printf("客户ip地址：%s\n", ip);
	//	printf("您的主机已被屏蔽！\n");
	//	goto error;
	//}

	delete CacheBuffer;
	delete DateBuffer;

success:
	if (!ConnectToServer(&((ProxyParam*)lpParameter)->serverSocket, httpHeader->host)) {
		printf("代理连接主机失败!\n");
		goto error;
	}
	printf("\n\n------*-*------*-*------*-*------*-*------*-*------*-*------*-*------\n\n");
	printf("代理连接主机 %s 成功!\n", httpHeader->host);
	printf("\n======请求报文======\n%s\n", Buffer);
	//将客户端发送的HTTP数据报文直接转发给目标服务器
	ret = send(((ProxyParam*)lpParameter)->serverSocket, Buffer, strlen(Buffer) + 1, 0);
	//等待目标服务器返回数据
	recvSize = recv(((ProxyParam*)lpParameter)->serverSocket, Buffer, MAXSIZE, 0);
	if (recvSize <= 0) {
		printf("接收数据报文失败!\n");
		goto error;
	}
	//有缓存时，判断返回的状态码是否是304，若是则将缓存的内容发送给客户端
	if (haveCache == true) {
		getCache(Buffer, filename);
	}
	//将目标服务器返回的数据直接转发给客户端
	printf("\n======响应报文======\n%s\n", Buffer);
	if (needCache == true) {
		makeCache(Buffer, httpHeader->url);  //缓存报文
	}
	ret = send(((ProxyParam*)lpParameter)->cilentSocket, Buffer, sizeof(Buffer), 0);

error:  //错误处理
		//printf("关闭套接字\n");
	Sleep(200);
	closesocket(((ProxyParam*)lpParameter)->cilentSocket);
	closesocket(((ProxyParam*)lpParameter)->serverSocket);
	delete lpParameter;
	_endthreadex(0);
	return 0;
}
