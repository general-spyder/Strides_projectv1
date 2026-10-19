# Strides - mix analyser (Windows .exe + VST3)

Built with JUCE 8 (C++17). One code base produces:
- `Strides.exe` (standalone app)  
- `Strides.vst3` (plugin: audio passes through untouched; press "Capture from DAW" and play your song to analyse it)

## Build (Windows 10/11)
1. Install **Visual Studio 2022** (workload "Desktop development with C++") and **CMake**.
2. Download **Poppins** from fonts.google.com/specimen/Poppins and copy these three files into `Resources/`:
   `Poppins-Regular.ttf`, `Poppins-SemiBold.ttf`, `Poppins-Bold.ttf`  (they get embedded in the exe/plugin).
3. In this folder, open a terminal:
   ```
   cmake -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release
   ```
4. Results:
   - `build\Strides_artefacts\Release\Standalone\Strides.exe`
   - `build\Strides_artefacts\Release\VST3\Strides.vst3` -> copy to `C:\Program Files\Common Files\VST3\`

## Using it
References tab: add up to 5 reference tracks (WAV/MP3, buttons or drag & drop) ->
Analyse tab: load your song, see 10 colour cards (similarity %, you vs. reference marker) ->
Checklist tab: prioritised to-do list with time ranges to focus on -> 
Review tab: load your revised mix, each checklist item becomes Fixed / Improved / No change / Worse / Overcorrected ->
"Print report" opens a formatted report in your browser with the print dialog (choose "Save as PDF" if you prefer).

## Licence note
JUCE is AGPLv3 / commercial. Selling Strides closed-source needs a JUCE commercial licence. Poppins is SIL OFL (free to embed).
