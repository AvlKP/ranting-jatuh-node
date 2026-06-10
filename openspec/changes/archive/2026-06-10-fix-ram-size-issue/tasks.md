## 1. Baseline Diagnostics

- [x] 1.1 Add a small heap diagnostics helper that reports free internal heap, minimum free heap, and largest free internal allocation block.
- [x] 1.2 Log heap checkpoints around WiFi init, dashboard start, logger task start, and monitor task start in the normal boot path.
- [x] 1.3 Update `Logger::Start()` and `Monitor::Start()` failure logs to include task name, requested stack size, free internal heap, and largest free internal block.

## 2. RAM Budget Reduction

- [x] 2.1 Review runtime allocations from dashboard HTTPD stack, WiFi buffers, logger queue/task stack, and monitor task stack using current boot logs and code.
- [x] 2.2 Reduce dashboard HTTP server stack size only as far as dashboard status, file listing, and download workflows keep measured stack margin.
- [x] 2.3 Reduce application task stack sizes only where high-water measurements prove safe margin.
- [x] 2.4 Tune WiFi/lwIP buffer settings conservatively if task-stack and startup-order changes do not leave enough heap margin.
- [x] 2.5 Reorder startup so critical `logger_task` and `monitor_task` start before optional dashboard service when this improves heap reliability without breaking dashboard behavior.

## 3. Safety Checks

- [x] 3.1 Keep monitor sample update path free of heap allocation and file/network I/O after RAM changes.
- [x] 3.2 Add or update compile-time checks for any changed monitor/logger/dashboard buffer limits.
- [x] 3.3 Ensure startup failure paths do not log the normal all-tasks-started message.

## 4. Verification

- [x] 4.1 Run `idf.py build`.
- [x] 4.2 Run `idf.py size` and record DIRAM impact.
- [ ] 4.3 Flash/monitor the normal build and confirm `logger_task`, `monitor_task`, and dashboard start successfully.
- [ ] 4.4 Record boot heap diagnostics after all critical runtime tasks have started.
- [ ] 4.5 Exercise dashboard status and file download workflows, then record relevant stack high-water margins where task handles are available.
