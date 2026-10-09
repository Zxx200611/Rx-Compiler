# ANTLR4 4.13.2 工具链（WSL Ubuntu / C++）

此目录固定使用 **ANTLR 4.13.2**，与课程 `template/grammar/Lexer.g4` 和 `Parser.g4` 的版本对应。生成工具与 C++ runtime 版本相同，生成的 C++ 代码使用 C++17。所有下列命令都在 **WSL Ubuntu 的终端**运行。

## 1. 目录和依赖来源

```text
tools/
├── antlr-4.13.2-complete.jar             # 官方 Java 生成工具
├── antlr4-cpp-runtime-4.13.2-source.zip  # 官方原始源码包
├── antlr4-cpp-runtime-4.13.2/            # 已解压的官方源码（含 LICENSE.txt）
├── SHA256SUMS                          # 下载文件的 SHA-256 记录
├── setup-antlr4.sh                     # 构建并在本目录安装 runtime
├── generate-antlr4.sh                  # 单独生成 C++ lexer/parser/visitor
├── verify-antlr4.sh                    # 生成、链接并验证示例
├── cmake/RxAntlr4.cmake                # 正式工程可复用的 CMake 接入模块
├── smoke/                             # 验证依赖的最小解析示例
├── build/                             # 本机生成的构建目录，不提交
├── installed/antlr4-4.13.2/            # 本机 Linux runtime，不提交
└── generated/                         # 单独生成脚本的默认输出，不提交
```

下载来源：

