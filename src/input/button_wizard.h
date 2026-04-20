#pragma once

// Runs the interactive button assignment wizard.
// Blocks until all 6 sample buttons, the delay switch and the filter switch
// have been pressed/activated. Writes the result to /pin_config.txt on SD
// and updates the runtime pin config arrays.
//
// Prerequisite: SD must be mounted, display must be initialised.
void runButtonWizard();
