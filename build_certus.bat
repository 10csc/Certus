@echo off
call D:\vs\VC\Auxiliary\Build\vcvars64.bat
cd /d F:\CodeFile\Certus
cmake -B build -DCMAKE_PREFIX_PATH=F:/Qt/6.5.3/msvc2019_64 -DCMAKE_BUILD_TYPE=Release
