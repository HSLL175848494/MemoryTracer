# 内存跟踪器 (HSLL::MemoryTracer)

一个轻量级的 C++ 内存泄漏检测工具，用于跟踪内存分配并捕获堆栈轨迹，帮助识别内存泄漏问题

## 特点

- 跟踪会话期间的所有 `new`/`delete` 操作
- 为每个内存分配捕获详细的堆栈轨迹
- 按调用堆栈对泄漏进行分组，便于分析
- 线程安全的实现
- 跨平台支持（Windows/Linux）

## 使用说明

### 编译配置要点(以cmake为例)

**必须**在编译时定义 `HS_ENABLE_TRACING` 宏来启用追踪功能：

```cmake
# CMake 配置示例
target_compile_definitions(${PROGRAM_NAME} PRIVATE HS_ENABLE_TRACING)
```

**必须**将 `HS_Leak.h` 包含路径添加到项目中，确保所有编译单元使用重载的内存操作函数：

```cmake
target_include_directories(${PROJECT_NAME} PRIVATE [HS_Leak.h所在目录])
```

### 平台特定的链接要求

- **Windows**: 需要链接 `dbghelp.lib`
- **Linux**: 需要链接 `-ldl` 库，推荐使用 `-rdynamic` 编译标志

## 代码示例

### 基本使用模式

```cpp
// 开始跟踪内存分配
HSLL::Utils::MemoryTracer::StartTracing();

// 执行需要检测的代码
// ...

// 停止跟踪并获取泄漏报告
std::string report = HSLL::Utils::MemoryTracer::EndTracing();

// 输出内存泄漏报告
std::cout << report << std::endl;
```

### 使用说明

- `StartTracing()` 和 `EndTracing()` 可以在任意线程中调用
- 并发调用 `EndTracing()` 时，只有第一次调用能获取报告
- 报告仅包含两个调用之间的内存分配情况

## 输出示例

```
================================================================================
                    MEMORY LEAK REPORT (GROUPED BY STACK TRACE)
================================================================================

LEAK GROUP 1:
  Leak Count: 100 allocations
  Total Size: 400 bytes
  Average Size: 4 bytes
  Stack Hash: 0x73d99140c79de33f
  Call Stack:
    # 0 HSLL::Utils::StackTraceCapturer::Capture
    # 1 HSLL::Utils::MemoryTracer::RecordAllocation
    # 2 operator new
    # 3 Leak1
    # 4 main
    # 5 invoke_main
    # 6 __scrt_common_main_seh
    # 7 __scrt_common_main
    # 8 mainCRTStartup
    # 9 BaseThreadInitThunk
    #10 RtlUserThreadStart

--------------------------------------------------------------------------------
LEAK GROUP 2:
  Leak Count: 100 allocations
  Total Size: 100 bytes
  Average Size: 1 bytes
  Stack Hash: 0x4f8e60ec5f5d1175
  Call Stack:
    # 0 HSLL::Utils::StackTraceCapturer::Capture
    # 1 HSLL::Utils::MemoryTracer::RecordAllocation
    # 2 operator new
    # 3 Leak2
    # 4 main
    # 5 invoke_main
    # 6 __scrt_common_main_seh
    # 7 __scrt_common_main
    # 8 mainCRTStartup
    # 9 BaseThreadInitThunk
    #10 RtlUserThreadStart

--------------------------------------------------------------------------------
SUMMARY:
  Total leak groups: 2
  Total allocations: 200
  Total leaked memory: 500 bytes
================================================================================
```