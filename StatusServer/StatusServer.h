#pragma once

// 启动并阻塞运行 StatusServer，直到收到退出信号。
// main() 只负责捕获异常，具体启动步骤集中在 RunServer() 中。
void RunServer();
