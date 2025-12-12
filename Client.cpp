#include <windows.h>
#include <iostream>
#include <string>
#include "employee.h"
using namespace std;

bool sendRequest(const Request& req, Response& resp) {
    try {
        string responsePipeName = "\\\\.\\pipe\\client_pipe_" + to_string(req.clientPid);

        HANDLE hResponsePipe = CreateNamedPipeA(
            responsePipeName.c_str(),
            PIPE_ACCESS_INBOUND,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1,
            sizeof(Response),
            sizeof(Response),
            0,
            NULL);

        if (hResponsePipe == INVALID_HANDLE_VALUE) {
            cout << "Ошибка создания клиентского канала\n";
            return false;
        }

        HANDLE hServerPipe = CreateFileA(
            "\\\\.\\pipe\\server_pipe",
            GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            0,
            NULL);

        if (hServerPipe == INVALID_HANDLE_VALUE) {
            cout << "Сервер не запущен!\n";
            CloseHandle(hResponsePipe);
            return false;
        }

        DWORD written;
        if (!WriteFile(hServerPipe, &req, sizeof(req), &written, NULL)) {
            cout << "Ошибка отправки запроса\n";
            CloseHandle(hServerPipe);
            CloseHandle(hResponsePipe);
            return false;
        }
        CloseHandle(hServerPipe);

        ConnectNamedPipe(hResponsePipe, NULL);

        DWORD bytesRead;
        if (!ReadFile(hResponsePipe, &resp, sizeof(resp), &bytesRead, NULL)) {
            cout << "Ошибка чтения ответа\n";
            CloseHandle(hResponsePipe);
            return false;
        }

        CloseHandle(hResponsePipe);
        return true;
    }
    catch (const exception& e) {
        cout << "Ошибка при отправке запроса: " << e.what() << endl;
        return false;
    }
}

void printRecord(const employee& e) {
    try {
        cout << "\n=== Запись сотрудника ===\n";
        cout << "ID: " << e.num << endl;
        cout << "Имя: " << e.name << endl;
        cout << "Часы: " << e.hours << endl;
        cout << "==========================\n";
    }
    catch (const exception& e) {
        cout << "Ошибка при выводе записи: " << e.what() << endl;
    }
}

int main() {
    try {
        setlocale(LC_ALL, "rus");
        cout << " Клиент \n";
        cout << "PID процесса: " << GetCurrentProcessId() << "\n\n";

        DWORD pid = GetCurrentProcessId();

        while (true) {
            try {
                cout << "\nМеню:\n";
                cout << "1 - Чтение записи\n";
                cout << "2 - Модификация записи\n";
                cout << "3 - Выход\n";
                cout << "Выберите действие: ";

                int choice;
                cin >> choice;

                if (choice == 3) {
                    Request req{};
                    req.cmd = CMD_EXIT;
                    req.clientPid = pid;
                    Response resp;
                    sendRequest(req, resp);
                    break;
                }

                if (choice != 1 && choice != 2) {
                    cout << "Неверный выбор! Пожалуйста, выберите 1, 2 или 3.\n";
                    continue;
                }

                cout << "Введите ID сотрудника: ";
                int id;
                cin >> id;

                if (choice == 1) {
                    Request req{};
                    req.cmd = CMD_READ;
                    req.id = id;
                    req.clientPid = pid;

                    Response resp;
                    cout << "Пытаюсь прочитать запись " << id << "...\n";

                    if (!sendRequest(req, resp)) {
                        cout << "Ошибка соединения\n";
                        continue;
                    }

                    if (!resp.ok) {
                        if (resp.data.num == 0) {
                            cout << "Запись не найдена!\n";
                        }
                        else {
                            cout << "Запись занята писателем! Попробуйте позже.\n";
                        }
                        continue;
                    }

                    printRecord(resp.data);

                    cout << "Нажмите Enter для завершения чтения...";
                    cin.ignore();
                    cin.get();

                    req.cmd = CMD_FINISH_ACCESS;
                    sendRequest(req, resp);
                }
                else if (choice == 2) {
                    Request req{};
                    req.cmd = CMD_WRITE_REQUEST;
                    req.id = id;
                    req.clientPid = pid;

                    Response resp;
                    cout << "Пытаюсь получить доступ для записи " << id << "...\n";

                    if (!sendRequest(req, resp)) {
                        cout << "Ошибка соединения\n";
                        continue;
                    }

                    if (!resp.ok) {
                        cout << "Запись занята.\n";
                        continue;
                    }

                    cout << "\nТекущие данные:\n";
                    printRecord(resp.data);

                    employee modified = resp.data;
                    cout << "\nВведите новые данные:\n";
                    cout << "Новое имя (макс 10 символов): ";
                    cin >> modified.name;
                    cout << "Новое количество часов: ";
                    cin >> modified.hours;

                    cout << "\nСохранить изменения? (1 - Да, 0 - Нет): ";
                    int confirm;
                    cin >> confirm;

                    if (confirm == 1) {
                        req.cmd = CMD_WRITE_SUBMIT;
                        req.data = modified;
                        if (!sendRequest(req, resp)) {
                            cout << "Ошибка при сохранении изменений\n";
                        }
                        else if (resp.ok) {
                            cout << "Изменения сохранены успешно!\n";
                        }
                    }
                    else {
                        cout << "Изменения отменены\n";
                    }

                    cout << "Нажмите Enter для завершения записи...";
                    cin.ignore();
                    cin.get();

                    req.cmd = CMD_FINISH_ACCESS;
                    req.data = employee{};
                    sendRequest(req, resp);
                }
            }
            catch (const exception& e) {
                cout << "Ошибка в меню: " << e.what() << endl;
                cin.clear();
                cin.ignore();
            }
        }

        cout << "\nКлиент завершил работу.\n";
        return 0;
    }
    catch (const exception& e) {
        cout << "Критическая ошибка в клиенте: " << e.what() << endl;
        return 1;
    }
}