#include <windows.h>
#include <iostream>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include "employee.h"
using namespace std;

struct RecordLock {
    HANDLE mutex;           
    HANDLE writeSemaphore;  
    int readerCount;        
    int writerCount;        
    bool writerWaiting;     

    RecordLock() {
        readerCount = 0;
        writerCount = 0;
        writerWaiting = false;
        mutex = CreateMutex(NULL, FALSE, NULL);
        writeSemaphore = CreateSemaphore(NULL, 1, 1, NULL);
    }

    ~RecordLock() {
        CloseHandle(mutex);
        CloseHandle(writeSemaphore);
    }
};

map<int, employee> database;
map<int, RecordLock*> locks;
map<DWORD, int> clientOperations;

HANDLE hServerPipe;
string filename;

bool beginRead(int id) {
    RecordLock* lock = locks[id];
    if (!lock)
        return false;

    DWORD startTime = GetTickCount();

    WaitForSingleObject(lock->mutex, INFINITE);

    while (lock->writerCount > 0 || lock->writerWaiting) {
        ReleaseMutex(lock->mutex);

        if (GetTickCount() - startTime > 1000) {
            return false;
        }

        Sleep(10);
        WaitForSingleObject(lock->mutex, INFINITE);
    }

    lock->readerCount++;
    ReleaseMutex(lock->mutex);
    return true;
}

void endRead(int id) {
    RecordLock* lock = locks[id];
    if (!lock)
        return;

    WaitForSingleObject(lock->mutex, INFINITE);
    lock->readerCount--;

    if (lock->readerCount == 0 && lock->writerWaiting) {
        ReleaseSemaphore(lock->writeSemaphore, 1, NULL);
    }

    ReleaseMutex(lock->mutex);
}

bool beginWrite(int id) {
    RecordLock* lock = locks[id];
    if (!lock) return false;

    WaitForSingleObject(lock->mutex, INFINITE);

    if (lock->readerCount > 0 || lock->writerCount > 0) {
        lock->writerWaiting = true;
        ReleaseMutex(lock->mutex);

        DWORD result = WaitForSingleObject(lock->writeSemaphore,100);

        WaitForSingleObject(lock->mutex, INFINITE);
        lock->writerWaiting = false;

        if (result == WAIT_OBJECT_0) {
            lock->writerCount = 1;
            ReleaseMutex(lock->mutex);
            return true;
        }
        else {
            ReleaseMutex(lock->mutex);
            return false; 
        }
    }
    else {
        WaitForSingleObject(lock->writeSemaphore, INFINITE);
        lock->writerCount = 1;
        ReleaseMutex(lock->mutex);
        return true;
    }
}

void endWrite(int id) {
    RecordLock* lock = locks[id];
    if (!lock) return;

    WaitForSingleObject(lock->mutex, INFINITE);
    lock->writerCount = 0;
    ReleaseSemaphore(lock->writeSemaphore, 1, NULL);
    ReleaseMutex(lock->mutex);
}

void loadFile() {
    ifstream f(filename, ios::binary);
    if (!f.is_open()) {
        cout << "Не удалось открыть файл для чтения\n";
        return;
    }

    employee e;
    while (f.read((char*)&e, sizeof(e))) {
        database[e.num] = e;
        if (!locks[e.num]) {
            locks[e.num] = new RecordLock();
        }
    }
    f.close();
}

void saveFile() {
    ofstream f(filename, ios::binary | ios::trunc);
    if (!f.is_open()) {
        cout << "Не удалось открыть файл для записи\n";
        return;
    }

    for (auto& p : database) {
        f.write((char*)&p.second, sizeof(employee));
    }
    f.close();
}

void printFile() {
    cout << "\nСодержимое файла:\n";
    cout << "ID\tИмя\tЧасы\n";
    cout << "----------------------\n";
    for (auto& p : database) {
        cout << p.second.num << "\t"
            << p.second.name << "\t"
            << p.second.hours << endl;
    }
}

void sendResponse(DWORD pid, const Response& resp) {
    string pipeName = "\\\\.\\pipe\\client_pipe_" + to_string(pid);
    HANDLE hNamedPipe = CreateFileA(
        pipeName.c_str(),
        GENERIC_WRITE,
        0, NULL,
        OPEN_EXISTING,
        0, NULL);

    if (hNamedPipe != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(hNamedPipe, &resp, sizeof(resp), &written, NULL);
        CloseHandle(hNamedPipe);
    }
    else {
        cout << "Ошибка отправки ответа клиенту " << pid << endl;
    }
}

