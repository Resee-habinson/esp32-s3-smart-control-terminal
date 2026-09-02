---
name: embedded-firmware
description: 设计、实现、修改或审查嵌入式 C/C++ 固件，覆盖 MCU/SoC、裸机或 RTOS、App/BSP/Drivers/Middleware/Platform 分层目录、教学型详细中文代码注释、硬件接口、状态机、API、错误处理、日志、资源约束和配套文档。用于固件功能开发、Bug 修复、代码评审、架构搭建或调整、接口设计、并发或 ISR 问题，以及 RAM、Flash、栈、CPU 和实时性分析；不用于与嵌入式系统无关的通用软件任务。
---

# 嵌入式固件开发

## 规则强度与优先级

- 将“必须”视为正确性、安全性或接口契约要求。
- 将“应该”视为默认选择；有仓库证据或明确权衡时允许偏离。
- 将“可以”视为可选方案。
- 按用户明确要求、仓库规则与自动化工具、邻近代码、此 Skill 默认值的顺序解决冲突。
- 不为套用本 Skill 而重构已稳定的工程。

## 工作流程

1. 检查仓库的 `AGENTS.md`、构建文件、目录结构、相关头文件、实现、配置和测试。
2. 确认目标平台、执行上下文、现有分层、资源限制和兼容要求；只在与任务有关时调查这些信息。
3. 搜索已有接口、模块和相似实现，确定最小修改面。
4. 根据任务读取下面直接关联的 reference；不要一次加载全部文件。
5. 新建工程或执行架构重构时，先建立可从目录名直接识别的分层，再放置模块和代码。
6. 先确定接口契约、失败路径、并发边界和验证方法，再实施修改。
7. 编写自研源码时使用详细中文注释；修改已有工程时兼顾其既有格式，避免无关代码批量改注释。
8. 运行仓库已有的构建、测试、静态检查或目标平台检查。
9. 报告结果、实际验证、未覆盖风险和必要的文档同步。

## Reference 路由

- 新模块、目录职责、层间依赖或平台隔离：读取 [references/architecture.md](references/architecture.md)。
- C/C++ 风格、内存、并发、ISR、数据类型或资源约束：读取 [references/coding.md](references/coding.md)。
- 新增或修改自研 C/C++ 源文件、函数或硬件配置代码：必须读取 [references/commenting.md](references/commenting.md)。
- 公开函数、回调、缓冲区、所有权或兼容性：读取 [references/api.md](references/api.md)。
- 错误码、清理、传播、重试、恢复或安全状态：读取 [references/error-handling.md](references/error-handling.md)。
- 日志接口、级别、限频、敏感数据或诊断信息：读取 [references/logging.md](references/logging.md)。
- 状态、事件、超时、转换或状态机实现：读取 [references/state-machine.md](references/state-machine.md)。
- README、接口说明、配置、架构或变更文档：读取 [references/documentation.md](references/documentation.md)。
- 构建、测试、静态分析和资源验证：读取 [references/verification.md](references/verification.md)。

## 实施边界

- 修改现有工程时输出或提交最小补丁；仅在用户要求、文件很短或局部修改不安全时提供完整替换文件。
- 不凭空发明 MCU、RTOS、引脚、时钟、协议、线程模型或现有 API。
- 可以采用明确且低风险的假设继续工作，但必须标注会影响实现的假设。
- 不把“架构纯洁”置于正确性、可验证性、实时性或项目一致性之上。
- 采用满足需求的最简单可靠实现；只有存在明确的数据一致性、并发、实时性或安全依据时，才增加双缓冲、额外任务、锁或抽象层。
