#include <windows.h>
#include <iostream>
#include <thread>
#include <cstring>

constexpr size_t PAGE_SIZE = 16384;

// ---------------- Write one page at offset ----------------
void write_page(HANDLE hFile, int page_id, const char* text) {
    char buffer[PAGE_SIZE];
    memset(buffer, 0, PAGE_SIZE);
    strncpy(buffer, text, PAGE_SIZE - 1);

    OVERLAPPED ov = {};
    ov.Offset = page_id * PAGE_SIZE;
    ov.OffsetHigh = 0;

    DWORD bytesWritten = 0;
    BOOL ok = WriteFile(
        hFile,
        buffer,
        PAGE_SIZE,
        &bytesWritten,
        &ov
    );

    if (!ok || bytesWritten != PAGE_SIZE) {
        std::cerr << "WriteFile failed for page " << page_id << "\n";
    }
}

// ---------------- Read and print one page ----------------
void read_page(HANDLE hFile, int page_id) {
    char buffer[PAGE_SIZE];
    memset(buffer, 0, PAGE_SIZE);

    OVERLAPPED ov = {};
    ov.Offset = page_id * PAGE_SIZE;
    ov.OffsetHigh = 0;

    DWORD bytesRead = 0;
    BOOL ok = ReadFile(
        hFile,
        buffer,
        PAGE_SIZE,
        &bytesRead,
        &ov
    );

    if (!ok || bytesRead != PAGE_SIZE) {
        std::cerr << "ReadFile failed for page " << page_id << "\n";
        return;
    }

    std::cout << "---- PAGE " << page_id << " ----\n";
    std::cout << buffer << "\n";
    std::cout << "------------------\n";
}

int main() {
    HANDLE hFile = CreateFile(
        "demo.ibd",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to open file\n";
        return 1;
    }

    // Concurrent writes
    std::thread t1(write_page, hFile, 1, "PAGE 1 written by THREAD 1");
    std::thread t2(write_page, hFile, 5, "PAGE 5 written by THREAD 2");

    t1.join();
    t2.join();

    std::cout << "\nConcurrent writes done.\n\n";

    // Read back and print
    read_page(hFile, 1);
    read_page(hFile, 5);

    CloseHandle(hFile);
    return 0;
}
