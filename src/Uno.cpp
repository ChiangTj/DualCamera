#include "Uno.h"

UNO::UNO() {
    portName = "\\\\.\\COM3";
    hSerial = CreateFile(portName,
        GENERIC_READ | GENERIC_WRITE,
        0,
        0,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        0);

    if (hSerial == INVALID_HANDLE_VALUE) {
        printf("打开串口失败\n");
        return;
    }

    dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(hSerial, &dcbSerialParams)) {
        printf("获取串口状态失败\n");
        CloseHandle(hSerial);
        return;
    }

    dcbSerialParams.BaudRate = CBR_9600;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;

    if (!SetCommState(hSerial, &dcbSerialParams)) {
        printf("设置串口参数失败\n");
        CloseHandle(hSerial);
        return;
    }

    // 设置串口超时参数
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;

    if (!SetCommTimeouts(hSerial, &timeouts)) {
        printf("设置超时参数失败\n");
        CloseHandle(hSerial);
        return;
    }
}

UNO::~UNO() {
    if (hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(hSerial);
    }
}

void UNO::start() {
    if (hSerial == INVALID_HANDLE_VALUE) {
        printf("串口未打开\n");
        return;
    }

    char command = 'a'; 
    if (!WriteFile(hSerial, &command, 1, &bytesWritten, NULL)) {
        printf("发送命令失败\n");
    }
    printf("发送命令成功\n");
}

void UNO::stop() {
    if (hSerial == INVALID_HANDLE_VALUE) {
        printf("串口未打开\n");
        return;
    }

    char command = 'q'; 
    if (!WriteFile(hSerial, &command, 1, &bytesWritten, NULL)) {
        printf("发送命令失败\n");
    }
}