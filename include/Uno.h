#ifndef UNO_H
#define UNO_H
#include <stdio.h>
#include <windows.h>

class UNO {
	private:
		char data;
		DWORD bytesWritten;
		HANDLE hSerial;
		DCB dcbSerialParams;
		const char* portName;
	public:
		UNO();
		~UNO();
		void start();
		void stop();

};

#endif