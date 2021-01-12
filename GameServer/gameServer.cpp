#pragma comment(lib,"ws2_32")
#include<iostream>
#include<thread>
#include<WinSock2.h>
#include<WS2tcpip.h>
#include<vector>
#include<mutex>

#define MAX_BUFFER 1024
#define MAX_MESSAGE_LEN 512
#define MAX_ID_LEN 20
#define SERVER_PORT 23000

int userCount = 0;

enum class eOP
{
	opSEND,
	opRECV
};


enum ePKCMD
{
	scLOGIN_OK,
	scLOGIN_NO,
	csLOGIN,
	scENTER,
	scLEAVE,
	scMOVE,
	csMOVE,
	scMESSAGE,
	csMESSAGE
};

#pragma pack(push, 1)
struct pkHead
{
	WORD dataSize;
	DWORD cmd;
};
struct st_packet
{
	pkHead head;
	char data[MAX_BUFFER];
};
struct st_scLOGIN_OK
{
	pkHead head;
	int id;
	float x, y, z;
};
struct st_csLOGIN
{
	pkHead head;
	char name[MAX_ID_LEN];
};
struct st_scENTER
{
	pkHead head;
	int id;
	char name[MAX_ID_LEN];
	float x, y, z;
};
struct st_scLEAVE
{
	pkHead head;
	int id;
};
struct st_scMOVE
{
	pkHead head;
	int id;
	float x, y, z;
};
struct st_csMOVE
{
	pkHead head;
	float x, y, z;
};
struct st_scMESSAGE
{
	pkHead head;
	int id;
	char message[MAX_MESSAGE_LEN];
};
struct st_csMESSAGE
{
	pkHead head;
	char message[MAX_MESSAGE_LEN];
};
#pragma pack(pop)

struct SOCKETINFO
{
	WSAOVERLAPPED overlapped;
	WSABUF dataBuffer;
	char buffer[MAX_BUFFER];
	eOP op;
};

struct CLIENT
{
	SOCKET socket;
	SOCKETINFO* socketInfo;
	bool connected;
	int id;

	char name[MAX_ID_LEN];
	float x, y, z;
};

std::vector<CLIENT> clients;

void acceptThread(SOCKET listenSocket, HANDLE* hIOCP);

void workerThread(HANDLE* hIOCP);

void sendEnterPacket(int userID, float x, float y, float z, char* name);

void sendLoginOKPacket(int userID, float x, float y, float z);

void sendLeavePacket(int userID);

void sendMovePacket(int userID, float x, float y, float z);

void sendMessagePacket(int userID, char* message);

void sendPacket(int userID, void* packet);

