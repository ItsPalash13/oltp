@echo off
cd /d "%~dp0"
echo Building server.exe...

set ROOT=..
set INC=%ROOT%/include
set SRC=%ROOT%/src
set SERVER=.
set NETWORK=%SRC%/network
set ORCH=%SRC%/orchestrator
set ANALYSER=%SRC%/analyser
set PLANNER=%SRC%/planner
set EXEC=%SRC%/executor
set STORAGE=%SRC%/storage
set TXN=%SRC%/transaction

g++ -std=c++17 -I%INC% -c %SERVER%/main.cpp -o main.o
if %ERRORLEVEL% NEQ 0 exit /b 1

g++ -std=c++17 -I%INC% -c %NETWORK%/tcp_server.cpp -o tcp_server.o
if %ERRORLEVEL% NEQ 0 exit /b 1

g++ -std=c++17 -I%INC% -c %NETWORK%/select_server.cpp -o select_server.o
if %ERRORLEVEL% NEQ 0 exit /b 1

g++ -std=c++17 -I%INC% -c %ORCH%/orchestrator.cpp -o orchestrator.o
if %ERRORLEVEL% NEQ 0 exit /b 1

g++ -std=c++17 -I%INC% -c %ANALYSER%/analyser.cpp -o analyser.o
if %ERRORLEVEL% NEQ 0 exit /b 1
g++ -std=c++17 -I%INC% -c %ANALYSER%/expr_utils.cpp -o expr_utils.o
if %ERRORLEVEL% NEQ 0 exit /b 1

g++ -std=c++17 -I%INC% -c %PLANNER%/planner.cpp -o planner.o
if %ERRORLEVEL% NEQ 0 exit /b 1

g++ -std=c++17 -I%INC% -c %EXEC%/executor.cpp -o executor.o
if %ERRORLEVEL% NEQ 0 exit /b 1
g++ -std=c++17 -I%INC% -c %EXEC%/executors.cpp -o executors.o
if %ERRORLEVEL% NEQ 0 exit /b 1
g++ -std=c++17 -I%INC% -c %EXEC%/executor_factory.cpp -o executor_factory.o
if %ERRORLEVEL% NEQ 0 exit /b 1
g++ -std=c++17 -I%INC% -c %EXEC%/evaluator.cpp -o evaluator.o
if %ERRORLEVEL% NEQ 0 exit /b 1
g++ -std=c++17 -I%INC% -c %EXEC%/expr_defs.cpp -o expr_defs.o
if %ERRORLEVEL% NEQ 0 exit /b 1
g++ -std=c++17 -I%INC% -c %EXEC%/storage.cpp -o storage_executor.o
if %ERRORLEVEL% NEQ 0 exit /b 1
g++ -std=c++17 -I%INC% -c %EXEC%/tuple_codec.cpp -o tuple_codec.o
if %ERRORLEVEL% NEQ 0 exit /b 1
g++ -std=c++17 -I%INC% -c %EXEC%/seq_scan_cursor.cpp -o seq_scan_cursor.o
if %ERRORLEVEL% NEQ 0 exit /b 1

g++ -std=c++17 -I%INC% -c %STORAGE%/storage_manager.cpp -o storage_manager.o
if %ERRORLEVEL% NEQ 0 exit /b 1
g++ -std=c++17 -I%INC% -c %STORAGE%/schema_serializer.cpp -o schema_serializer.o
if %ERRORLEVEL% NEQ 0 exit /b 1
g++ -std=c++17 -I%INC% -c %STORAGE%/db_manager.cpp -o db_manager.o
if %ERRORLEVEL% NEQ 0 exit /b 1
g++ -std=c++17 -I%INC% -c %STORAGE%/catalog_manager.cpp -o catalog_manager.o
if %ERRORLEVEL% NEQ 0 exit /b 1
g++ -std=c++17 -I%INC% -c %STORAGE%/bufferpool_manager.cpp -o bufferpool_manager.o
if %ERRORLEVEL% NEQ 0 exit /b 1
g++ -std=c++17 -I%INC% -c %STORAGE%/page_layout.cpp -o page_layout.o
if %ERRORLEVEL% NEQ 0 exit /b 1
g++ -std=c++17 -I%INC% -c %STORAGE%/bplustree.cpp -o bplustree.o
if %ERRORLEVEL% NEQ 0 exit /b 1
g++ -std=c++17 -I%INC% -c %TXN%/transaction_manager.cpp -o transaction_manager.o
if %ERRORLEVEL% NEQ 0 exit /b 1

g++ -std=c++17 -pthread -Wl,--allow-multiple-definition main.o tcp_server.o select_server.o orchestrator.o analyser.o expr_utils.o planner.o executor.o executors.o executor_factory.o evaluator.o expr_defs.o storage_executor.o storage_manager.o schema_serializer.o db_manager.o catalog_manager.o bufferpool_manager.o page_layout.o bplustree.o seq_scan_cursor.o tuple_codec.o transaction_manager.o -lws2_32 -o server.exe
if %ERRORLEVEL% NEQ 0 exit /b 1

echo Built server\server.exe
