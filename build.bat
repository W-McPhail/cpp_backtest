@echo off
REM Build with g++ when make isn't on PATH. Run from build/ folder.
REM Add your MinGW or g++ bin folder to PATH if g++ not found.

set CXX=g++
set INC=-I../include -I../strategies
set CFLAGS=-std=c++17 -Wall %INC%

echo Compiling...
%CXX% %CFLAGS% -c ../src/main.cpp -o main.o
%CXX% %CFLAGS% -c ../src/data_source.cpp -o data_source.o
%CXX% %CFLAGS% -c ../src/simulator.cpp -o simulator.o
%CXX% %CFLAGS% -c ../src/backtester.cpp -o backtester.o
%CXX% %CFLAGS% -c ../src/report.cpp -o report.o
%CXX% %CFLAGS% -c ../strategies/example_sma_strategy.cpp -o example_sma_strategy.o
%CXX% %CFLAGS% -c ../strategies/ctm_strategy_simple.cpp -o ctm_strategy_simple.o
%CXX% %CFLAGS% -c ../strategies/orb_strategy.cpp -o orb_strategy.o

echo Linking...
%CXX% -o backtester.exe main.o data_source.o simulator.o backtester.o report.o example_sma_strategy.o ctm_strategy_simple.o orb_strategy.o

%CXX% %CFLAGS% -c ../tests/test_runner.cpp -o test_runner.o
%CXX% -o test_runner.exe test_runner.o data_source.o simulator.o

if %ERRORLEVEL% equ 0 (
  echo Done. Run: backtester.exe  or  test_runner.exe
) else (
  echo Build failed. Is g++ on your PATH? Try: where g++
  exit /b 1
)
