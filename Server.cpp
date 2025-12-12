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

    RecordLock() {
        readerCount = 0;
        writerCount = 0;
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
    if (!lock) return false;

    WaitForSingleObject(lock->mutex, INFINITE);

    if (lock->writerCount > 0) {
        ReleaseMutex(lock->mutex);
        return false;
    }

    lock->readerCount++;
    ReleaseMutex(lock->mutex);
    return true;
}

void endRead(int id) {
    RecordLock* lock = locks[id];
    if (!lock) return;

    WaitForSingleObject(lock->mutex, INFINITE);
    lock->readerCount--;
    ReleaseMutex(lock->mutex);
}

bool beginWrite(int id) {
    RecordLock* lock = locks[id];
    if (!lock) return false;

    if (WaitForSingleObject(lock->writeSemaphore, 0) != WAIT_OBJECT_0) {
        return false;
    }

    WaitForSingleObject(lock->mutex, INFINITE);

    while (lock->readerCount > 0) {
        ReleaseMutex(lock->mutex);
        Sleep(10);
        WaitForSingleObject(lock->mutex, INFINITE);
    }

    lock->writerCount = 1;
    ReleaseMutex(lock->mutex);
    return true;
}

void endWrite(int id) {
    RecordLock* lock = locks[id];
    if (!lock) return;

    WaitForSingleObject(lock->mutex, INFINITE);
    lock->writerCount = 0;
    ReleaseMutex(lock->mutex);

    ReleaseSemaphore(lock->writeSemaphore, 1, NULL);
}

void loadFile() {
    try {
        ifstream f(filename, ios::binary);
        if (!f.is_open()) {
            cout << "Не удалось открыть файл для чтения\n";
            cout.flush();
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
    catch (const exception& e) {
        cout << "Ошибка при загрузке файла: " << e.what() << endl;
        cout.flush();
    }
}

void saveFile() {
    try {
        ofstream f(filename, ios::binary | ios::trunc);
        if (!f.is_open()) {
            cout << "Не удалось открыть файл для записи\n";
            cout.flush();
            return;
        }

        for (auto& p : database) {
            f.write((char*)&p.second, sizeof(employee));
        }
        f.close();
    }
    catch (const exception& e) {
        cout << "Ошибка при сохранении файла: " << e.what() << endl;
        cout.flush();
    }
}

void printFile() {
    try {
        cout << "\nСодержимое файла:\n";
        cout.flush();
        cout << "ID\tИмя\tЧасы\n";
        cout.flush();
        cout << "----------------------\n";
        cout.flush();
        for (auto& p : database) {
            cout << p.second.num << "\t"
                << p.second.name << "\t"
                << p.second.hours << endl;
            cout.flush();
        }
    }
    catch (const exception& e) {
        cout << "Ошибка при выводе данных: " << e.what() << endl;
        cout.flush();
    }
}

void sendResponse(DWORD pid, const Response& resp) {
    try {
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
            cout.flush();
        }
    }
    catch (const exception& e) {
        cout << "Ошибка при отправке ответа клиенту " << pid << ": " << e.what() << endl;
        cout.flush();
    }
}

void processRequest(const Request& req) {
    try {
        Response resp{};
        int id = req.id;

        if (!locks[id]) {
            locks[id] = new RecordLock();
        }

        RecordLock* lock = locks[id];

        switch (req.cmd) {
        case CMD_READ:
            if (beginRead(id)) {
                clientOperations[req.clientPid] = CMD_READ;
                resp.ok = database.count(id);
                if (resp.ok) {
                    resp.data = database[id];
                    cout << "Клиент " << req.clientPid
                        << " начал чтение записи " << id
                        << " (читателей: " << lock->readerCount << ")" << endl;
                    cout.flush();
                }
                else {
                    resp.data = employee{};
                    endRead(id);
                }
            }
            else {
                resp.ok = false;
                cout << "Клиент " << req.clientPid
                    << " не смог прочитать запись " << id << " (занята писателем)" << endl;
                cout.flush();
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
                        << " начал запись в запись " << id << endl;
                    cout.flush();
                }
                else {
                    resp.data = employee{};
                }
            }
            else {
                resp.ok = false;
                cout << "Клиент " << req.clientPid
                    << " не смог получить доступ для записи " << id << " (занято)" << endl;
                cout.flush();
            }
            sendResponse(req.clientPid, resp);
            break;

        case CMD_WRITE_SUBMIT:
            if (database.count(id)) {
                database[id] = req.data;
                resp.ok = true;
                cout << "Клиент " << req.clientPid
                    << " сохранил изменения записи " << id << endl;
                cout.flush();
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
                    cout.flush();
                }
                else if (cmd == CMD_WRITE_REQUEST) {
                    endWrite(id);
                    cout << "Клиент " << req.clientPid
                        << " завершил запись в запись " << id << endl;
                    cout.flush();
                }
                clientOperations.erase(req.clientPid);
            }
            resp.ok = true;
            sendResponse(req.clientPid, resp);
            break;
        }
    }
    catch (const exception& e) {
        cout << "Ошибка при обработке запроса: " << e.what() << endl;
        cout.flush();
    }
}

