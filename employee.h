#pragma once
#include <windows.h>

struct employee {
    int num;
    char name[10];
    double hours;
};

enum CommandType {
    CMD_READ,
    CMD_WRITE_REQUEST,
    CMD_WRITE_SUBMIT,
    CMD_FINISH_ACCESS,
    CMD_EXIT
};

struct Request {
    CommandType cmd;
    int id;         
    DWORD clientPid;
    employee data;  
};

struct Response {
    bool ok;
    employee data;
};
