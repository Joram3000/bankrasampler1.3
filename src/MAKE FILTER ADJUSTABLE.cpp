// // ...existing code...

// // Vervang de enkele filter-declaraties door concrete instances van elk type
// enum class FilterMode { LowPass = 0, HighPass = 1, BandPass = 2 };
// static FilterMode currentFilterMode = FilterMode::LowPass;

// static LowPassFilter<float> lowPassL;
// static LowPassFilter<float> lowPassR;
// static HighPassFilter<float> highPassL;
// static HighPassFilter<float> highPassR;
// static BandPassFilter<float> bandPassL;
// static BandPassFilter<float> bandPassR;

// // ...existing code...

// // Helper: zet welke filter-objecten de filteredStream moet gebruiken
// static void applyFilterMode(FilterMode mode) {
//   currentFilterMode = mode;
//   switch (mode) {
//     case FilterMode::LowPass:
//       filteredStream.setFilter(0, &lowPassL);
//       filteredStream.setFilter(1, &lowPassR);
//       break;
//     case FilterMode::HighPass:
//       filteredStream.setFilter(0, &highPassL);
//       filteredStream.setFilter(1, &highPassR);
//       break;
//     case FilterMode::BandPass:
//       filteredStream.setFilter(0, &bandPassL);
//       filteredStream.setFilter(1, &bandPassR);
//       break;
//   }
//   // (her)initialiseer cutoff/Q voor de nieuw ingestelde filters
//   updateCutoff(filterCutoff);
// }

// // Pas updateCutoff aan om op het huidig geselecteerde filtertype te werken
// void updateCutoff(float target) {
//   smoothedCutoff += 0.1f * (target - smoothedCutoff);

//   static float lastAppliedQ = -1.0f;
//   static float lastAppliedFreq = -1.0f;

//   float qToUse = LOW_PASS_Q;
//   // als je per-mode verschillende Q wil kun je hier per-mode Q kiezen of uit instellingen halen
//   if (auto ss = static_cast<decltype(getSettingsScreen())>(getSettingsScreen())) {
//     qToUse = ss->getFilterQ();
//   }

//   if (fabsf(qToUse - lastAppliedQ) > 0.05f || fabsf(smoothedCutoff - lastAppliedFreq) > 0.5f) {
//     switch (currentFilterMode) {
//       case FilterMode::LowPass:
//         lowPassL.begin(smoothedCutoff, info.sample_rate, qToUse);
//         lowPassR.begin(smoothedCutoff, info.sample_rate, qToUse);
//         break;
//       case FilterMode::HighPass:
//         highPassL.begin(smoothedCutoff, info.sample_rate, qToUse);
//         highPassR.begin(smoothedCutoff, info.sample_rate, qToUse);
//         break;
//       case FilterMode::BandPass:
//         // Bandpass: begin(centerFreq, sampleRate, Q) — pas aan als jouw BandPass API anders is
//         bandPassL.begin(smoothedCutoff, info.sample_rate, qToUse);
//         bandPassR.begin(smoothedCutoff, info.sample_rate, qToUse);
//         break;
//     }
//     lastAppliedQ = qToUse;
//     lastAppliedFreq = smoothedCutoff;
//   }
// }

// // ...existing code...

// void initAudio() {
//   // ...existing config code...

//   // in plaats van één setFilter -> kies het huidige filtertype
//   applyFilterMode(currentFilterMode);

//   // ...rest van initAudio...
// }

// // Voorbeeld: functie om te cycelen door filter-modi (koppel aan button/switch/settings)
// static void cycleFilterMode() {
//   int next = (static_cast<int>(currentFilterMode) + 1) % 3;
//   applyFilterMode(static_cast<FilterMode>(next));
//   if (DEBUGMODE) {
//     Serial.print(F("Filter mode -> "));
//     Serial.println(next);
//   }
// }

// // Voorstel waar te koppelen: - maak een button of settings-item dat cycleFilterMode() aanroept
// // bv. in onMuxChange: als channel == FILTER_MODE_SWITCH_CHANNEL en active==true -> cycleFilterMode()

// // ...existing code...