int main()
{
	WSADATA WSAData;

	if (WSAStartup(MAKEWORD(2, 2), &WSAData) != 0)
	{
		std::cout << "WSAStartup Error" << std::endl;
		return 1;
	}

	SOCKET listenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (listenSocket == INVALID_SOCKET)
	{
		std::cout << "Invalid Socket Error" << std::endl;
		WSACleanup();
		return 1;
	}
	int nagleOption = FALSE;
	setsockopt(listenSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&nagleOption, sizeof(nagleOption));

	SOCKADDR_IN serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(SERVER_PORT);
	serverAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	if (bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR)
	{
		std::cout << "Bind Failed" << std::endl;
		closesocket(listenSocket);
		WSACleanup();
		return 1;
	}

	if (listen(listenSocket, 5) == SOCKET_ERROR)
	{
		std::cout << "Listen Failed" << std::endl;
		closesocket(listenSocket);
		WSACleanup();
		return 1;
	}

	std::cout << "Listening for Accept..." << std::endl;

	HANDLE hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

	SYSTEM_INFO systemInfo;
	GetSystemInfo(&systemInfo);
	int threadCount = systemInfo.dwNumberOfProcessors * 2;
	std::vector<std::thread> threadPool;
	for (int i = 0; i < threadCount; i++)
	{
		threadPool.emplace_back(std::thread{ workerThread,&hIOCP });
	}

	std::thread aThread(acceptThread, listenSocket, &hIOCP);

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
	DWORD receiveBytes;
	DWORD flags;

	while (1)
	{
		clientSocket = accept(listenSocket, (struct sockaddr*)&clientAddr, &addrLen);
		if (clientSocket == INVALID_SOCKET)
		{
			std::cout << "Accept Failed" << std::endl;
			return;
		}

		std::cout << "Accept Success" << std::endl;

		CLIENT client;
		client.socket = clientSocket;
		client.id = userCount;

		client.socketInfo = new SOCKETINFO;
		memset((void*)&client.socketInfo->overlapped, 0, sizeof(WSAOVERLAPPED));
		client.socketInfo->dataBuffer.buf = client.socketInfo->buffer;
		client.socketInfo->dataBuffer.len = MAX_BUFFER;
		client.socketInfo->op = eOP::opRECV;
		flags = 0;
		*hIOCP = CreateIoCompletionPort((HANDLE)clientSocket, *hIOCP, (ULONG_PTR)&client.id, 0);

		clients.push_back(client);
		userCount++;

		if (WSARecv(clientSocket, &(client.socketInfo->dataBuffer), 1, &receiveBytes, &flags, &(client.socketInfo->overlapped), NULL) == INVALID_SOCKET)
		{
			if (WSAGetLastError() != WSA_IO_PENDING)
			{
				std::cout << "IO Pending Failed" << std::endl;
				closesocket(clientSocket);
				client.connected = false;
				delete client.socketInfo;
				return;
			}
		}
	}

	closesocket(clientSocket);
}

void workerThread(HANDLE* hIOCP)
{
	DWORD transferBytes;
	int* id;
	DWORD flags;
	SOCKETINFO* socketInfo;
	CLIENT* cl;
	WORD dataSize;

	while (1)
	{
		if (GetQueuedCompletionStatus(*hIOCP, &transferBytes, (PULONG_PTR)&id, (LPOVERLAPPED*)&socketInfo, INFINITE) == 0)
		{
			std::cout << "GQCS Error: " << GetLastError() << std::endl;
			closesocket(clients[*id].socket);
			clients[*id].connected = false;
			delete socketInfo;
			return;
		}

		cl = &clients[*id];

		switch (socketInfo->op)
		{
		case eOP::opSEND:
		{
			if (transferBytes == 0)
			{
				std::cout << cl->name << " Logout." << std::endl;
				closesocket(cl->socket);
			}
			delete socketInfo;
			break;
		}
		case eOP::opRECV:
		{
			if (transferBytes == 0)
			{
				std::cout << cl->name << " Logout." << std::endl;
				closesocket(cl->socket);
				delete socketInfo;
			}
			else
			{
				switch (reinterpret_cast<st_packet*>(socketInfo->buffer)->head.cmd)
				{
				case csLOGIN:
				{
					st_csLOGIN* recvPacket = reinterpret_cast<st_csLOGIN*>(socketInfo->buffer);
					dataSize = recvPacket->head.dataSize;
					strcpy(cl->name, recvPacket->name);
					cl->name[strlen(cl->name)] = NULL;
					cl->x = 5;
					cl->y = 1.5;
					cl->z = 7.9;
					cl->connected = true;
					std::cout << recvPacket->name << " Login." << std::endl;
					sendLoginOKPacket(*id, cl->x, cl->y, cl->z);
					sendEnterPacket(*id, cl->x, cl->y, cl->z, cl->name);
					break;
				}
				case csMOVE:
				{
					st_csMOVE* recvPacket = reinterpret_cast<st_csMOVE*>(socketInfo->buffer);
					dataSize = recvPacket->head.dataSize;
					cl->x = recvPacket->x;
					cl->y = recvPacket->y;
					cl->z = recvPacket->z;
					sendMovePacket(*id, cl->x, cl->y, cl->z);
					break;
				}
				case csMESSAGE:
				{
					st_csMESSAGE* recvPacket = reinterpret_cast<st_csMESSAGE*>(socketInfo->buffer);
					dataSize = recvPacket->head.dataSize;
					std::cout << cl->name << ": " << recvPacket->message << std::endl;
					sendMessagePacket(*id, recvPacket->message);
					break;
				}
				default:

					break;
				}

				cl->socketInfo->dataBuffer.len = MAX_BUFFER;
				flags = 0;

				if (WSARecv(cl->socket, &(cl->socketInfo->dataBuffer), 1, NULL, &flags, &(cl->socketInfo->overlapped), NULL) == SOCKET_ERROR)
				{
					if (WSAGetLastError() != WSA_IO_PENDING)
					{
						std::cout << "WSARecv Failed" << std::endl;
					}
				}
			}
			break;
		}
		}
	}
}