int main() {
    try {
        setlocale(LC_ALL, "rus");

        cout << " Сервер " << endl;
        cout.flush();

        cout << "Введите имя файла: ";
        cout.flush();
        cin >> filename;

        cout << "Количество сотрудников: ";
        cout.flush();
        int n;
        cin >> n;

        ofstream f(filename, ios::binary | ios::trunc);
        if (!f.is_open()) {
            cout << "Ошибка создания файла!\n";
            cout.flush();
            return 1;
        }

        for (int i = 0; i < n; i++) {
            employee e;
            cout << "\nСотрудник " << (i + 1) << ":\n";
            cout.flush();
            cout << "  ID: ";
            cout.flush();
            cin >> e.num;
            cout << "  Имя (max 10 символов): ";
            cout.flush();
            cin >> e.name;
            cout << "  Часы: ";
            cout.flush();
            cin >> e.hours;
            f.write((char*)&e, sizeof(e));
            locks[e.num] = new RecordLock();
        }
        f.close();

        loadFile();
        printFile();

        cout << "\nВведите количество клиентов: ";
        cout.flush();
        int clientCount;
        cin >> clientCount;
        cout << "Ожидаю до " << clientCount << " клиентов...\n";
        cout.flush();

        cout << "\nСервер запущен. Ожидаю клиентов...\n";
        cout.flush();

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
                cout.flush();
                break;
            }

            cout << "Ожидание подключения клиента..." << endl;
            cout.flush();
            if (!ConnectNamedPipe(hServerPipe, NULL)) {
                DWORD err = GetLastError();
                if (err != ERROR_PIPE_CONNECTED) {
                    cout << "Ошибка подключения: " << err << endl;
                    cout.flush();
                    CloseHandle(hServerPipe);
                    continue;
                }
            }

            cout << "Клиент подключен" << endl;
            cout.flush();

            Request req;
            DWORD bytesRead;
            if (ReadFile(hServerPipe, &req, sizeof(req), &bytesRead, NULL)) {
                if (req.cmd == CMD_EXIT) {
                    cout << "Получена команда завершения работы" << endl;
                    cout.flush();
                    CloseHandle(hServerPipe);
                    break;
                }

                processRequest(req);
            }
            else {
                cout << "Ошибка чтения запроса: " << GetLastError() << endl;
                cout.flush();
            }

            CloseHandle(hServerPipe);
        }

        saveFile();
        cout << "\nФинальное состояние файла:\n";
        cout.flush();
        printFile();

        for (auto& pair : locks) {
            delete pair.second;
        }
        locks.clear();

        cout << "\nСервер завершил работу.\n";
        cout.flush();
        return 0;
    }
    catch (const exception& e) {
        cout << "Критическая ошибка в работе сервера: " << e.what() << endl;
        cout.flush();

        for (auto& pair : locks) {
            delete pair.second;
        }
        locks.clear();

        if (hServerPipe != INVALID_HANDLE_VALUE && hServerPipe != NULL) {
            CloseHandle(hServerPipe);
        }

        return 1;
    }
}