- [ANTLR 4.13.2 complete JAR](https://www.antlr.org/download/antlr-4.13.2-complete.jar)
- [ANTLR 4.13.2 C++ runtime 源码包](https://www.antlr.org/download/antlr4-cpp-runtime-4.13.2-source.zip)

`SHA256SUMS` 记录本次官方 HTTPS 下载文件的哈希，用于之后检查文件是否变化，不是另外获取的官方签名。可复查：

```sh
cd tools
sha256sum --check SHA256SUMS
cd ..
```

runtime 源码和源码包保留官方许可。构建采用静态库，不需要设置 `LD_LIBRARY_PATH`，也不会将 runtime 安装到 Ubuntu 的 `/usr/local`。上游的 demo 和 GoogleTest 测试关闭，因此已准备好源码后的 runtime 构建无需联网。

## 2. WSL 环境

当前检查的环境是 Ubuntu 22.04（WSL 1），已有 GCC 11.4、CMake 3.22 和 Ninja；本次为 ANTLR 生成工具安装了 OpenJDK 17。当前 WSL 版本可以完成本目录的生成和编译。

换到新的 Ubuntu 环境时，先安装这些依赖：

```sh
sudo apt update
sudo apt install -y build-essential cmake ninja-build openjdk-17-jre-headless
```

不需要安装 Ubuntu 仓库中的 `antlr4` 或 `libantlr4-runtime-dev`，本工程使用这里固定的 4.13.2。Java 用于开发时生成代码，最终 C++ 编译器运行不依赖 Java。

Windows 仓库路径 `H:\Homework\Rx-Compiler` 在当前 WSL 中对应：

```sh
cd /mnt/h/Homework/Rx-Compiler
```

如果后续把仓库克隆到 Ubuntu 文件系统中，例如 `~/Rx-Compiler`，进入该目录即可，脚本会按自身位置定位依赖，无需修改路径。不同操作系统或移动后的 CMake 构建缓存不要混用；重新构建 runtime 与示例。所有脚本均通过 `bash` 调用，不依赖 Windows 挂载盘上的可执行权限。

## 3. 构建和验证

在仓库根目录执行：

```sh
bash tools/setup-antlr4.sh
bash tools/verify-antlr4.sh
```

第一条校验下载文件，构建 runtime 并安装到：

```text
tools/installed/antlr4-4.13.2/include/antlr4-runtime/
tools/installed/antlr4-4.13.2/lib/libantlr4-runtime.a
tools/installed/antlr4-4.13.2/lib/cmake/antlr4-runtime/
```

第二条会生成课程文法对应的 C++ 代码，编译并链接示例，然后检查：合法程序解析成功、缺失分号被拒绝、非法整数 token 被拒绝。示例结果是 ANTLR 解析树，供确认工具链接通；自定义 AST 构建仍按照 [Antlr4 Knowledge.md](../docs/Antlr4%20Knowledge.md) 后续实现。示例只做词法/语法检查，不做类型或名字检查，也不替代课程完整测试。

脚本可重复运行，CMake/Ninja 会复用当前构建缓存。默认并行度为 4；内存不足时可以降低：

```sh
ANTLR_BUILD_JOBS=2 bash tools/setup-antlr4.sh
ANTLR_BUILD_JOBS=2 bash tools/verify-antlr4.sh
```

依赖示例可读取自己的完整 Rx 程序：

```sh
tools/build/smoke/rx-parse-smoke tools/smoke/valid.rx
tools/build/smoke/rx-parse-smoke path/to/program.rx
```

成功时退出码为 0，并打印解析树；拒绝时退出码为 1，诊断写到 stderr。示例固定调用 `crate()`，不用于解析测试库中 `expression`、`typeRef` 等语法片段。

## 4. 单独生成 C++ 代码

在仓库根目录执行：

```sh
bash tools/generate-antlr4.sh
```

默认写入 `tools/generated/`。也可以选择输出目录，相对路径按调用时的工作目录解释：

```sh
bash tools/generate-antlr4.sh build/generated
```

生成 Lexer 后再生成 Parser，以确保 Parser 找到 `RxLexer.tokens`。启用 `-visitor`，关闭 Listener，并用 `-package rxantlr` 将生成的类放入 `rxantlr` 命名空间。主要代码文件是 `RxLexer.{h,cpp}`、`RxParser.{h,cpp}`、`RxParserVisitor.{h,cpp}`、`RxParserBaseVisitor.{h,cpp}`。生成目录不要手工编辑；修改文法或 AstBuilder 后重新构建。

也可手工调用 JAR，例如查看工具版本与帮助：

```sh
java -jar tools/antlr-4.13.2-complete.jar
```

输出开头应为 `ANTLR Parser Generator Version 4.13.2`。不带文法时只显示帮助，不生成分析器。

## 5. 正式 C++ 工程的 CMake 接入

以后在仓库根目录建立 `CMakeLists.txt` 时，可以直接复用本目录模块。以下示例假设你已经编写了 `src/main.cpp`：

```cmake
cmake_minimum_required(VERSION 3.15)
project(RxCompiler LANGUAGES CXX)

include("${CMAKE_CURRENT_SOURCE_DIR}/tools/cmake/RxAntlr4.cmake")
rx_add_antlr4(rx_antlr4)

add_executable(rx-compiler src/main.cpp)
target_link_libraries(rx-compiler PRIVATE rx_antlr4)
```

然后在 WSL 的仓库根目录构建：

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
```

模块自动查找项目内安装的 **精确 4.13.2 runtime** 与 Java 11+，先生成 Lexer，再生成 Parser，并将生成代码编译为 `rx_antlr4` 静态库。文法、JAR 或生成脚本变化会触发必要的重新生成。

只需链接 `rx_antlr4`，即可继承生成目录、runtime 头文件搜索路径、静态库与线程链接依赖，以及 C++17 要求。生成目录位于本次 CMake 构建目录的 `rx_antlr4-generated/`，与手工生成脚本的 `tools/generated/` 独立；正式工程无需先运行手工生成脚本。

自己的 C++ 代码可以这样包含头文件：

```cpp
#include "antlr4-runtime.h"
#include "RxLexer.h"
#include "RxParser.h"
#include "RxParserBaseVisitor.h"
```

生成类名是 `rxantlr::RxLexer`、`rxantlr::RxParser`、`rxantlr::RxParserBaseVisitor`；runtime 基类是 `antlr4::Lexer`、`antlr4::Parser`。课程文法的名字为 Lexer/Parser，直接生成到全局命名空间时，生成的 C++ 文件内部也会出现与 runtime 基类的名字冲突，仅在自己的调用处加 `::` 不足以解决；`-package rxantlr` 本身也不足以解决，因为生成的 `.cpp` 同时导入两个命名空间。因此脚本在生成目录建立 `RxLexer.g4` / `RxParser.g4` 副本，只改文法名称和 `tokenVocab`，再配合 `-package rxantlr` 生成；课程原文法与规则保持不变。Visitor 接口使用 `std::any`，不要照搬旧版本 `antlrcpp::Any` 示例。

此处没有创建根目录正式编译器工程；已经可运行的接入实例是 `tools/smoke/CMakeLists.txt`。

## 6. 手工编译示例

如果暂时不使用 CMake 接入模块，在仓库根目录生成后，也可用 Ubuntu 的 g++ 手工链接验证程序：

```sh
bash tools/generate-antlr4.sh
g++ -std=c++17 \
    -Itools/generated \
    -Itools/installed/antlr4-4.13.2/include/antlr4-runtime \
    tools/smoke/main.cpp \
    tools/generated/RxLexer.cpp \
    tools/generated/RxParser.cpp \
    tools/generated/RxParserVisitor.cpp \
    tools/generated/RxParserBaseVisitor.cpp \
    tools/installed/antlr4-4.13.2/lib/libantlr4-runtime.a \
    -pthread -o tools/build/rx-parse-manual
tools/build/rx-parse-manual tools/smoke/valid.rx
```

先执行过 `setup-antlr4.sh`，因此 `tools/build/` 和安装目录应已存在。直接链接静态库时，库放在使用它的源文件/目标文件之后。

## 7. 常见问题

- **找不到 `java`**：在 Ubuntu 安装 OpenJDK；Windows 的 Java 安装不等于 WSL 内已有 Java。
- **找不到 runtime 包**：先运行 `bash tools/setup-antlr4.sh`。接入模块固定使用仓库内安装前缀。
- **JAR/runtime 版本不匹配**：一起使用本目录的 4.13.2，并重新生成代码。不要混用系统包或其他版本生成结果。
- **构建缓存指向旧路径或 Windows 编译器**：换用新的构建目录；本目录缓存只用于当前 Linux 环境。
- **脚本出现 `\r` 或 `bad interpreter`**：确保自己的 shell 文件保存为 LF 换行；本目录 `.gitattributes` 已固定脚本为 LF。
- **解析失败却仍返回树**：ANTLR 默认可能恢复后返回树；按照知识文档检查 Lexer 错误 token 和 Parser 错误状态后再构建 AST。