void processRequest(const Request& req) {
    Response resp{};
    int id = req.id;

    if (!locks[id]) {
        locks[id] = new RecordLock();
    }

    RecordLock* lock = locks[id];

    switch (req.cmd) {
    case CMD_READ:
        beginRead(id);

        clientOperations[req.clientPid] = CMD_READ;
        resp.ok = database.count(id);
        if (resp.ok) {
            resp.data = database[id];
            cout << "Клиент " << req.clientPid
                << " начал чтение записи " << id
                << " (читателей: " << lock->readerCount << ")" << endl;
        }
        else {
            resp.data = employee{};
            endRead(id); 
        }
        sendResponse(req.clientPid, resp);
        break;

    case CMD_WRITE_REQUEST:
        if (beginWrite(id)) {
            clientOperations[req.clientPid] = CMD_WRITE_REQUEST;
            resp.ok = database.count(id);
            if (resp.ok) {
                resp.data = database[id];
                cout << "Клиент " << req.clientPid
                    << " начал запись в запись " << id
                    << " (ожидает писателей: " << lock->writerWaiting << ")" << endl;
            }
            else {
                resp.data = employee{};
            }
        }
        else {
            resp.ok = false; 
            cout << "Клиент " << req.clientPid
                << " не смог получить доступ для записи " << id << " (таймаут)" << endl;
        }
        sendResponse(req.clientPid, resp);
        break;

    case CMD_WRITE_SUBMIT:
        if (database.count(id)) {
            database[id] = req.data;
            resp.ok = true;
            cout << "Клиент " << req.clientPid
                << " сохранил изменения записи " << id << endl;
        }
        else {
            resp.ok = false;
        }
        sendResponse(req.clientPid, resp);
        break;

    case CMD_FINISH_ACCESS:
        if (clientOperations.count(req.clientPid)) {
            int cmd = clientOperations[req.clientPid];
            if (cmd == CMD_READ) {
                endRead(id);
                cout << "Клиент " << req.clientPid
                    << " завершил чтение записи " << id << endl;
            }
            else if (cmd == CMD_WRITE_REQUEST) {
                endWrite(id);
                cout << "Клиент " << req.clientPid
                    << " завершил запись в запись " << id << endl;
            }
            clientOperations.erase(req.clientPid);
        }
        resp.ok = true;
        sendResponse(req.clientPid, resp);
        break;
    }
}

int main() {
    setlocale(LC_ALL, "rus");
    cout << "Сервер" << endl;;

    cout << "Введите имя файла: ";
    cin >> filename;

    cout << "Количество сотрудников: ";
    int n;
    cin >> n;

    ofstream f(filename, ios::binary | ios::trunc);
    if (!f.is_open()) {
        cout << "Ошибка создания файла!\n";
        return 1;
    }

    for (int i = 0; i < n; i++) {
        employee e;
        cout << "\nСотрудник " << (i + 1) << ":\n";
        cout << "  ID: ";
        cin >> e.num;
        cout << "  Имя (max 10 символов): ";
        cin >> e.name;
        cout << "  Часы: ";
        cin >> e.hours;
        f.write((char*)&e, sizeof(e));
        locks[e.num] = new RecordLock();
    }
    f.close();

    loadFile();
    printFile();

    cout << "\nСервер запущен. Ожидаю клиентов...\n";

    while (true) {
        hServerPipe = CreateNamedPipeA(
            "\\\\.\\pipe\\server_pipe",
            PIPE_ACCESS_INBOUND,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            sizeof(Request),
            sizeof(Response),
            0,
            NULL);

        if (hServerPipe == INVALID_HANDLE_VALUE) {
            cout << "Ошибка создания канала: " << GetLastError() << endl;
            break;
        }

        cout << "Ожидание подключения клиента...\n";
        if (!ConnectNamedPipe(hServerPipe, NULL)) {
            DWORD err = GetLastError();
            if (err != ERROR_PIPE_CONNECTED) {
                cout << "Ошибка подключения: " << err << endl;
                CloseHandle(hServerPipe);
                continue;
            }
        }

        Request req;
        DWORD bytesRead;
        if (ReadFile(hServerPipe, &req, sizeof(req), &bytesRead, NULL)) {
            if (req.cmd == CMD_EXIT) {
                cout << "Получена команда завершения работы\n";
                CloseHandle(hServerPipe);
                break;
            }

            processRequest(req);
        }
        else {
            cout << "Ошибка чтения запроса: " << GetLastError() << endl;
        }

        CloseHandle(hServerPipe);
    }

    saveFile();
    cout << "\nФинальное состояние файла:\n";
    printFile();

    for (auto& pair : locks) {
        delete pair.second;
    }
    locks.clear();

    cout << "\nСервер завершил работу.\n";
    return 0;
}