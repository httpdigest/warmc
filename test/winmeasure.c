/* Measure one child process tree on Windows: wall clock, CPU time and peak
 * memory, gathered through a Job object so helper subprocesses count too.
 * Cygwin's wait4() cannot see any of this for native children: the MSYS
 * python that drives the test harness execs a native PE through a proxy
 * stub, and the rusage it reports describes the stub, not the child.
 *
 *   winmeasure <report-file> <timeout-ms> <program> [args...]
 *
 * Runs the child inside a kill-on-close Job (0 timeout-ms = no timeout),
 * waits for the root process, then writes one line to the report file:
 *
 *   <wall-ns> <user-ns> <kernel-ns> <peak-workingset-bytes> <peak-job-commit-bytes>
 *
 * CPU time and peak job commit cover every process that ran in the job;
 * peak working set is the root process's.  Exits with the child's exit
 * code, 258 after killing the whole job on timeout. */
#define PSAPI_VERSION 2
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: winmeasure <report-file> <timeout-ms> <cmd...>\n");
    return 125;
  }
  const char *report = argv[1];
  DWORD timeout = (DWORD)strtoul(argv[2], 0, 10);

  /* Resolve the program ourselves and hand it to CreateProcess as
   * lpApplicationName, which is taken verbatim: the command-line parser
   * would append .exe to an extensionless name, and warmc's output has no
   * extension.  Bare names (an engine on PATH) go through SearchPath. */
  static char prog[MAX_PATH + 8], found[MAX_PATH + 8];
  {
    const char *a = argv[3];
    size_t l = strlen(a);
    if (l >= MAX_PATH) {
      fprintf(stderr, "winmeasure: program path too long\n");
      return 125;
    }
    for (size_t j = 0; j < l; j++)
      prog[j] = a[j] == '/' ? '\\' : a[j];
    prog[l] = 0;
    if (!strchr(prog, '\\')) {
      if (SearchPathA(0, prog, ".exe", sizeof found, found, 0))
        memcpy(prog, found, sizeof found);
    } else if (GetFileAttributesA(prog) == INVALID_FILE_ATTRIBUTES) {
      memcpy(prog + l, ".exe", 5);
      if (GetFileAttributesA(prog) == INVALID_FILE_ATTRIBUTES)
        prog[l] = 0;
    }
  }

  /* Reconstruct a command line; the harness passes plain paths, so only
   * whitespace needs quoting. */
  static char cmd[32768];
  size_t n = 0;
  for (int i = 3; i < argc; i++) {
    const char *a = argv[i];
    size_t l = strlen(a);
    int q = !l || strchr(a, ' ') || strchr(a, '\t');
    if (n + l + 4 >= sizeof cmd) {
      fprintf(stderr, "winmeasure: command too long\n");
      return 125;
    }
    if (i > 3) cmd[n++] = ' ';
    if (q) cmd[n++] = '"';
    for (size_t j = 0; j < l; j++)
      cmd[n++] = (i == 3 && a[j] == '/') ? '\\' : a[j];
    if (q) cmd[n++] = '"';
  }
  cmd[n] = 0;

  HANDLE job = CreateJobObjectA(0, 0);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION lim;
  memset(&lim, 0, sizeof lim);
  lim.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &lim, sizeof lim);

  STARTUPINFOA si;
  memset(&si, 0, sizeof si);
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  PROCESS_INFORMATION pi;
  LARGE_INTEGER freq, t0, t1;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&t0);
  if (!CreateProcessA(prog, cmd, 0, 0, TRUE, CREATE_SUSPENDED, 0, 0, &si, &pi)) {
    fprintf(stderr, "winmeasure: CreateProcess failed (%lu): %s | %s\n",
            (unsigned long)GetLastError(), prog, cmd);
    return 126;
  }
  AssignProcessToJobObject(job, pi.hProcess);
  ResumeThread(pi.hThread);
  DWORD rc = 258;
  if (WaitForSingleObject(pi.hProcess, timeout ? timeout : INFINITE) == WAIT_OBJECT_0)
    GetExitCodeProcess(pi.hProcess, &rc);
  else
    TerminateJobObject(job, 258);
  QueryPerformanceCounter(&t1);

  JOBOBJECT_BASIC_ACCOUNTING_INFORMATION acct;
  memset(&acct, 0, sizeof acct);
  QueryInformationJobObject(job, JobObjectBasicAccountingInformation,
                            &acct, sizeof acct, 0);
  QueryInformationJobObject(job, JobObjectExtendedLimitInformation,
                            &lim, sizeof lim, 0);
  PROCESS_MEMORY_COUNTERS pmc;
  memset(&pmc, 0, sizeof pmc);
  pmc.cb = sizeof pmc;
  GetProcessMemoryInfo(pi.hProcess, &pmc, sizeof pmc);

  unsigned long long wall_ns = (unsigned long long)
      ((double)(t1.QuadPart - t0.QuadPart) * 1e9 / (double)freq.QuadPart);
  FILE *f = fopen(report, "w");
  if (!f)
    return 125;
  fprintf(f, "%llu %llu %llu %llu %llu\n", wall_ns,
          (unsigned long long)acct.TotalUserTime.QuadPart * 100ull,
          (unsigned long long)acct.TotalKernelTime.QuadPart * 100ull,
          (unsigned long long)pmc.PeakWorkingSetSize,
          (unsigned long long)lim.PeakJobMemoryUsed);
  fclose(f);
  return (int)rc;
}
