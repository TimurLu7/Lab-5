#include "pch.h"
#include "CppUnitTest.h"
#include <map>
#include <string>
#include "employee.h"
#include <fstream>

class MockServerLogic {
private:
    std::map<int, employee> database;
    std::map<int, bool> recordLocks;
    std::map<DWORD, int> clientOperations;

public:
    MockServerLogic() = default;

    void loadTestData() {
        employee emp1{ 1, "John", 40.5 };
        employee emp2{ 2, "Alice", 35.0 };
        employee emp3{ 3, "Bob", 42.0 };

        database[1] = emp1;
        database[2] = emp2;
        database[3] = emp3;
    }

    bool readRecord(int id, employee& result) {
        if (recordLocks[id]) return false; 

        auto it = database.find(id);
        if (it != database.end()) {
            result = it->second;
            recordLocks[id] = true; 
            return true;
        }
        return false;
    }

    bool writeRecord(int id, const employee& emp) {
        if (recordLocks[id]) return false; 

        recordLocks[id] = true;
        database[id] = emp;
        return true;
    }

    void releaseLock(int id) {
        recordLocks[id] = false;
    }

    bool recordExists(int id) const {
        return database.find(id) != database.end();
    }

    size_t getRecordCount() const {
        return database.size();
    }
};

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTestLab5
{
    TEST_CLASS(EmployeeTests)
    {
    public:

        TEST_METHOD(TestEmployeeSize)
        {
            size_t minSize = sizeof(int) + 10 * sizeof(char) + sizeof(double);
            size_t actualSize = sizeof(employee);

            Assert::IsTrue(actualSize >= minSize,
                L"Размер структуры меньше ожидаемого минимального");

            Assert::IsTrue(actualSize <= 64,
                L"Размер структуры слишком большой");   
        }

        TEST_METHOD(TestEmployeeInitialization)
        {
            employee emp{};
            emp.num = 1;
            strcpy_s(emp.name, "John");
            emp.hours = 40.5;

            Assert::AreEqual(1, emp.num);
            Assert::AreEqual("John", emp.name);
            Assert::AreEqual(40.5, emp.hours);
        }

        TEST_METHOD(TestRequestResponseSizes)
        {
            Assert::IsTrue(sizeof(Request) > 0);
            Assert::IsTrue(sizeof(Response) > 0);
        }
    };
    TEST_CLASS(FileIOTests)
    {
    public:

        TEST_METHOD(TestEmployeeBinaryWriteRead)
        {
            const char* testFileName = "test_employee.bin";

            {
                employee emp{ 1, "TestUser", 45.5 };
                std::ofstream file(testFileName, std::ios::binary);
                Assert::IsTrue(file.is_open());

                file.write(reinterpret_cast<const char*>(&emp), sizeof(employee));
                file.close();
            }

            {
                employee readEmp{};
                std::ifstream file(testFileName, std::ios::binary);
                Assert::IsTrue(file.is_open());

                file.read(reinterpret_cast<char*>(&readEmp), sizeof(employee));
                file.close();

                Assert::AreEqual(1, readEmp.num);
                Assert::AreEqual("TestUser", readEmp.name);
                Assert::AreEqual(45.5, readEmp.hours);
            }
        }

        TEST_METHOD(TestMultipleEmployeesWriteRead)
        {
            const char* testFileName = "test_employees.bin";
            const int numEmployees = 3;

            {
                std::ofstream file(testFileName, std::ios::binary);
                Assert::IsTrue(file.is_open());

                employee employees[numEmployees] = {
                    {1, "First", 10.0},
                    {2, "Second", 20.0},
                    {3, "Third", 30.0}
                };

                for (int i = 0; i < numEmployees; i++) {
                    file.write(reinterpret_cast<const char*>(&employees[i]),
                        sizeof(employee));
                }
                file.close();
            }

            {
                std::ifstream file(testFileName, std::ios::binary);
                Assert::IsTrue(file.is_open());

                for (int i = 0; i < numEmployees; i++) {
                    employee readEmp{};
                    file.read(reinterpret_cast<char*>(&readEmp), sizeof(employee));

                    Assert::AreEqual(i + 1, readEmp.num);
                    Assert::AreEqual(readEmp.hours, (i + 1) * 10.0);
                }

                file.close();
            }
        }

        TEST_METHOD(TestFileOpenFailure)
        {
            std::ifstream file("nonexistent_file_12345.bin", std::ios::binary);
            Assert::IsFalse(file.is_open());
        }
    };
    TEST_CLASS(IntegrationTests)
    {
    public:

        TEST_METHOD(TestCommandTypesValues)
        {
            Assert::AreEqual(0, (int)CMD_READ);
            Assert::AreEqual(1, (int)CMD_WRITE_REQUEST);
            Assert::AreEqual(2, (int)CMD_WRITE_SUBMIT);
            Assert::AreEqual(3, (int)CMD_FINISH_ACCESS);
            Assert::AreEqual(4, (int)CMD_EXIT);
        }

        TEST_METHOD(TestResponseStructure)
        {
            Response resp{};
            resp.ok = true;
            resp.data.num = 100;
            strcpy_s(resp.data.name, "Test");
            resp.data.hours = 50.5;

            Assert::IsTrue(resp.ok);
            Assert::AreEqual(100, resp.data.num);
            Assert::AreEqual("Test", resp.data.name);
            Assert::AreEqual(50.5, resp.data.hours);
        }

        TEST_METHOD(TestRequestStructure)
        {
            Request req{};
            req.cmd = CMD_READ;
            req.id = 42;
            req.clientPid = 12345;
            req.data.num = 1;
            strcpy_s(req.data.name, "ReqData");
            req.data.hours = 25.0;

            Assert::AreEqual((int)CMD_READ, (int)req.cmd);
            Assert::AreEqual(42, req.id);
            Assert::AreEqual((DWORD)12345, req.clientPid);
            Assert::AreEqual(1, req.data.num);
        }
    };
    TEST_CLASS(ServerLogicTests)
    {
    public:

        TEST_METHOD(TestDatabaseOperations)
        {
            MockServerLogic logic;
            logic.loadTestData();

            Assert::AreEqual((size_t)3, logic.getRecordCount());

            Assert::IsTrue(logic.recordExists(1));
            Assert::IsFalse(logic.recordExists(999));
        }

        TEST_METHOD(TestReadRecordSuccess)
        {
            MockServerLogic logic;
            logic.loadTestData();

            employee result{};
            bool success = logic.readRecord(1, result);

            Assert::IsTrue(success);
            Assert::AreEqual(1, result.num);
            Assert::AreEqual("John", result.name);
            Assert::AreEqual(40.5, result.hours);
        }

        TEST_METHOD(TestReadRecordNotFound)
        {
            MockServerLogic logic;
            logic.loadTestData();

            employee result{};
            bool success = logic.readRecord(999, result);

            Assert::IsFalse(success);
        }

        TEST_METHOD(TestWriteRecordSuccess)
        {
            MockServerLogic logic;
            logic.loadTestData();

            employee newEmp{ 4, "Charlie", 38.0 };
            bool success = logic.writeRecord(4, newEmp);

            Assert::IsTrue(success);
            Assert::IsTrue(logic.recordExists(4));
        }

        TEST_METHOD(TestConcurrentAccessPrevention)
        {
            MockServerLogic logic;
            logic.loadTestData();

            employee result1{};
            bool success1 = logic.readRecord(1, result1);
            Assert::IsTrue(success1);

            employee result2{};
            bool success2 = logic.readRecord(1, result2);
            Assert::IsFalse(success2);

            logic.releaseLock(1);
            bool success3 = logic.readRecord(1, result2);
            Assert::IsTrue(success3);
        }
    };
}
