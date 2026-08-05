struct WarmInstance;
void warmc_gcth_run_tests(void);
void warmc_rt_report(struct WarmInstance *inst);

void (*warmc_gcth_tests)(void) = warmc_gcth_run_tests;

void (*warmc_report_extra)(struct WarmInstance *inst) = warmc_rt_report;