void sendEnterPacket(int userID, float x, float y, float z, char* name)
{
	st_scENTER packet;

	packet.head.cmd = scMOVE;
	packet.head.dataSize = sizeof(packet);
	packet.id = userID;
	packet.x = x;
	packet.y = y;
	packet.z = z;
	strcpy(packet.name, name);

	for (auto& cl : clients)
	{
		if (cl.id == userID || !cl.connected)continue;
		sendPacket(cl.id, &packet);
	}
}

void sendLoginOKPacket(int userID, float x, float y, float z)
{
	st_scLOGIN_OK packet;

	packet.head.cmd = scLOGIN_OK;
	packet.head.dataSize = sizeof(packet);
	packet.id = userID;
	packet.x = x;
	packet.y = y;
	packet.z = z;

	sendPacket(userID, &packet);
}

void sendLeavePacket(int userID)
{
	st_scLEAVE packet;

	packet.head.cmd = scLEAVE;
	packet.head.dataSize = sizeof(packet);
	packet.id = userID;

	for (auto& cl : clients)
	{
		if (cl.id == userID || !cl.connected)continue;
		sendPacket(cl.id, &packet);
	}
}

void sendMovePacket(int userID, float x, float y, float z)
{
	st_scMOVE packet;

	packet.head.cmd = scMOVE;
	packet.head.dataSize = sizeof(packet);
	packet.id = userID;
	packet.x = x;
	packet.y = y;
	packet.z = z;

	for (auto& cl : clients)
	{
		if (cl.id == userID || !cl.connected)continue;
		sendPacket(cl.id, &packet);
	}
}

void sendMessagePacket(int userID, char* message)
{
	st_scMESSAGE packet;

	packet.head.cmd = scMESSAGE;
	packet.head.dataSize = sizeof(packet);
	packet.id = userID;
	strcpy(packet.message, message);

	//clients[userID].socketInfo->buffer = sendPacket;

	for (auto& cl : clients)
	{
		if (cl.id == userID || !cl.connected)continue;
		sendPacket(cl.id, &packet);
	}
}

void sendPacket(int userID, void* packet)
{
	char* buf = reinterpret_cast<char*>(packet);

	CLIENT* user = &clients[userID];

	SOCKETINFO* socketInfo = new SOCKETINFO;
	memset((void*)socketInfo, 0, sizeof(struct SOCKETINFO));
	socketInfo->op = eOP::opSEND;
	socketInfo->dataBuffer.buf = socketInfo->buffer;
	socketInfo->dataBuffer.len = reinterpret_cast<st_packet*>(packet)->head.dataSize;
	memcpy(socketInfo->buffer, buf, socketInfo->dataBuffer.len);

	if (WSASend(user->socket, &(socketInfo->dataBuffer), 1, NULL, 0, &(socketInfo->overlapped), NULL) == SOCKET_ERROR)
	{
		if (WSAGetLastError() != WSA_IO_PENDING)
		{
			std::cout << "ERROR - Fail WSASend(error_code : " << WSAGetLastError() << std::endl;
		}
	}
}