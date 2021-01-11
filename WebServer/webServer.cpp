#pragma comment(lib,"ws2_32")
#include<iostream>
#include<thread>
#include<WinSock2.h>
#include<WS2tcpip.h>
#include<vector>
#include<mutex>

using namespace std;

#define MAX_BUFFER 1024
#define SERVER_PORT 23000

//mutex gMutex;
int userCount = 0;

struct SOCKETINFO
{
	WSAOVERLAPPED overlapped;
	WSABUF dataBuffer;
	SOCKET socket;
	char messageBuffer[MAX_BUFFER];
};

void acceptThread(SOCKET listenSocket, HANDLE* hIOCP);

void workerThread(HANDLE* hIOCP);

int main()
{
	WSADATA WSAData;

	if (WSAStartup(MAKEWORD(2, 2), &WSAData) != 0)
	{
		cout << "WSAStartup Error" << endl;
		return 1;
	}

	SOCKET listenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (listenSocket == INVALID_SOCKET)
	{
		cout << "Invalid Socket Error" << endl;
		WSACleanup();
		return 1;
	}

	SOCKADDR_IN serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(SERVER_PORT);
	serverAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	if (bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR)
	{
		cout << "Bind Failed" << endl;
		closesocket(listenSocket);
		WSACleanup();
		return 1;
	}

	if (listen(listenSocket, 5) == SOCKET_ERROR)
	{
		cout << "Listen Failed" << endl;
		closesocket(listenSocket);
		WSACleanup();
		return 1;
	}

	cout << "Listening for Accept..." << endl;

	HANDLE hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

	SYSTEM_INFO systemInfo;
	GetSystemInfo(&systemInfo);
	int threadCount = systemInfo.dwNumberOfProcessors * 2;
	vector<thread> threadPool;
	for (int i = 0; i < threadCount; i++)
	{
		threadPool.emplace_back(thread{ workerThread,&hIOCP });
	}

	thread aThread(acceptThread, listenSocket, &hIOCP);

	aThread.join();

	for (auto& t : threadPool)
	{
		t.join();
	}

	closesocket(listenSocket);
	WSACleanup();
	return 0;
}

void acceptThread(SOCKET listenSocket, HANDLE* hIOCP)
{
	SOCKET clientSocket;
	SOCKADDR_IN clientAddr;
	int addrLen = sizeof(SOCKADDR_IN);
	SOCKETINFO* socketInfo;
	DWORD receiveBytes;
	DWORD flags;

	while (1)
	{
		clientSocket = accept(listenSocket, (struct sockaddr*)&clientAddr, &addrLen);
		if (clientSocket == INVALID_SOCKET)
		{
			cout << "Accept Failed" << endl;
			return;
		}

		//gMutex.lock();
		//cout << "Accept Success (Current User : " << ++userCount << ")" << endl;
		//gMutex.unlock();

		socketInfo = (struct SOCKETINFO*)malloc(sizeof(struct SOCKETINFO));
		memset((void*)socketInfo, 0, sizeof(struct SOCKETINFO));
		socketInfo->socket = clientSocket;
		socketInfo->dataBuffer.len = MAX_BUFFER;
		socketInfo->dataBuffer.buf = socketInfo->messageBuffer;
		flags = 0;
		*hIOCP = CreateIoCompletionPort((HANDLE)clientSocket, *hIOCP, (DWORD)socketInfo, 0);
		if (WSARecv(clientSocket, &(socketInfo->dataBuffer), 1, &receiveBytes, &flags, &(socketInfo->overlapped), NULL) == INVALID_SOCKET)
		{
			if (WSAGetLastError() != WSA_IO_PENDING)
			{
				cout << "IO Pending Failed" << endl;
				closesocket(clientSocket);
				free(socketInfo);
				return;
			}
		}
	}

	closesocket(clientSocket);
}

void workerThread(HANDLE* hIOCP)
{
	DWORD receiveBytes;
	DWORD sendBytes;
	DWORD completionKey;
	DWORD flags;
	SOCKETINFO* socketInfo;

	while (1)
	{
		if (GetQueuedCompletionStatus(*hIOCP, &receiveBytes, (PULONG_PTR)&completionKey, (LPOVERLAPPED*)&socketInfo, INFINITE) == 0)
		{
			cout << "GQCS Error: " << GetLastError() << endl;
			closesocket(socketInfo->socket);
			free(socketInfo);
			return;
		}

		socketInfo->dataBuffer.len = receiveBytes;

		if (receiveBytes == 0)
		{
			//gMutex.lock();
			//cout << "Close Connection (Current User : " << --userCount << ")" << endl;
			//gMutex.unlock();
			shutdown(socketInfo->socket, SD_BOTH);
			closesocket(socketInfo->socket);
			free(socketInfo);
		}
		else
		{
			//gMutex.lock();
			//cout << "TRACE - Receive message : " << socketInfo->dataBuffer.buf << " (" << socketInfo->dataBuffer.len << " bytes)" << endl;
			//gMutex.unlock();

			strcpy(socketInfo->messageBuffer, "HTTP/1.1 200 OK\nServer: c++\nContent-Type: text/html\nContent-Length: 61\r\n\r\n<!doctype html>\n<html>\n<head>\n</head>\n<body>\n</body>\n</html>");
			socketInfo->dataBuffer.len = strlen(socketInfo->messageBuffer) + 1;

			if (WSASend(socketInfo->socket, &(socketInfo->dataBuffer), 1, &sendBytes, 0, NULL, NULL) == SOCKET_ERROR)
			{
				if (WSAGetLastError() != WSA_IO_PENDING)
				{
					cout << "WSASend Failed" << endl;
				}
			}

			//gMutex.lock();
			//cout << "TRACE - Send message : " << socketInfo->dataBuffer.buf << " (" << socketInfo->dataBuffer.len << " bytes)" << endl;
			//gMutex.unlock();

			//memset(socketInfo->messageBuffer, 0, MAX_BUFFER);
			socketInfo->dataBuffer.len = MAX_BUFFER;
			flags = 0;

			if (WSARecv(socketInfo->socket, &(socketInfo->dataBuffer), 1, &receiveBytes, &flags, &(socketInfo->overlapped), NULL) == SOCKET_ERROR)
			{
				if (WSAGetLastError() != WSA_IO_PENDING)
				{
					cout << "WSARecv Failed" << endl;
				}
			}
		}
	}
}