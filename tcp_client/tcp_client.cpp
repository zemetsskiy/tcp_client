#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <process.h>
#include <fstream>
#include <string>

#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")

#define DEFAULT_BUFLEN 512

int get_config_data(std::string& ip_address, std::string& server_port, std::string& client_port) {
    std::string filename = "../Debug/tcp_client.cfg.TXT";

    std::ifstream config(filename);
    if (!config.is_open()) {
        std::cerr << "Error: Unable to open config file " << filename << std::endl;
        return 1;
    }

    if (config.peek() == std::ifstream::traits_type::eof()) {
        std::cerr << "Error: Config file " << filename << " is empty" << std::endl;
        return 1;
    }

    std::string line;
    while (std::getline(config, line)) {
        if (line.find("ip_address=") == 0) {
            ip_address = line.substr(11);
            break;
        }
    }

    while (std::getline(config, line)) {
        if (line.find("server_port=") == 0) {
            server_port = line.substr(12);
            break;
        }
    }

    while (std::getline(config, line)) {
        if (line.find("client_port=") == 0) {
            client_port = line.substr(12);
            break;
        }
    }

    config.close();

    //std::cout << "Server Ip address: " << ip_address << std::endl;
    //std::cout << "Server port: " << server_port << std::endl;
    // std::cout << "Client port: " << client_port << std::endl;
    return 0;
}


unsigned __stdcall ClientThreadFunc(void* pArguments)
{
    int iResult;
    char recvbuf[DEFAULT_BUFLEN];
    int recvbuflen = DEFAULT_BUFLEN;
    SOCKET ConnectSocket = *((SOCKET*)pArguments);

    do {
        // заполнение буфера recvbuf нулевыми байтами
        memset(recvbuf, 0, DEFAULT_BUFLEN);
        // получение данных от сокета
        iResult = recv(ConnectSocket, recvbuf, recvbuflen, 0);
        if (iResult > 0)
            printf("\rBytes received: %d: %s\n%s", iResult, recvbuf, "To server: ");
        else if (iResult == 0)
            printf("Connection closed\n");
        else
            printf("recv failed with error: %d\n", WSAGetLastError());

    } while (iResult > 0);

    _endthreadex(0);
    return 0;
}

int __cdecl main(int argc, char** argv)
{
    // инициализация библиотеки сокетов

    WSADATA wsaData;
    int iResult;
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed with error: %d\n", iResult);
        return 1;
    }


    struct addrinfo* result = NULL, * ptr = NULL, hints;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    // getting data from config
    std::string server_ip_address;
    std::string client_port;
    std::string server_port;


    if (argc == 1) {
        get_config_data(server_ip_address, server_port, client_port);
    }

    else if (argc != 4)  {
        printf("usage: %s server-name server-port client-port or put these params in config file.\n", argv[0]);
        return 1;
    }

    else {
        server_ip_address = argv[1];
        server_port = argv[2];
        client_port = argv[3];
    }

    // переменная result будет содержать связанный список структур addrinfo
    iResult = getaddrinfo(server_ip_address.c_str(), server_port.c_str(), &hints, &result);
    if (iResult != 0) {
        std::cerr << "getaddrinfo failed with error: " << iResult << std::endl;
        WSACleanup();
        return 1;
    }

    SOCKET ConnectSocket = INVALID_SOCKET;
    std::string sendbuf;
    //char recvbuf[DEFAULT_BUFLEN];
    int recvbuflen = DEFAULT_BUFLEN;

    // перебор до 1 успешной попытки
    for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {
        // создание сокета
        ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (ConnectSocket == INVALID_SOCKET) {
            std::cerr << "socket failed with error: " << WSAGetLastError() << std::endl;
            WSACleanup();
            return 1;
        }

        // связываем сокет с адресом
        sockaddr_in client;
        client.sin_family = AF_INET;
        client.sin_addr.s_addr = INADDR_ANY;
        client.sin_port = htons(std::stoi(client_port));

        if (bind(ConnectSocket, (sockaddr*)&client, sizeof(client)) == SOCKET_ERROR) {
            std::cerr << "Can't bind socket! Quitting" << std::endl;
            closesocket(ConnectSocket);
            WSACleanup();
            return 1;
        }

        // подключение клиента к серверу
        iResult = connect(ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
        if (iResult == SOCKET_ERROR) {
            closesocket(ConnectSocket);
            ConnectSocket = INVALID_SOCKET;
            continue;
        }
        break;
    }

    freeaddrinfo(result);

    if (ConnectSocket == INVALID_SOCKET) {
        std::cerr << "Unable to connect to server!" << std::endl;
        WSACleanup();
        return 1;
    }

    // создаем новый поток для обработки подключения клиента к сокету

    HANDLE hThread;
    unsigned threadID; 
    hThread = (HANDLE)_beginthreadex(NULL, 0, &ClientThreadFunc, (void*)(&ConnectSocket), 0, &threadID);

    while (1)
    {
        std::cout << "To server: ";
        std::cin.ignore(); // Очистка буфера ввода
        std::getline(std::cin, sendbuf);

        if (!strcmp(sendbuf.c_str(), "q"))
            break;

        iResult = send(ConnectSocket, sendbuf.c_str(), (int)strlen(sendbuf.c_str()), 0);
        if (iResult == SOCKET_ERROR) {
            std::cerr << "send failed with error: " << WSAGetLastError() << std::endl;
            closesocket(ConnectSocket);
            WSACleanup();
            return 1;
        }
    }

    iResult = shutdown(ConnectSocket, SD_SEND);
    if (iResult == SOCKET_ERROR) {
        std::cerr << "shutdown failed with error: " << WSAGetLastError() << std::endl;

        closesocket(ConnectSocket);
        WSACleanup();
        return 1;
    }

    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
    closesocket(ConnectSocket);
    WSACleanup();

    return 0;
}
