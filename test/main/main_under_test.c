// Compile the production implementation into the test app, but rename app_main
// so Unity's app entry point remains the only app_main symbol.
#define app_main wwvb_app_main_disabled_for_tests
#include "../../main/main.c"
