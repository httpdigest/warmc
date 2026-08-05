int warmc_cli_main(int argc, char **argv);

int (*warmc_driver_hook)(int argc, char **argv) = warmc_cli_main;
